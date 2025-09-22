/*	$OpenBSD: pf_lb.c,v 1.76 2025/08/18 11:27:58 yasuoka Exp $ */

/*
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002 - 2008 Henning Brauer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/stdint.h>

#include <crypto/siphash.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#endif /* INET6 */

#include <net/pfvar.h>
#include <net/pfvar_priv.h>

u_int64_t		 pf_hash(struct pf_addr *, struct pf_addr *,
			    struct pf_poolhashkey *, sa_family_t);
int			 pf_get_sport(struct pf_pdesc *, struct pf_rule *,
			    struct pf_addr *, u_int16_t *, u_int16_t,
			    u_int16_t, struct pf_src_node **);
int			 pf_map_addr_states_increase(sa_family_t,
				struct pf_pool *, struct pf_addr *);
int			 pf_get_transaddr_af(struct pf_rule *,
			    struct pf_pdesc *, struct pf_src_node **);
int			 pf_map_addr_sticky(sa_family_t, struct pf_rule *,
			    struct pf_addr *, struct pf_addr *,
			    struct pf_src_node **, struct pf_pool *,
			    enum pf_sn_types);
int			 pf_pool_states_decrease_addr(struct pf_pool *, int,
			    struct pf_addr *);

u_int64_t
pf_hash(struct pf_addr *inaddr, struct pf_addr *hash,
    struct pf_poolhashkey *key, sa_family_t af)
{
	uint64_t res = 0;
#ifdef INET6
	union {
		uint64_t hash64;
		uint32_t hash32[2];
	} h;
#endif	/* INET6 */

	switch (af) {
	case AF_INET:
		res = SipHash24((SIPHASH_KEY *)key,
		    &inaddr->addr32[0], sizeof(inaddr->addr32[0]));
		hash->addr32[0] = res;
		break;
#ifdef INET6
	case AF_INET6:
		res = SipHash24((SIPHASH_KEY *)key, &inaddr->addr32[0],
		    4 * sizeof(inaddr->addr32[0]));
		h.hash64 = res;
		hash->addr32[0] = h.hash32[0];
		hash->addr32[1] = h.hash32[1];
		/*
		 * siphash isn't big enough, but flipping it around is
		 * good enough here.
		 */
		hash->addr32[2] = ~h.hash32[1];
		hash->addr32[3] = ~h.hash32[0];
		break;
#endif /* INET6 */
	default:
		unhandled_af(af);
	}
	return (res);
}

int
pf_get_sport(struct pf_pdesc *pd, struct pf_rule *r,
    struct pf_addr *naddr, u_int16_t *nport, u_int16_t low, u_int16_t high,
    struct pf_src_node **sn)
{
	struct pf_state_key_cmp	key;
	struct pf_addr		init_addr;
	u_int16_t		cut;
	int			dir = (pd->dir == PF_IN) ? PF_OUT : PF_IN;
	int			sidx = pd->sidx;
	int			didx = pd->didx;

	memset(&init_addr, 0, sizeof(init_addr));
	if (pf_map_addr(pd->naf, r, &pd->nsaddr, naddr, &init_addr, sn, &r->nat,
	    PF_SN_NAT))
		return (1);

	if (pd->proto == IPPROTO_ICMP) {
		if (pd->ndport == htons(ICMP_ECHO)) {
			low = 1;
			high = 65535;
		} else
			return (0);	/* Don't try to modify non-echo ICMP */
	}
#ifdef INET6
	if (pd->proto == IPPROTO_ICMPV6) {
		if (pd->ndport == htons(ICMP6_ECHO_REQUEST)) {
			low = 1;
			high = 65535;
		} else
			return (0);	/* Don't try to modify non-echo ICMP */
	}
#endif /* INET6 */

	do {
		key.af = pd->naf;
		key.proto = pd->proto;
		key.rdomain = pd->rdomain;
		pf_addrcpy(&key.addr[didx], &pd->ndaddr, key.af);
		pf_addrcpy(&key.addr[sidx], naddr, key.af);
		key.port[didx] = pd->ndport;

		/*
		 * port search; start random, step;
		 * similar 2 portloop in in_pcbbind
		 */
		if (!(pd->proto == IPPROTO_TCP || pd->proto == IPPROTO_UDP ||
		    pd->proto == IPPROTO_ICMP || pd->proto == IPPROTO_ICMPV6)) {
			/* XXX bug: icmp states dont use the id on both
			 * XXX sides (traceroute -I through nat) */
			key.port[sidx] = pd->nsport;
			key.hash = pf_pkt_hash(key.af, key.proto, &key.addr[0],
			    &key.addr[1], key.port[0], key.port[1]);
			if (pf_find_state_all(&key, dir, NULL) == NULL) {
				*nport = pd->nsport;
				return (0);
			}
		} else if (low == 0 && high == 0) {
			key.port[sidx] = pd->nsport;
			key.hash = pf_pkt_hash(key.af, key.proto, &key.addr[0],
			    &key.addr[1], key.port[0], key.port[1]);
			if (pf_find_state_all(&key, dir, NULL) == NULL) {
				*nport = pd->nsport;
				return (0);
			}
		} else if (low == high) {
			key.port[sidx] = htons(low);
			key.hash = pf_pkt_hash(key.af, key.proto, &key.addr[0],
			    &key.addr[1], key.port[0], key.port[1]);
			if (pf_find_state_all(&key, dir, NULL) == NULL) {
				*nport = htons(low);
				return (0);
			}
		} else {
			u_int32_t tmp;

			if (low > high) {
				tmp = low;
				low = high;
				high = tmp;
			}
			/* low < high */
			cut = arc4random_uniform(1 + high - low) + low;
			/* low <= cut <= high */
			for (tmp = cut; tmp <= high && tmp <= 0xffff; ++tmp) {
				key.port[sidx] = htons(tmp);
				key.hash = pf_pkt_hash(key.af, key.proto,
				    &key.addr[0], &key.addr[1], key.port[0],
				    key.port[1]);
				if (pf_find_state_all(&key, dir, NULL) ==
				    NULL && !in_baddynamic(tmp, pd->proto)) {
					*nport = htons(tmp);
					return (0);
				}
			}
			tmp = cut;
			for (tmp -= 1; tmp >= low && tmp <= 0xffff; --tmp) {
				key.port[sidx] = htons(tmp);
				key.hash = pf_pkt_hash(key.af, key.proto,
				    &key.addr[0], &key.addr[1], key.port[0],
				    key.port[1]);
				if (pf_find_state_all(&key, dir, NULL) ==
				    NULL && !in_baddynamic(tmp, pd->proto)) {
					*nport = htons(tmp);
					return (0);
				}
			}
		}

		switch (r->nat.opts & PF_POOL_TYPEMASK) {
		case PF_POOL_RANDOM:
		case PF_POOL_ROUNDROBIN:
		case PF_POOL_LEASTSTATES:
			/*
			 * pick a different source address since we're out
			 * of free port choices for the current one.
			 */
			if (pf_map_addr(pd->naf, r, &pd->nsaddr, naddr,
			    &init_addr, sn, &r->nat, PF_SN_NAT))
				return (1);
			break;
		case PF_POOL_NONE:
		case PF_POOL_SRCHASH:
		case PF_POOL_BITMASK:
		default:
			return (1);
		}
	} while (! PF_AEQ(&init_addr, naddr, pd->naf) );
	return (1);					/* none available */
}

int
pf_map_addr_sticky(sa_family_t af, struct pf_rule *r, struct pf_addr *saddr,
    struct pf_addr *naddr, struct pf_src_node **sns, struct pf_pool *rpool,
    enum pf_sn_types type)
{
	struct pf_addr		*raddr, *rmask, *cached;
	struct pf_state		*s;
	struct pf_src_node	 k;
	int			 valid;

	k.af = af;
	k.type = type;
	pf_addrcpy(&k.addr, saddr, af);
	k.rule.ptr = r;
	pf_status.scounters[SCNT_SRC_NODE_SEARCH]++;
	sns[type] = RB_FIND(pf_src_tree, &tree_src_tracking, &k);
	if (sns[type] == NULL)
		return (-1);

	/* check if the cached entry is still valid */
	cached = &(sns[type])->raddr;
	valid = 0;
	if (PF_AZERO(cached, af)) {
		valid = 1;
	} else if (rpool->addr.type == PF_ADDR_DYNIFTL) {
		if (pfr_kentry_byaddr(rpool->addr.p.dyn->pfid_kt, cached,
		    af, 0))
			valid = 1;
	} else if (rpool->addr.type == PF_ADDR_TABLE) {
		if (pfr_kentry_byaddr(rpool->addr.p.tbl, cached, af, 0))
			valid = 1;
	} else if (rpool->addr.type != PF_ADDR_NOROUTE) {
		raddr = &rpool->addr.v.a.addr;
		rmask = &rpool->addr.v.a.mask;
		valid = pf_match_addr(0, raddr, rmask, cached, af);
	}
	if (!valid) {
		if (pf_status.debug >= LOG_DEBUG) {
			log(LOG_DEBUG, "pf: pf_map_addr: "
			    "stale src tracking (%u) ", type);
			pf_print_host(&k.addr, 0, af);
			addlog(" to ");
			pf_print_host(cached, 0, af);
			addlog("\n");
		}
		if (sns[type]->states != 0) {
			/* XXX expensive */
			RBT_FOREACH(s, pf_state_tree_id, &tree_id)
				pf_state_rm_src_node(s, sns[type]);
		}
		sns[type]->expire = 1;
		pf_remove_src_node(sns[type]);
		sns[type] = NULL;
		return (-1);
	}


	if (!PF_AZERO(cached, af)) {
		pf_addrcpy(naddr, cached, af);
		if ((rpool->opts & PF_POOL_TYPEMASK) == PF_POOL_LEASTSTATES &&
		    pf_map_addr_states_increase(af, rpool, cached) == -1)
			return (-1);
	}
	if (pf_status.debug >= LOG_DEBUG) {
		log(LOG_DEBUG, "pf: pf_map_addr: "
		    "src tracking (%u) maps ", type);
		pf_print_host(&k.addr, 0, af);
		addlog(" to ");
		pf_print_host(naddr, 0, af);
		addlog("\n");
	}

	if (sns[type]->kif != NULL)
		rpool->kif = sns[type]->kif;

	return (0);
}

uint32_t
pf_rand_addr(uint32_t mask)
{
	uint32_t addr;

	mask = ~ntohl(mask);
	addr = arc4random_uniform(mask + 1);

	return (htonl(addr));
}

int
pf_map_addr(sa_family_t af, struct pf_rule *r, struct pf_addr *saddr,
    struct pf_addr *naddr, struct pf_addr *init_addr, struct pf_src_node **sns,
    struct pf_pool *rpool, enum pf_sn_types type)
{
	struct pf_addr		 hash;
	struct pf_addr		 faddr;
	struct pf_addr		*raddr = &rpool->addr.v.a.addr;
	struct pf_addr		*rmask = &rpool->addr.v.a.mask;
	struct pfr_ktable	*kt;
	struct pfi_kif		*kif;
	u_int64_t		 states;
	u_int16_t		 weight;
	u_int64_t		 load;
	u_int64_t		 cload;
	u_int64_t		 hashidx;
	int			 cnt;

	if (sns[type] == NULL && rpool->opts & PF_POOL_STICKYADDR &&
	    (rpool->opts & PF_POOL_TYPEMASK) != PF_POOL_NONE &&
	    pf_map_addr_sticky(af, r, saddr, naddr, sns, rpool, type) == 0)
		return (0);

	if (rpool->addr.type == PF_ADDR_NOROUTE)
		return (1);
	if (rpool->addr.type == PF_ADDR_DYNIFTL) {
		switch (af) {
		case AF_INET:
			if (rpool->addr.p.dyn->pfid_acnt4 < 1 &&
			    !PF_POOL_DYNTYPE(rpool->opts))
				return (1);
			raddr = &rpool->addr.p.dyn->pfid_addr4;
			rmask = &rpool->addr.p.dyn->pfid_mask4;
			break;
#ifdef INET6
		case AF_INET6:
			if (rpool->addr.p.dyn->pfid_acnt6 < 1 &&
			    !PF_POOL_DYNTYPE(rpool->opts))
				return (1);
			raddr = &rpool->addr.p.dyn->pfid_addr6;
			rmask = &rpool->addr.p.dyn->pfid_mask6;
			break;
#endif /* INET6 */
		default:
			unhandled_af(af);
		}
	} else if (rpool->addr.type == PF_ADDR_TABLE) {
		if (!PF_POOL_DYNTYPE(rpool->opts))
			return (1); /* unsupported */
	} else {
		raddr = &rpool->addr.v.a.addr;
		rmask = &rpool->addr.v.a.mask;
	}

	switch (rpool->opts & PF_POOL_TYPEMASK) {
	case PF_POOL_NONE:
		pf_addrcpy(naddr, raddr, af);
		break;
	case PF_POOL_BITMASK:
		pf_poolmask(naddr, raddr, rmask, saddr, af);
		break;
	case PF_POOL_RANDOM:
		if (rpool->addr.type == PF_ADDR_TABLE ||
		    rpool->addr.type == PF_ADDR_DYNIFTL) {
			if (rpool->addr.type == PF_ADDR_TABLE)
				kt = rpool->addr.p.tbl;
			else
				kt = rpool->addr.p.dyn->pfid_kt;
			kt = pfr_ktable_select_active(kt);
			if (kt == NULL)
				return (1);

			cnt = kt->pfrkt_cnt;
			if (cnt == 0)
				rpool->tblidx = 0;
			else
				rpool->tblidx = (int)arc4random_uniform(cnt);
			memset(&rpool->counter, 0, sizeof(rpool->counter));
			if (pfr_pool_get(rpool, &raddr, &rmask, af))
				return (1);
			pf_addrcpy(naddr, &rpool->counter, af);
		} else if (init_addr != NULL && PF_AZERO(init_addr, af)) {
			switch (af) {
			case AF_INET:
				rpool->counter.addr32[0] = pf_rand_addr(
				    rmask->addr32[0]);
				break;
#ifdef INET6
			case AF_INET6:
				if (rmask->addr32[3] != 0xffffffff)
					rpool->counter.addr32[3] = pf_rand_addr(
					    rmask->addr32[3]);
				else
					break;
				if (rmask->addr32[2] != 0xffffffff)
					rpool->counter.addr32[2] = pf_rand_addr(
					    rmask->addr32[2]);
				else
					break;
				if (rmask->addr32[1] != 0xffffffff)
					rpool->counter.addr32[1] = pf_rand_addr(
					    rmask->addr32[1]);
				else
					break;
				if (rmask->addr32[0] != 0xffffffff)
					rpool->counter.addr32[0] = pf_rand_addr(
					    rmask->addr32[0]);
				break;
#endif /* INET6 */
			default:
				unhandled_af(af);
			}
			pf_poolmask(naddr, raddr, rmask, &rpool->counter, af);
			pf_addrcpy(init_addr, naddr, af);

		} else {
			pf_addr_inc(&rpool->counter, af);
			pf_poolmask(naddr, raddr, rmask, &rpool->counter, af);
		}
		break;
	case PF_POOL_SRCHASH:
		hashidx = pf_hash(saddr, &hash, &rpool->key, af);

		if (rpool->addr.type == PF_ADDR_TABLE ||
		    rpool->addr.type == PF_ADDR_DYNIFTL) {
			if (rpool->addr.type == PF_ADDR_TABLE)
				kt = rpool->addr.p.tbl;
			else
				kt = rpool->addr.p.dyn->pfid_kt;
			kt = pfr_ktable_select_active(kt);
			if (kt == NULL)
				return (1);

			cnt = kt->pfrkt_cnt;
			if (cnt == 0)
				rpool->tblidx = 0;
			else
				rpool->tblidx = (int)(hashidx % cnt);
			memset(&rpool->counter, 0, sizeof(rpool->counter));
			if (pfr_pool_get(rpool, &raddr, &rmask, af))
				return (1);
			pf_addrcpy(naddr, &rpool->counter, af);
		} else {
			pf_poolmask(naddr, raddr, rmask, &hash, af);
		}
		break;
	case PF_POOL_ROUNDROBIN:
		if (rpool->addr.type == PF_ADDR_TABLE ||
		    rpool->addr.type == PF_ADDR_DYNIFTL) {
			if (pfr_pool_get(rpool, &raddr, &rmask, af)) {
				/*
				 * reset counter in case its value
				 * has been removed from the pool.
				 */
				memset(&rpool->counter, 0,
				    sizeof(rpool->counter));
				if (pfr_pool_get(rpool, &raddr, &rmask, af))
					return (1);
			}
		} else if (PF_AZERO(&rpool->counter, af)) {
			/*
			 * fall back to POOL_NONE if there is a single host
			 * address in pool.
			 */
			if (af == AF_INET &&
			    rmask->addr32[0] == INADDR_BROADCAST) {
				pf_addrcpy(naddr, raddr, af);
				break;
			}
#ifdef INET6
			if (af == AF_INET6 &&
			    IN6_ARE_ADDR_EQUAL(&rmask->v6, &in6mask128)) {
				pf_addrcpy(naddr, raddr, af);
				break;
			}
#endif
		} else if (pf_match_addr(0, raddr, rmask, &rpool->counter, af))
			return (1);

		/* iterate over table if it contains entries which are weighted */
		if ((rpool->addr.type == PF_ADDR_TABLE &&
		    rpool->addr.p.tbl->pfrkt_refcntcost > 0) ||
		    (rpool->addr.type == PF_ADDR_DYNIFTL &&
		    rpool->addr.p.dyn->pfid_kt->pfrkt_refcntcost > 0)) {
			do {
				if (rpool->addr.type == PF_ADDR_TABLE ||
				    rpool->addr.type == PF_ADDR_DYNIFTL) {
					if (pfr_pool_get(rpool,
					    &raddr, &rmask, af))
						return (1);
				} else {
					log(LOG_ERR, "pf: pf_map_addr: "
					    "weighted RR failure");
					return (1);
				}
				if (rpool->weight >= rpool->curweight)
					break;
				pf_addr_inc(&rpool->counter, af);
			} while (1);
 
			weight = rpool->weight;
		}

		pf_poolmask(naddr, raddr, rmask, &rpool->counter, af);
		if (init_addr != NULL && PF_AZERO(init_addr, af))
			pf_addrcpy(init_addr, &rpool->counter, af);
		pf_addr_inc(&rpool->counter, af);
		break;
	case PF_POOL_LEASTSTATES:
		/* retrieve an address first */
		if (rpool->addr.type == PF_ADDR_TABLE ||
		    rpool->addr.type == PF_ADDR_DYNIFTL) {
			if (pfr_pool_get(rpool, &raddr, &rmask, af)) {
				/* see PF_POOL_ROUNDROBIN */
				memset(&rpool->counter, 0,
				    sizeof(rpool->counter));
				if (pfr_pool_get(rpool, &raddr, &rmask, af))
					return (1);
			}
		} else if (pf_match_addr(0, raddr, rmask, &rpool->counter, af))
			return (1);

		states = rpool->states;
		weight = rpool->weight;
		kif = rpool->kif;

		if ((rpool->addr.type == PF_ADDR_TABLE &&
		    rpool->addr.p.tbl->pfrkt_refcntcost > 0) ||
		    (rpool->addr.type == PF_ADDR_DYNIFTL &&
		    rpool->addr.p.dyn->pfid_kt->pfrkt_refcntcost > 0))
			load = ((UINT16_MAX * rpool->states) / rpool->weight);
		else
			load = states;

		pf_addrcpy(&faddr, &rpool->counter, af);

		pf_addrcpy(naddr, &rpool->counter, af);
		if (init_addr != NULL && PF_AZERO(init_addr, af))
			pf_addrcpy(init_addr, naddr, af);

		/*
		 * iterate *once* over whole table and find destination with
		 * least connection
		 */
		do  {
			pf_addr_inc(&rpool->counter, af);
			if (rpool->addr.type == PF_ADDR_TABLE ||
			    rpool->addr.type == PF_ADDR_DYNIFTL) {
				if (pfr_pool_get(rpool, &raddr, &rmask, af))
					return (1);
			} else if (pf_match_addr(0, raddr, rmask,
			    &rpool->counter, af))
				return (1);

			if ((rpool->addr.type == PF_ADDR_TABLE &&
			    rpool->addr.p.tbl->pfrkt_refcntcost > 0) ||
			    (rpool->addr.type == PF_ADDR_DYNIFTL &&
			    rpool->addr.p.dyn->pfid_kt->pfrkt_refcntcost > 0))
				cload = ((UINT16_MAX * rpool->states)
					/ rpool->weight);
			else
				cload = rpool->states;

			/* find lc minimum */
			if (cload < load) {
				states = rpool->states;
				weight = rpool->weight;
				kif = rpool->kif;
				load = cload;

				pf_addrcpy(naddr, &rpool->counter, af);
				if (init_addr != NULL &&
				    PF_AZERO(init_addr, af))
				    pf_addrcpy(init_addr, naddr, af);
			}
		} while (pf_match_addr(1, &faddr, rmask, &rpool->counter, af) &&
		    (states > 0));

		if (pf_map_addr_states_increase(af, rpool, naddr) == -1)
			return (1);
		/* revert the kif which was set by pfr_pool_get() */
		rpool->kif = kif;
		break;
	}

	if (rpool->opts & PF_POOL_STICKYADDR) {
		if (sns[type] != NULL) {
			pf_remove_src_node(sns[type]);
			sns[type] = NULL;
		}
		if (pf_insert_src_node(&sns[type], r, type, af, saddr, naddr,
		    rpool->kif))
			return (1);
	}

	if (pf_status.debug >= LOG_INFO &&
	    (rpool->opts & PF_POOL_TYPEMASK) != PF_POOL_NONE) {
		log(LOG_INFO, "pf: pf_map_addr: selected address ");
		pf_print_host(naddr, 0, af);
		if ((rpool->opts & PF_POOL_TYPEMASK) ==
		    PF_POOL_LEASTSTATES)
			addlog(" with state count %llu", states);
		if ((rpool->addr.type == PF_ADDR_TABLE &&
		    rpool->addr.p.tbl->pfrkt_refcntcost > 0) ||
		    (rpool->addr.type == PF_ADDR_DYNIFTL &&
		    rpool->addr.p.dyn->pfid_kt->pfrkt_refcntcost > 0))
			addlog(" with weight %u", weight);
		addlog("\n");
	}

	return (0);
}

int
pf_map_addr_states_increase(sa_family_t af, struct pf_pool *rpool,
    struct pf_addr *naddr)
{
	if (rpool->addr.type == PF_ADDR_TABLE) {
		if (pfr_states_increase(rpool->addr.p.tbl,
		    naddr, af) == -1) {
			if (pf_status.debug >= LOG_DEBUG) {
				log(LOG_DEBUG,
				    "pf: pf_map_addr_states_increase: "
				    "selected address ");
				pf_print_host(naddr, 0, af);
				addlog(". Failed to increase count!\n");
			}
			return (-1);
		}
	} else if (rpool->addr.type == PF_ADDR_DYNIFTL) {
		if (pfr_states_increase(rpool->addr.p.dyn->pfid_kt,
		    naddr, af) == -1) {
			if (pf_status.debug >= LOG_DEBUG) {
				log(LOG_DEBUG,
				    "pf: pf_map_addr_states_increase: "
				    "selected address ");
				pf_print_host(naddr, 0, af);
				addlog(". Failed to increase count!\n");
			}
			return (-1);
		}
	}
	return (0);
}

int
pf_get_transaddr(struct pf_rule *r, struct pf_pdesc *pd,
    struct pf_src_node **sns, struct pf_rule **nr)
{
	struct pf_addr	naddr;
	u_int16_t	nport;

#ifdef INET6
	if (pd->af != pd->naf)
		return (pf_get_transaddr_af(r, pd, sns));
#endif /* INET6 */

	if (r->nat.addr.type != PF_ADDR_NONE) {
		/* XXX is this right? what if rtable is changed at the same
		 * XXX time? where do I need to figure out the sport? */
		nport = 0;
		if (pf_get_sport(pd, r, &naddr, &nport,
		    r->nat.proxy_port[0], r->nat.proxy_port[1], sns)) {
			DPFPRINTF(LOG_NOTICE,
			    "pf: NAT proxy port allocation (%u-%u) failed",
			    r->nat.proxy_port[0],
			    r->nat.proxy_port[1]);
			return (-1);
		}
		/* decrease least-connection state counter of the previous */
		if ((*nr) != NULL && (*nr)->nat.addr.type != PF_ADDR_NONE &&
		    ((*nr)->nat.opts & PF_POOL_TYPEMASK) ==
		    PF_POOL_LEASTSTATES)
			pf_pool_states_decrease_addr(&(*nr)->nat, pd->af,
			    &pd->nsaddr);
		*nr = r;
		pf_addrcpy(&pd->nsaddr, &naddr, pd->af);
		pd->nsport = nport;
	}
	if (r->rdr.addr.type != PF_ADDR_NONE) {
		if (pf_map_addr(pd->af, r, &pd->nsaddr, &naddr, NULL, sns,
		    &r->rdr, PF_SN_RDR))
			return (-1);
		if ((r->rdr.opts & PF_POOL_TYPEMASK) == PF_POOL_BITMASK)
			pf_poolmask(&naddr, &naddr,  &r->rdr.addr.v.a.mask,
			    &pd->ndaddr, pd->af);

		nport = 0;
		if (r->rdr.proxy_port[1]) {
			u_int32_t	tmp_nport;
			u_int16_t	div;

			div = r->rdr.proxy_port[1] - r->rdr.proxy_port[0] + 1;
			div = (div == 0) ? 1 : div;

			tmp_nport = ((ntohs(pd->ndport) - ntohs(r->dst.port[0])) % div) +
			    r->rdr.proxy_port[0];

			/* wrap around if necessary */
			if (tmp_nport > 65535)
				tmp_nport -= 65535;
			nport = htons((u_int16_t)tmp_nport);
		} else if (r->rdr.proxy_port[0])
			nport = htons(r->rdr.proxy_port[0]);
		/* decrease least-connection state counter of the previous */
		if ((*nr) != NULL && (*nr)->rdr.addr.type != PF_ADDR_NONE &&
		    ((*nr)->rdr.opts & PF_POOL_TYPEMASK) ==
		    PF_POOL_LEASTSTATES)
			pf_pool_states_decrease_addr(&(*nr)->rdr, pd->af,
			    &pd->ndaddr);
		*nr = r;
		pf_addrcpy(&pd->ndaddr, &naddr, pd->af);
		if (nport)
			pd->ndport = nport;
	}

	return (0);
}

#ifdef INET6
int
pf_get_transaddr_af(struct pf_rule *r, struct pf_pdesc *pd,
    struct pf_src_node **sns)
{
	struct pf_addr	ndaddr, nsaddr, naddr;
	u_int16_t	nport;
	int		prefixlen = 96;

	if (pf_status.debug >= LOG_INFO) {
		log(LOG_INFO, "pf: af-to %s %s, ",
		    pd->naf == AF_INET ? "inet" : "inet6",
		    r->rdr.addr.type == PF_ADDR_NONE ? "nat" : "rdr");
		pf_print_host(&pd->nsaddr, pd->nsport, pd->af);
		addlog(" -> ");
		pf_print_host(&pd->ndaddr, pd->ndport, pd->af);
		addlog("\n");
	}

	if (r->nat.addr.type == PF_ADDR_NONE)
		panic("pf_get_transaddr_af: no nat pool for source address");

	/* get source address and port */
	nport = 0;
	if (pf_get_sport(pd, r, &nsaddr, &nport,
	    r->nat.proxy_port[0], r->nat.proxy_port[1], sns)) {
		DPFPRINTF(LOG_NOTICE,
		    "pf: af-to NAT proxy port allocation (%u-%u) failed",
		    r->nat.proxy_port[0],
		    r->nat.proxy_port[1]);
		return (-1);
	}
	pd->nsport = nport;

	if (pd->proto == IPPROTO_ICMPV6 && pd->naf == AF_INET) {
		if (pd->dir == PF_IN) {
			pd->ndport = ntohs(pd->ndport);
			if (pd->ndport == ICMP6_ECHO_REQUEST)
				pd->ndport = ICMP_ECHO;
			else if (pd->ndport == ICMP6_ECHO_REPLY)
				pd->ndport = ICMP_ECHOREPLY;
			pd->ndport = htons(pd->ndport);
		} else {
			pd->nsport = ntohs(pd->nsport);
			if (pd->nsport == ICMP6_ECHO_REQUEST)
				pd->nsport = ICMP_ECHO;
			else if (pd->nsport == ICMP6_ECHO_REPLY)
				pd->nsport = ICMP_ECHOREPLY;
			pd->nsport = htons(pd->nsport);
		}
	} else if (pd->proto == IPPROTO_ICMP && pd->naf == AF_INET6) {
		if (pd->dir == PF_IN) {
			pd->ndport = ntohs(pd->ndport);
			if (pd->ndport == ICMP_ECHO)
				pd->ndport = ICMP6_ECHO_REQUEST;
			else if (pd->ndport == ICMP_ECHOREPLY)
				pd->ndport = ICMP6_ECHO_REPLY;
			pd->ndport = htons(pd->ndport);
		} else {
			pd->nsport = ntohs(pd->nsport);
			if (pd->nsport == ICMP_ECHO)
				pd->nsport = ICMP6_ECHO_REQUEST;
			else if (pd->nsport == ICMP_ECHOREPLY)
				pd->nsport = ICMP6_ECHO_REPLY;
			pd->nsport = htons(pd->nsport);
		}
	}

	/* get the destination address and port */
	if (r->rdr.addr.type != PF_ADDR_NONE) {
		if (pf_map_addr(pd->naf, r, &nsaddr, &naddr, NULL, sns,
		    &r->rdr, PF_SN_RDR))
			return (-1);
		if (r->rdr.proxy_port[0])
			pd->ndport = htons(r->rdr.proxy_port[0]);

		if (pd->naf == AF_INET) {
			/* The prefix is the IPv4 rdr address */
			prefixlen = in_mask2len((struct in_addr *)
			    &r->rdr.addr.v.a.mask);
			inet_nat46(pd->naf, &pd->ndaddr,
			    &ndaddr, &naddr, prefixlen);
		} else {
			/* The prefix is the IPv6 rdr address */
			prefixlen =
			    in6_mask2len((struct in6_addr *)
			    &r->rdr.addr.v.a.mask, NULL);
			inet_nat64(pd->naf, &pd->ndaddr,
			    &ndaddr, &naddr, prefixlen);
		}
	} else {
		if (pd->naf == AF_INET) {
			/* The prefix is the IPv6 dst address */
			prefixlen =
			    in6_mask2len((struct in6_addr *)
			    &r->dst.addr.v.a.mask, NULL);
			if (prefixlen < 32)
				prefixlen = 96;
			inet_nat64(pd->naf, &pd->ndaddr,
			    &ndaddr, &pd->ndaddr, prefixlen);
		} else {
			/*
			 * The prefix is the IPv6 nat address
			 * (that was stored in pd->nsaddr)
			 */
			prefixlen = in6_mask2len((struct in6_addr *)
			    &r->nat.addr.v.a.mask, NULL);
			if (prefixlen > 96)
				prefixlen = 96;
			inet_nat64(pd->naf, &pd->ndaddr,
			    &ndaddr, &nsaddr, prefixlen);
		}
	}

	pf_addrcpy(&pd->nsaddr, &nsaddr, pd->naf);
	pf_addrcpy(&pd->ndaddr, &ndaddr, pd->naf);

	if (pf_status.debug >= LOG_INFO) {
		log(LOG_INFO, "pf: af-to %s %s done, prefixlen %d, ",
		    pd->naf == AF_INET ? "inet" : "inet6",
		    r->rdr.addr.type == PF_ADDR_NONE ? "nat" : "rdr",
		    prefixlen);
		pf_print_host(&pd->nsaddr, pd->nsport, pd->naf);
		addlog(" -> ");
		pf_print_host(&pd->ndaddr, pd->ndport, pd->naf);
		addlog("\n");
	}

	return (0);
}
#endif /* INET6 */

int
pf_postprocess_addr(struct pf_state *cur)
{
	struct pf_state_key	*sks;
	struct pf_addr		*addr;
	struct pf_pool		*rpool, *natpl = NULL, *rdrpl = NULL;
	struct pf_rule_item	*ri;
	int			 ret = 0;

	/* decrease the states counter in pool for "least-states" */
	if (cur->natrule.ptr != NULL) { /* this is the final */
		if (cur->natrule.ptr->nat.addr.type != PF_ADDR_NONE)
			natpl = &cur->natrule.ptr->nat;
		if (cur->natrule.ptr->rdr.addr.type != PF_ADDR_NONE)
			rdrpl = &cur->natrule.ptr->rdr;
	}
	if (natpl == NULL || rdrpl == NULL) {
		/* nat or rdr might be done by a previous "match" */
		SLIST_FOREACH(ri, &cur->match_rules, entry) {
			/* first match since the list order is reversed */
			if (natpl == NULL &&
			    ri->r->nat.addr.type != PF_ADDR_NONE)
				natpl = &ri->r->nat;
			if (rdrpl == NULL &&
			    ri->r->rdr.addr.type != PF_ADDR_NONE)
				rdrpl = &ri->r->rdr;
			if (natpl != NULL && rdrpl != NULL)
				break;
		}
	}
	if (natpl != NULL &&
	    (natpl->opts & PF_POOL_TYPEMASK) == PF_POOL_LEASTSTATES) {
		/* nat-to <table> least-state */
		if (cur->direction == PF_IN) {
			sks = cur->key[PF_SK_STACK];
			addr = &sks->addr[0];
		} else {
			sks = cur->key[PF_SK_WIRE];
			addr = &sks->addr[1];
		}
		if (pf_pool_states_decrease_addr(natpl, sks->af, addr) != 0)
			ret = 1;
	}
	if (rdrpl != NULL &&
	    (rdrpl->opts & PF_POOL_TYPEMASK) == PF_POOL_LEASTSTATES) {
		/* rdr <table> least-state */
		if (cur->direction == PF_IN) {
			sks = cur->key[PF_SK_STACK];
			addr = &sks->addr[1];
		} else {
			sks = cur->key[PF_SK_WIRE];
			addr = &sks->addr[0];
		}
		if (pf_pool_states_decrease_addr(rdrpl, sks->af, addr) != 0)
			ret = 1;
	}
	if (cur->rule.ptr != NULL) {
		/* route-to <table> least-state */
		rpool = &cur->rule.ptr->route;
		if (rpool->addr.type != PF_ADDR_NONE &&
		    (rpool->opts & PF_POOL_TYPEMASK) == PF_POOL_LEASTSTATES) {
			sks = cur->key[(cur->direction == PF_IN)
			    ? PF_SK_STACK : PF_SK_WIRE];
			addr = &cur->rt_addr;
			KASSERT(sks != NULL);
			if (pf_pool_states_decrease_addr(rpool, sks->af, addr)
			    != 0)
				ret = 1;
		}
	}

	return (ret);
}

int
pf_pool_states_decrease_addr(struct pf_pool *rpool, int af,
    struct pf_addr *addr)
{
	int	 slbcount = -1;

	if (rpool->addr.type == PF_ADDR_TABLE) {
		if ((slbcount = pfr_states_decrease(
		    rpool->addr.p.tbl, addr, af)) == -1) {
			if (pf_status.debug >= LOG_DEBUG) {
				log(LOG_DEBUG, "pf: %s: selected address ",
				    __func__);
				pf_print_host(addr, 0, af);
				addlog(". Failed to "
				    "decrease count!\n");
			}
			return (1);
		}
	} else if (rpool->addr.type == PF_ADDR_DYNIFTL) {
		if ((slbcount = pfr_states_decrease(
		    rpool->addr.p.dyn->pfid_kt, addr, af)) == -1) {
			if (pf_status.debug >= LOG_DEBUG) {
				log(LOG_DEBUG, "pf: %s: selected address ",
				    __func__);
				pf_print_host(addr, 0, af);
				addlog(". Failed to "
				    "decrease count!\n");
			}
			return (1);
		}
	}
	if (slbcount > -1) {
		if (pf_status.debug >= LOG_INFO) {
			log(LOG_INFO, "pf: %s: selected address ", __func__);
			pf_print_host(addr, 0, af);
			addlog(" decreased state count to %u\n",
			    slbcount);
		}
	}

	return (0);
}

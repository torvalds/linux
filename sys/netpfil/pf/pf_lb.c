/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
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
 *	$OpenBSD: pf_lb.c,v 1.2 2009/02/12 02:13:15 sthen Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_pf.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/vnet.h>
#include <net/pfvar.h>
#include <net/if_pflog.h>

#define DPFPRINTF(n, x)	if (V_pf_status.debug >= (n)) printf x

static void		 pf_hash(struct pf_addr *, struct pf_addr *,
			    struct pf_poolhashkey *, sa_family_t);
static struct pf_rule	*pf_match_translation(struct pf_pdesc *, struct mbuf *,
			    int, int, struct pfi_kif *,
			    struct pf_addr *, u_int16_t, struct pf_addr *,
			    uint16_t, int, struct pf_anchor_stackframe *);
static int pf_get_sport(sa_family_t, uint8_t, struct pf_rule *,
    struct pf_addr *, uint16_t, struct pf_addr *, uint16_t, struct pf_addr *,
    uint16_t *, uint16_t, uint16_t, struct pf_src_node **);

#define mix(a,b,c) \
	do {					\
		a -= b; a -= c; a ^= (c >> 13);	\
		b -= c; b -= a; b ^= (a << 8);	\
		c -= a; c -= b; c ^= (b >> 13);	\
		a -= b; a -= c; a ^= (c >> 12);	\
		b -= c; b -= a; b ^= (a << 16);	\
		c -= a; c -= b; c ^= (b >> 5);	\
		a -= b; a -= c; a ^= (c >> 3);	\
		b -= c; b -= a; b ^= (a << 10);	\
		c -= a; c -= b; c ^= (b >> 15);	\
	} while (0)

/*
 * hash function based on bridge_hash in if_bridge.c
 */
static void
pf_hash(struct pf_addr *inaddr, struct pf_addr *hash,
    struct pf_poolhashkey *key, sa_family_t af)
{
	u_int32_t	a = 0x9e3779b9, b = 0x9e3779b9, c = key->key32[0];

	switch (af) {
#ifdef INET
	case AF_INET:
		a += inaddr->addr32[0];
		b += key->key32[1];
		mix(a, b, c);
		hash->addr32[0] = c + key->key32[2];
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		a += inaddr->addr32[0];
		b += inaddr->addr32[2];
		mix(a, b, c);
		hash->addr32[0] = c;
		a += inaddr->addr32[1];
		b += inaddr->addr32[3];
		c += key->key32[1];
		mix(a, b, c);
		hash->addr32[1] = c;
		a += inaddr->addr32[2];
		b += inaddr->addr32[1];
		c += key->key32[2];
		mix(a, b, c);
		hash->addr32[2] = c;
		a += inaddr->addr32[3];
		b += inaddr->addr32[0];
		c += key->key32[3];
		mix(a, b, c);
		hash->addr32[3] = c;
		break;
#endif /* INET6 */
	}
}

static struct pf_rule *
pf_match_translation(struct pf_pdesc *pd, struct mbuf *m, int off,
    int direction, struct pfi_kif *kif, struct pf_addr *saddr, u_int16_t sport,
    struct pf_addr *daddr, uint16_t dport, int rs_num,
    struct pf_anchor_stackframe *anchor_stack)
{
	struct pf_rule		*r, *rm = NULL;
	struct pf_ruleset	*ruleset = NULL;
	int			 tag = -1;
	int			 rtableid = -1;
	int			 asd = 0;

	r = TAILQ_FIRST(pf_main_ruleset.rules[rs_num].active.ptr);
	while (r && rm == NULL) {
		struct pf_rule_addr	*src = NULL, *dst = NULL;
		struct pf_addr_wrap	*xdst = NULL;

		if (r->action == PF_BINAT && direction == PF_IN) {
			src = &r->dst;
			if (r->rpool.cur != NULL)
				xdst = &r->rpool.cur->addr;
		} else {
			src = &r->src;
			dst = &r->dst;
		}

		r->evaluations++;
		if (pfi_kif_match(r->kif, kif) == r->ifnot)
			r = r->skip[PF_SKIP_IFP].ptr;
		else if (r->direction && r->direction != direction)
			r = r->skip[PF_SKIP_DIR].ptr;
		else if (r->af && r->af != pd->af)
			r = r->skip[PF_SKIP_AF].ptr;
		else if (r->proto && r->proto != pd->proto)
			r = r->skip[PF_SKIP_PROTO].ptr;
		else if (PF_MISMATCHAW(&src->addr, saddr, pd->af,
		    src->neg, kif, M_GETFIB(m)))
			r = r->skip[src == &r->src ? PF_SKIP_SRC_ADDR :
			    PF_SKIP_DST_ADDR].ptr;
		else if (src->port_op && !pf_match_port(src->port_op,
		    src->port[0], src->port[1], sport))
			r = r->skip[src == &r->src ? PF_SKIP_SRC_PORT :
			    PF_SKIP_DST_PORT].ptr;
		else if (dst != NULL &&
		    PF_MISMATCHAW(&dst->addr, daddr, pd->af, dst->neg, NULL,
		    M_GETFIB(m)))
			r = r->skip[PF_SKIP_DST_ADDR].ptr;
		else if (xdst != NULL && PF_MISMATCHAW(xdst, daddr, pd->af,
		    0, NULL, M_GETFIB(m)))
			r = TAILQ_NEXT(r, entries);
		else if (dst != NULL && dst->port_op &&
		    !pf_match_port(dst->port_op, dst->port[0],
		    dst->port[1], dport))
			r = r->skip[PF_SKIP_DST_PORT].ptr;
		else if (r->match_tag && !pf_match_tag(m, r, &tag,
		    pd->pf_mtag ? pd->pf_mtag->tag : 0))
			r = TAILQ_NEXT(r, entries);
		else if (r->os_fingerprint != PF_OSFP_ANY && (pd->proto !=
		    IPPROTO_TCP || !pf_osfp_match(pf_osfp_fingerprint(pd, m,
		    off, pd->hdr.tcp), r->os_fingerprint)))
			r = TAILQ_NEXT(r, entries);
		else {
			if (r->tag)
				tag = r->tag;
			if (r->rtableid >= 0)
				rtableid = r->rtableid;
			if (r->anchor == NULL) {
				rm = r;
			} else
				pf_step_into_anchor(anchor_stack, &asd,
				    &ruleset, rs_num, &r, NULL, NULL);
		}
		if (r == NULL)
			pf_step_out_of_anchor(anchor_stack, &asd, &ruleset,
			    rs_num, &r, NULL, NULL);
	}

	if (tag > 0 && pf_tag_packet(m, pd, tag))
		return (NULL);
	if (rtableid >= 0)
		M_SETFIB(m, rtableid);

	if (rm != NULL && (rm->action == PF_NONAT ||
	    rm->action == PF_NORDR || rm->action == PF_NOBINAT))
		return (NULL);
	return (rm);
}

static int
pf_get_sport(sa_family_t af, u_int8_t proto, struct pf_rule *r,
    struct pf_addr *saddr, uint16_t sport, struct pf_addr *daddr,
    uint16_t dport, struct pf_addr *naddr, uint16_t *nport, uint16_t low,
    uint16_t high, struct pf_src_node **sn)
{
	struct pf_state_key_cmp	key;
	struct pf_addr		init_addr;

	bzero(&init_addr, sizeof(init_addr));
	if (pf_map_addr(af, r, saddr, naddr, &init_addr, sn))
		return (1);

	if (proto == IPPROTO_ICMP) {
		low = 1;
		high = 65535;
	}

	bzero(&key, sizeof(key));
	key.af = af;
	key.proto = proto;
	key.port[0] = dport;
	PF_ACPY(&key.addr[0], daddr, key.af);

	do {
		PF_ACPY(&key.addr[1], naddr, key.af);

		/*
		 * port search; start random, step;
		 * similar 2 portloop in in_pcbbind
		 */
		if (!(proto == IPPROTO_TCP || proto == IPPROTO_UDP ||
		    proto == IPPROTO_ICMP) || (low == 0 && high == 0)) {
			/*
			 * XXX bug: icmp states don't use the id on both sides.
			 * (traceroute -I through nat)
			 */
			key.port[1] = sport;
			if (pf_find_state_all(&key, PF_IN, NULL) == NULL) {
				*nport = sport;
				return (0);
			}
		} else if (low == high) {
			key.port[1] = htons(low);
			if (pf_find_state_all(&key, PF_IN, NULL) == NULL) {
				*nport = htons(low);
				return (0);
			}
		} else {
			uint32_t tmp;
			uint16_t cut;

			if (low > high) {
				tmp = low;
				low = high;
				high = tmp;
			}
			/* low < high */
			cut = arc4random() % (1 + high - low) + low;
			/* low <= cut <= high */
			for (tmp = cut; tmp <= high && tmp <= 0xffff; ++tmp) {
				key.port[1] = htons(tmp);
				if (pf_find_state_all(&key, PF_IN, NULL) ==
				    NULL) {
					*nport = htons(tmp);
					return (0);
				}
			}
			tmp = cut;
			for (tmp -= 1; tmp >= low && tmp <= 0xffff; --tmp) {
				key.port[1] = htons(tmp);
				if (pf_find_state_all(&key, PF_IN, NULL) ==
				    NULL) {
					*nport = htons(tmp);
					return (0);
				}
			}
		}

		switch (r->rpool.opts & PF_POOL_TYPEMASK) {
		case PF_POOL_RANDOM:
		case PF_POOL_ROUNDROBIN:
			/*
			 * pick a different source address since we're out
			 * of free port choices for the current one.
			 */
			if (pf_map_addr(af, r, saddr, naddr, &init_addr, sn))
				return (1);
			break;
		case PF_POOL_NONE:
		case PF_POOL_SRCHASH:
		case PF_POOL_BITMASK:
		default:
			return (1);
		}
	} while (! PF_AEQ(&init_addr, naddr, af) );
	return (1);					/* none available */
}

int
pf_map_addr(sa_family_t af, struct pf_rule *r, struct pf_addr *saddr,
    struct pf_addr *naddr, struct pf_addr *init_addr, struct pf_src_node **sn)
{
	struct pf_pool		*rpool = &r->rpool;
	struct pf_addr		*raddr = NULL, *rmask = NULL;

	/* Try to find a src_node if none was given and this
	   is a sticky-address rule. */
	if (*sn == NULL && r->rpool.opts & PF_POOL_STICKYADDR &&
	    (r->rpool.opts & PF_POOL_TYPEMASK) != PF_POOL_NONE)
		*sn = pf_find_src_node(saddr, r, af, 0);

	/* If a src_node was found or explicitly given and it has a non-zero
	   route address, use this address. A zeroed address is found if the
	   src node was created just a moment ago in pf_create_state and it
	   needs to be filled in with routing decision calculated here. */
	if (*sn != NULL && !PF_AZERO(&(*sn)->raddr, af)) {
		/* If the supplied address is the same as the current one we've
		 * been asked before, so tell the caller that there's no other
		 * address to be had. */
		if (PF_AEQ(naddr, &(*sn)->raddr, af))
			return (1);

		PF_ACPY(naddr, &(*sn)->raddr, af);
		if (V_pf_status.debug >= PF_DEBUG_MISC) {
			printf("pf_map_addr: src tracking maps ");
			pf_print_host(saddr, 0, af);
			printf(" to ");
			pf_print_host(naddr, 0, af);
			printf("\n");
		}
		return (0);
	}

	/* Find the route using chosen algorithm. Store the found route
	   in src_node if it was given or found. */
	if (rpool->cur->addr.type == PF_ADDR_NOROUTE)
		return (1);
	if (rpool->cur->addr.type == PF_ADDR_DYNIFTL) {
		switch (af) {
#ifdef INET
		case AF_INET:
			if (rpool->cur->addr.p.dyn->pfid_acnt4 < 1 &&
			    (rpool->opts & PF_POOL_TYPEMASK) !=
			    PF_POOL_ROUNDROBIN)
				return (1);
			 raddr = &rpool->cur->addr.p.dyn->pfid_addr4;
			 rmask = &rpool->cur->addr.p.dyn->pfid_mask4;
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			if (rpool->cur->addr.p.dyn->pfid_acnt6 < 1 &&
			    (rpool->opts & PF_POOL_TYPEMASK) !=
			    PF_POOL_ROUNDROBIN)
				return (1);
			raddr = &rpool->cur->addr.p.dyn->pfid_addr6;
			rmask = &rpool->cur->addr.p.dyn->pfid_mask6;
			break;
#endif /* INET6 */
		}
	} else if (rpool->cur->addr.type == PF_ADDR_TABLE) {
		if ((rpool->opts & PF_POOL_TYPEMASK) != PF_POOL_ROUNDROBIN)
			return (1); /* unsupported */
	} else {
		raddr = &rpool->cur->addr.v.a.addr;
		rmask = &rpool->cur->addr.v.a.mask;
	}

	switch (rpool->opts & PF_POOL_TYPEMASK) {
	case PF_POOL_NONE:
		PF_ACPY(naddr, raddr, af);
		break;
	case PF_POOL_BITMASK:
		PF_POOLMASK(naddr, raddr, rmask, saddr, af);
		break;
	case PF_POOL_RANDOM:
		if (init_addr != NULL && PF_AZERO(init_addr, af)) {
			switch (af) {
#ifdef INET
			case AF_INET:
				rpool->counter.addr32[0] = htonl(arc4random());
				break;
#endif /* INET */
#ifdef INET6
			case AF_INET6:
				if (rmask->addr32[3] != 0xffffffff)
					rpool->counter.addr32[3] =
					    htonl(arc4random());
				else
					break;
				if (rmask->addr32[2] != 0xffffffff)
					rpool->counter.addr32[2] =
					    htonl(arc4random());
				else
					break;
				if (rmask->addr32[1] != 0xffffffff)
					rpool->counter.addr32[1] =
					    htonl(arc4random());
				else
					break;
				if (rmask->addr32[0] != 0xffffffff)
					rpool->counter.addr32[0] =
					    htonl(arc4random());
				break;
#endif /* INET6 */
			}
			PF_POOLMASK(naddr, raddr, rmask, &rpool->counter, af);
			PF_ACPY(init_addr, naddr, af);

		} else {
			PF_AINC(&rpool->counter, af);
			PF_POOLMASK(naddr, raddr, rmask, &rpool->counter, af);
		}
		break;
	case PF_POOL_SRCHASH:
	    {
		unsigned char hash[16];

		pf_hash(saddr, (struct pf_addr *)&hash, &rpool->key, af);
		PF_POOLMASK(naddr, raddr, rmask, (struct pf_addr *)&hash, af);
		break;
	    }
	case PF_POOL_ROUNDROBIN:
	    {
		struct pf_pooladdr *acur = rpool->cur;

		/*
		 * XXXGL: in the round-robin case we need to store
		 * the round-robin machine state in the rule, thus
		 * forwarding thread needs to modify rule.
		 *
		 * This is done w/o locking, because performance is assumed
		 * more important than round-robin precision.
		 *
		 * In the simpliest case we just update the "rpool->cur"
		 * pointer. However, if pool contains tables or dynamic
		 * addresses, then "tblidx" is also used to store machine
		 * state. Since "tblidx" is int, concurrent access to it can't
		 * lead to inconsistence, only to lost of precision.
		 *
		 * Things get worse, if table contains not hosts, but
		 * prefixes. In this case counter also stores machine state,
		 * and for IPv6 address, counter can't be updated atomically.
		 * Probably, using round-robin on a table containing IPv6
		 * prefixes (or even IPv4) would cause a panic.
		 */

		if (rpool->cur->addr.type == PF_ADDR_TABLE) {
			if (!pfr_pool_get(rpool->cur->addr.p.tbl,
			    &rpool->tblidx, &rpool->counter, af))
				goto get_addr;
		} else if (rpool->cur->addr.type == PF_ADDR_DYNIFTL) {
			if (!pfr_pool_get(rpool->cur->addr.p.dyn->pfid_kt,
			    &rpool->tblidx, &rpool->counter, af))
				goto get_addr;
		} else if (pf_match_addr(0, raddr, rmask, &rpool->counter, af))
			goto get_addr;

	try_next:
		if (TAILQ_NEXT(rpool->cur, entries) == NULL)
			rpool->cur = TAILQ_FIRST(&rpool->list);
		else
			rpool->cur = TAILQ_NEXT(rpool->cur, entries);
		if (rpool->cur->addr.type == PF_ADDR_TABLE) {
			rpool->tblidx = -1;
			if (pfr_pool_get(rpool->cur->addr.p.tbl,
			    &rpool->tblidx, &rpool->counter, af)) {
				/* table contains no address of type 'af' */
				if (rpool->cur != acur)
					goto try_next;
				return (1);
			}
		} else if (rpool->cur->addr.type == PF_ADDR_DYNIFTL) {
			rpool->tblidx = -1;
			if (pfr_pool_get(rpool->cur->addr.p.dyn->pfid_kt,
			    &rpool->tblidx, &rpool->counter, af)) {
				/* table contains no address of type 'af' */
				if (rpool->cur != acur)
					goto try_next;
				return (1);
			}
		} else {
			raddr = &rpool->cur->addr.v.a.addr;
			rmask = &rpool->cur->addr.v.a.mask;
			PF_ACPY(&rpool->counter, raddr, af);
		}

	get_addr:
		PF_ACPY(naddr, &rpool->counter, af);
		if (init_addr != NULL && PF_AZERO(init_addr, af))
			PF_ACPY(init_addr, naddr, af);
		PF_AINC(&rpool->counter, af);
		break;
	    }
	}
	if (*sn != NULL)
		PF_ACPY(&(*sn)->raddr, naddr, af);

	if (V_pf_status.debug >= PF_DEBUG_MISC &&
	    (rpool->opts & PF_POOL_TYPEMASK) != PF_POOL_NONE) {
		printf("pf_map_addr: selected address ");
		pf_print_host(naddr, 0, af);
		printf("\n");
	}

	return (0);
}

struct pf_rule *
pf_get_translation(struct pf_pdesc *pd, struct mbuf *m, int off, int direction,
    struct pfi_kif *kif, struct pf_src_node **sn,
    struct pf_state_key **skp, struct pf_state_key **nkp,
    struct pf_addr *saddr, struct pf_addr *daddr,
    uint16_t sport, uint16_t dport, struct pf_anchor_stackframe *anchor_stack)
{
	struct pf_rule	*r = NULL;
	struct pf_addr	*naddr;
	uint16_t	*nport;

	PF_RULES_RASSERT();
	KASSERT(*skp == NULL, ("*skp not NULL"));
	KASSERT(*nkp == NULL, ("*nkp not NULL"));

	if (direction == PF_OUT) {
		r = pf_match_translation(pd, m, off, direction, kif, saddr,
		    sport, daddr, dport, PF_RULESET_BINAT, anchor_stack);
		if (r == NULL)
			r = pf_match_translation(pd, m, off, direction, kif,
			    saddr, sport, daddr, dport, PF_RULESET_NAT,
			    anchor_stack);
	} else {
		r = pf_match_translation(pd, m, off, direction, kif, saddr,
		    sport, daddr, dport, PF_RULESET_RDR, anchor_stack);
		if (r == NULL)
			r = pf_match_translation(pd, m, off, direction, kif,
			    saddr, sport, daddr, dport, PF_RULESET_BINAT,
			    anchor_stack);
	}

	if (r == NULL)
		return (NULL);

	switch (r->action) {
	case PF_NONAT:
	case PF_NOBINAT:
	case PF_NORDR:
		return (NULL);
	}

	*skp = pf_state_key_setup(pd, saddr, daddr, sport, dport);
	if (*skp == NULL)
		return (NULL);
	*nkp = pf_state_key_clone(*skp);
	if (*nkp == NULL) {
		uma_zfree(V_pf_state_key_z, *skp);
		*skp = NULL;
		return (NULL);
	}

	/* XXX We only modify one side for now. */
	naddr = &(*nkp)->addr[1];
	nport = &(*nkp)->port[1];

	switch (r->action) {
	case PF_NAT:
		if (pf_get_sport(pd->af, pd->proto, r, saddr, sport, daddr,
		    dport, naddr, nport, r->rpool.proxy_port[0],
		    r->rpool.proxy_port[1], sn)) {
			DPFPRINTF(PF_DEBUG_MISC,
			    ("pf: NAT proxy port allocation (%u-%u) failed\n",
			    r->rpool.proxy_port[0], r->rpool.proxy_port[1]));
			goto notrans;
		}
		break;
	case PF_BINAT:
		switch (direction) {
		case PF_OUT:
			if (r->rpool.cur->addr.type == PF_ADDR_DYNIFTL){
				switch (pd->af) {
#ifdef INET
				case AF_INET:
					if (r->rpool.cur->addr.p.dyn->
					    pfid_acnt4 < 1)
						goto notrans;
					PF_POOLMASK(naddr,
					    &r->rpool.cur->addr.p.dyn->
					    pfid_addr4,
					    &r->rpool.cur->addr.p.dyn->
					    pfid_mask4, saddr, AF_INET);
					break;
#endif /* INET */
#ifdef INET6
				case AF_INET6:
					if (r->rpool.cur->addr.p.dyn->
					    pfid_acnt6 < 1)
						goto notrans;
					PF_POOLMASK(naddr,
					    &r->rpool.cur->addr.p.dyn->
					    pfid_addr6,
					    &r->rpool.cur->addr.p.dyn->
					    pfid_mask6, saddr, AF_INET6);
					break;
#endif /* INET6 */
				}
			} else
				PF_POOLMASK(naddr,
				    &r->rpool.cur->addr.v.a.addr,
				    &r->rpool.cur->addr.v.a.mask, saddr,
				    pd->af);
			break;
		case PF_IN:
			if (r->src.addr.type == PF_ADDR_DYNIFTL) {
				switch (pd->af) {
#ifdef INET
				case AF_INET:
					if (r->src.addr.p.dyn-> pfid_acnt4 < 1)
						goto notrans;
					PF_POOLMASK(naddr,
					    &r->src.addr.p.dyn->pfid_addr4,
					    &r->src.addr.p.dyn->pfid_mask4,
					    daddr, AF_INET);
					break;
#endif /* INET */
#ifdef INET6
				case AF_INET6:
					if (r->src.addr.p.dyn->pfid_acnt6 < 1)
						goto notrans;
					PF_POOLMASK(naddr,
					    &r->src.addr.p.dyn->pfid_addr6,
					    &r->src.addr.p.dyn->pfid_mask6,
					    daddr, AF_INET6);
					break;
#endif /* INET6 */
				}
			} else
				PF_POOLMASK(naddr, &r->src.addr.v.a.addr,
				    &r->src.addr.v.a.mask, daddr, pd->af);
			break;
		}
		break;
	case PF_RDR: {
		if (pf_map_addr(pd->af, r, saddr, naddr, NULL, sn))
			goto notrans;
		if ((r->rpool.opts & PF_POOL_TYPEMASK) == PF_POOL_BITMASK)
			PF_POOLMASK(naddr, naddr, &r->rpool.cur->addr.v.a.mask,
			    daddr, pd->af);

		if (r->rpool.proxy_port[1]) {
			uint32_t	tmp_nport;

			tmp_nport = ((ntohs(dport) - ntohs(r->dst.port[0])) %
			    (r->rpool.proxy_port[1] - r->rpool.proxy_port[0] +
			    1)) + r->rpool.proxy_port[0];

			/* Wrap around if necessary. */
			if (tmp_nport > 65535)
				tmp_nport -= 65535;
			*nport = htons((uint16_t)tmp_nport);
		} else if (r->rpool.proxy_port[0])
			*nport = htons(r->rpool.proxy_port[0]);
		break;
	}
	default:
		panic("%s: unknown action %u", __func__, r->action);
	}

	/* Return success only if translation really happened. */
	if (bcmp(*skp, *nkp, sizeof(struct pf_state_key_cmp)))
		return (r);

notrans:
	uma_zfree(V_pf_state_key_z, *nkp);
	uma_zfree(V_pf_state_key_z, *skp);
	*skp = *nkp = NULL;
	*sn = NULL;

	return (NULL);
}

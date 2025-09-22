/*	$OpenBSD: bridgectl.c,v 1.25 2021/02/25 02:48:21 dlg Exp $	*/

/*
 * Copyright (c) 1999, 2000 Jason L. Wright (jason@thought.net)
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
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/timeout.h>
#include <sys/kernel.h>

#include <crypto/siphash.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_bridge.h>


int	bridge_rtfind(struct bridge_softc *, struct ifbaconf *);
int	bridge_rtdaddr(struct bridge_softc *, struct ether_addr *);
u_int32_t bridge_hash(struct bridge_softc *, struct ether_addr *);

int	bridge_brlconf(struct bridge_iflist *, struct ifbrlconf *);
int	bridge_addrule(struct bridge_iflist *, struct ifbrlreq *, int out);

int
bridgectl_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct bridge_softc *sc = (struct bridge_softc *)ifp->if_softc;
	struct ifbreq *req = (struct ifbreq *)data;
	struct ifbrlreq *brlreq = (struct ifbrlreq *)data;
	struct ifbrlconf *bc = (struct ifbrlconf *)data;
	struct ifbareq *bareq = (struct ifbareq *)data;
	struct ifbrparam *bparam = (struct ifbrparam *)data;
	struct bridge_iflist *bif;
	struct ifnet *ifs;
	int error = 0;

	switch (cmd) {
	case SIOCBRDGRTS:
		error = bridge_rtfind(sc, (struct ifbaconf *)data);
		break;
	case SIOCBRDGFLUSH:
		bridge_rtflush(sc, req->ifbr_ifsflags);
		break;
	case SIOCBRDGSADDR:
		ifs = if_unit(bareq->ifba_ifsname);
		if (ifs == NULL) {			/* no such interface */
			error = ENOENT;
			break;
		}
		if (ifs->if_bridgeidx != ifp->if_index) {
			if_put(ifs);
			error = ESRCH;
			break;
		}

		if (bridge_rtupdate(sc, &bareq->ifba_dst, ifs, 1,
		    bareq->ifba_flags, NULL))
			error = ENOMEM;
		if_put(ifs);
		break;
	case SIOCBRDGDADDR:
		error = bridge_rtdaddr(sc, &bareq->ifba_dst);
		break;
	case SIOCBRDGGCACHE:
		bparam->ifbrp_csize = sc->sc_brtmax;
		break;
	case SIOCBRDGSCACHE:
		mtx_enter(&sc->sc_mtx);
		sc->sc_brtmax = bparam->ifbrp_csize;
		mtx_leave(&sc->sc_mtx);
		break;
	case SIOCBRDGSTO:
		if (bparam->ifbrp_ctime < 0 ||
		    bparam->ifbrp_ctime > INT_MAX / hz) {
			error = EINVAL;
			break;
		}
		sc->sc_brttimeout = bparam->ifbrp_ctime;
		if (bparam->ifbrp_ctime != 0)
			timeout_add_sec(&sc->sc_brtimeout, sc->sc_brttimeout);
		else
			timeout_del(&sc->sc_brtimeout);
		break;
	case SIOCBRDGGTO:
		bparam->ifbrp_ctime = sc->sc_brttimeout;
		break;
	case SIOCBRDGARL:
		if ((brlreq->ifbr_action != BRL_ACTION_BLOCK &&
		    brlreq->ifbr_action != BRL_ACTION_PASS) ||
		    (brlreq->ifbr_flags & (BRL_FLAG_IN|BRL_FLAG_OUT)) == 0) {
			error = EINVAL;
			break;
		}
		error = bridge_findbif(sc, brlreq->ifbr_ifsname, &bif);
		if (error != 0)
			break;
		if (brlreq->ifbr_flags & BRL_FLAG_IN) {
			error = bridge_addrule(bif, brlreq, 0);
			if (error)
				break;
		}
		if (brlreq->ifbr_flags & BRL_FLAG_OUT) {
			error = bridge_addrule(bif, brlreq, 1);
			if (error)
				break;
		}
		break;
	case SIOCBRDGFRL:
		error = bridge_findbif(sc, brlreq->ifbr_ifsname, &bif);
		if (error != 0)
			break;
		bridge_flushrule(bif);
		break;
	case SIOCBRDGGRL:
		error = bridge_findbif(sc, bc->ifbrl_ifsname, &bif);
		if (error != 0)
			break;
		error = bridge_brlconf(bif, bc);
		break;
	default:
		break;
	}

	return (error);
}

int
bridge_rtupdate(struct bridge_softc *sc, struct ether_addr *ea,
    struct ifnet *ifp, int setflags, u_int8_t flags, struct mbuf *m)
{
	struct bridge_rtnode *p, *q;
	struct bridge_tunneltag	*brtag = NULL;
	u_int32_t h;
	int dir, error = 0;

	if (m != NULL) {
		/* Check if the mbuf was tagged with a tunnel endpoint addr */
		brtag = bridge_tunnel(m);
	}

	h = bridge_hash(sc, ea);
	mtx_enter(&sc->sc_mtx);
	p = LIST_FIRST(&sc->sc_rts[h]);
	if (p == NULL) {
		if (sc->sc_brtcnt >= sc->sc_brtmax)
			goto done;
		p = malloc(sizeof(*p), M_DEVBUF, M_NOWAIT);
		if (p == NULL)
			goto done;

		bcopy(ea, &p->brt_addr, sizeof(p->brt_addr));
		p->brt_ifidx = ifp->if_index;
		p->brt_age = 1;
		bridge_copytag(brtag, &p->brt_tunnel);

		if (setflags)
			p->brt_flags = flags;
		else
			p->brt_flags = IFBAF_DYNAMIC;

		LIST_INSERT_HEAD(&sc->sc_rts[h], p, brt_next);
		sc->sc_brtcnt++;
		goto want;
	}

	do {
		q = p;
		p = LIST_NEXT(p, brt_next);

		dir = memcmp(ea, &q->brt_addr, sizeof(q->brt_addr));
		if (dir == 0) {
			if (setflags) {
				q->brt_ifidx = ifp->if_index;
				q->brt_flags = flags;
			} else if (!(q->brt_flags & IFBAF_STATIC))
				q->brt_ifidx = ifp->if_index;

			if (q->brt_ifidx == ifp->if_index)
				q->brt_age = 1;
			bridge_copytag(brtag, &q->brt_tunnel);
			goto want;
		}

		if (dir > 0) {
			if (sc->sc_brtcnt >= sc->sc_brtmax)
				goto done;
			p = malloc(sizeof(*p), M_DEVBUF, M_NOWAIT);
			if (p == NULL)
				goto done;

			bcopy(ea, &p->brt_addr, sizeof(p->brt_addr));
			p->brt_ifidx = ifp->if_index;
			p->brt_age = 1;
			bridge_copytag(brtag, &p->brt_tunnel);

			if (setflags)
				p->brt_flags = flags;
			else
				p->brt_flags = IFBAF_DYNAMIC;

			LIST_INSERT_BEFORE(q, p, brt_next);
			sc->sc_brtcnt++;
			goto want;
		}

		if (p == NULL) {
			if (sc->sc_brtcnt >= sc->sc_brtmax)
				goto done;
			p = malloc(sizeof(*p), M_DEVBUF, M_NOWAIT);
			if (p == NULL)
				goto done;

			bcopy(ea, &p->brt_addr, sizeof(p->brt_addr));
			p->brt_ifidx = ifp->if_index;
			p->brt_age = 1;
			bridge_copytag(brtag, &p->brt_tunnel);

			if (setflags)
				p->brt_flags = flags;
			else
				p->brt_flags = IFBAF_DYNAMIC;
			LIST_INSERT_AFTER(q, p, brt_next);
			sc->sc_brtcnt++;
			goto want;
		}
	} while (p != NULL);

done:
	error = 1;
want:
	mtx_leave(&sc->sc_mtx);
	return (error);
}

unsigned int
bridge_rtlookup(struct ifnet *brifp, struct ether_addr *ea, struct mbuf *m)
{
	struct bridge_softc *sc = brifp->if_softc;
	struct bridge_rtnode *p = NULL;
	unsigned int ifidx = 0;
	u_int32_t h;
	int dir;

	h = bridge_hash(sc, ea);
	mtx_enter(&sc->sc_mtx);
	LIST_FOREACH(p, &sc->sc_rts[h], brt_next) {
		dir = memcmp(ea, &p->brt_addr, sizeof(p->brt_addr));
		if (dir == 0)
			break;
		if (dir > 0) {
			p = NULL;
			break;
		}
	}
	if (p != NULL) {
		ifidx = p->brt_ifidx;

		if (p->brt_family != AF_UNSPEC && m != NULL) {
			struct bridge_tunneltag *brtag;

			brtag = bridge_tunneltag(m);
			if (brtag != NULL)
				bridge_copytag(&p->brt_tunnel, brtag);
		}
	}
	mtx_leave(&sc->sc_mtx);

	return (ifidx);
}

u_int32_t
bridge_hash(struct bridge_softc *sc, struct ether_addr *addr)
{
	return SipHash24((SIPHASH_KEY *)sc->sc_hashkey, addr, ETHER_ADDR_LEN) &
	    BRIDGE_RTABLE_MASK;
}

/*
 * Perform an aging cycle
 */
void
bridge_rtage(void *vsc)
{
	struct bridge_softc *sc = vsc;
	struct ifnet *ifp = &sc->sc_if;
	struct bridge_rtnode *n, *p;
	int i;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return;

	mtx_enter(&sc->sc_mtx);
	for (i = 0; i < BRIDGE_RTABLE_SIZE; i++) {
		n = LIST_FIRST(&sc->sc_rts[i]);
		while (n != NULL) {
			if ((n->brt_flags & IFBAF_TYPEMASK) == IFBAF_STATIC) {
				n->brt_age = !n->brt_age;
				if (n->brt_age)
					n->brt_age = 0;
				n = LIST_NEXT(n, brt_next);
			} else if (n->brt_age) {
				n->brt_age = 0;
				n = LIST_NEXT(n, brt_next);
			} else {
				p = LIST_NEXT(n, brt_next);
				LIST_REMOVE(n, brt_next);
				sc->sc_brtcnt--;
				free(n, M_DEVBUF, sizeof *n);
				n = p;
			}
		}
	}
	mtx_leave(&sc->sc_mtx);

	if (sc->sc_brttimeout != 0)
		timeout_add_sec(&sc->sc_brtimeout, sc->sc_brttimeout);
}

void
bridge_rtagenode(struct ifnet *ifp, int age)
{
	struct bridge_softc *sc;
	struct bridge_rtnode *n;
	struct ifnet *bifp;
	int i;

	bifp = if_get(ifp->if_bridgeidx);
	if (bifp == NULL)
		return;
	sc = bifp->if_softc;

	/*
	 * If the age is zero then flush, otherwise set all the expiry times to
	 * age for the interface
	 */
	if (age == 0)
		bridge_rtdelete(sc, ifp, 1);
	else {
		mtx_enter(&sc->sc_mtx);
		for (i = 0; i < BRIDGE_RTABLE_SIZE; i++) {
			LIST_FOREACH(n, &sc->sc_rts[i], brt_next) {
				/* Cap the expiry time to 'age' */
				if (n->brt_ifidx == ifp->if_index &&
				    n->brt_age > getuptime() + age &&
				    (n->brt_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC)
					n->brt_age = getuptime() + age;
			}
		}
		mtx_leave(&sc->sc_mtx);
	}

	if_put(bifp);
}

/*
 * Remove all dynamic addresses from the cache
 */
void
bridge_rtflush(struct bridge_softc *sc, int full)
{
	int i;
	struct bridge_rtnode *p, *n;

	mtx_enter(&sc->sc_mtx);
	for (i = 0; i < BRIDGE_RTABLE_SIZE; i++) {
		n = LIST_FIRST(&sc->sc_rts[i]);
		while (n != NULL) {
			if (full ||
			    (n->brt_flags & IFBAF_TYPEMASK) == IFBAF_DYNAMIC) {
				p = LIST_NEXT(n, brt_next);
				LIST_REMOVE(n, brt_next);
				sc->sc_brtcnt--;
				free(n, M_DEVBUF, sizeof *n);
				n = p;
			} else
				n = LIST_NEXT(n, brt_next);
		}
	}
	mtx_leave(&sc->sc_mtx);
}

/*
 * Remove an address from the cache
 */
int
bridge_rtdaddr(struct bridge_softc *sc, struct ether_addr *ea)
{
	int h;
	struct bridge_rtnode *p;

	h = bridge_hash(sc, ea);
	mtx_enter(&sc->sc_mtx);
	LIST_FOREACH(p, &sc->sc_rts[h], brt_next) {
		if (memcmp(ea, &p->brt_addr, sizeof(p->brt_addr)) == 0) {
			LIST_REMOVE(p, brt_next);
			sc->sc_brtcnt--;
			mtx_leave(&sc->sc_mtx);
			free(p, M_DEVBUF, sizeof *p);
			return (0);
		}
	}
	mtx_leave(&sc->sc_mtx);

	return (ENOENT);
}

/*
 * Delete routes to a specific interface member.
 */
void
bridge_rtdelete(struct bridge_softc *sc, struct ifnet *ifp, int dynonly)
{
	int i;
	struct bridge_rtnode *n, *p;

	/*
	 * Loop through all of the hash buckets and traverse each
	 * chain looking for routes to this interface.
	 */
	mtx_enter(&sc->sc_mtx);
	for (i = 0; i < BRIDGE_RTABLE_SIZE; i++) {
		n = LIST_FIRST(&sc->sc_rts[i]);
		while (n != NULL) {
			if (n->brt_ifidx != ifp->if_index) {
				/* Not ours */
				n = LIST_NEXT(n, brt_next);
				continue;
			}
			if (dynonly &&
			    (n->brt_flags & IFBAF_TYPEMASK) != IFBAF_DYNAMIC) {
				/* only deleting dynamics */
				n = LIST_NEXT(n, brt_next);
				continue;
			}
			p = LIST_NEXT(n, brt_next);
			LIST_REMOVE(n, brt_next);
			sc->sc_brtcnt--;
			free(n, M_DEVBUF, sizeof *n);
			n = p;
		}
	}
	mtx_leave(&sc->sc_mtx);
}

/*
 * Gather all of the routes for this interface.
 */
int
bridge_rtfind(struct bridge_softc *sc, struct ifbaconf *baconf)
{
	struct ifbareq *bareq, *bareqs = NULL;
	struct bridge_rtnode *n;
	u_int32_t i = 0, total = 0;
	int k, error = 0;

	mtx_enter(&sc->sc_mtx);
	for (k = 0; k < BRIDGE_RTABLE_SIZE; k++) {
		LIST_FOREACH(n, &sc->sc_rts[k], brt_next)
			total++;
	}
	mtx_leave(&sc->sc_mtx);

	if (baconf->ifbac_len == 0) {
		i = total;
		goto done;
	}

	total = MIN(total, baconf->ifbac_len / sizeof(*bareqs));
	bareqs = mallocarray(total, sizeof(*bareqs), M_TEMP, M_NOWAIT|M_ZERO);
	if (bareqs == NULL)
		goto done;

	mtx_enter(&sc->sc_mtx);
	for (k = 0; k < BRIDGE_RTABLE_SIZE; k++) {
		LIST_FOREACH(n, &sc->sc_rts[k], brt_next) {
			struct ifnet *ifp;

			if (i >= total) {
				mtx_leave(&sc->sc_mtx);
				goto done;
			}
			bareq = &bareqs[i];

			ifp = if_get(n->brt_ifidx);
			if (ifp == NULL)
				continue;
			bcopy(ifp->if_xname, bareq->ifba_ifsname,
			    sizeof(bareq->ifba_ifsname));
			if_put(ifp);

			bcopy(sc->sc_if.if_xname, bareq->ifba_name,
			    sizeof(bareq->ifba_name));
			bcopy(&n->brt_addr, &bareq->ifba_dst,
			    sizeof(bareq->ifba_dst));
			bridge_copyaddr(&n->brt_tunnel.brtag_peer.sa,
			    sstosa(&bareq->ifba_dstsa));
			bareq->ifba_age = n->brt_age;
			bareq->ifba_flags = n->brt_flags;
			i++;
		}
	}
	mtx_leave(&sc->sc_mtx);

	error = copyout(bareqs, baconf->ifbac_req, i * sizeof(*bareqs));
done:
	free(bareqs, M_TEMP, total * sizeof(*bareqs));
	baconf->ifbac_len = i * sizeof(*bareqs);
	return (error);
}

void
bridge_update(struct ifnet *ifp, struct ether_addr *ea, int delete)
{
	struct bridge_softc *sc;
	struct bridge_iflist *bif;
	u_int8_t *addr;

	addr = (u_int8_t *)ea;

	bif = bridge_getbif(ifp);
	if (bif == NULL)
		return;
	sc = bif->bridge_sc;
	if (sc == NULL)
		return;

	/*
	 * Update the bridge interface if it is in
	 * the learning state.
	 */
	if ((bif->bif_flags & IFBIF_LEARNING) &&
	    (ETHER_IS_MULTICAST(addr) == 0) &&
	    !(addr[0] == 0 && addr[1] == 0 && addr[2] == 0 &&
	      addr[3] == 0 && addr[4] == 0 && addr[5] == 0)) {
		/* Care must be taken with spanning tree */
		if ((bif->bif_flags & IFBIF_STP) &&
		    (bif->bif_state == BSTP_IFSTATE_DISCARDING))
			return;

		/* Delete the address from the bridge */
		bridge_rtdaddr(sc, ea);

		if (!delete) {
			/* Update the bridge table */
			bridge_rtupdate(sc, ea, ifp, 0, IFBAF_DYNAMIC, NULL);
		}
	}
}

/*
 * bridge filter/matching rules
 */
int
bridge_brlconf(struct bridge_iflist *bif, struct ifbrlconf *bc)
{
	struct bridge_softc *sc = bif->bridge_sc;
	struct brl_node *n;
	struct ifbrlreq *req, *reqs = NULL;
	int error = 0;
	u_int32_t i = 0, total = 0;

	SIMPLEQ_FOREACH(n, &bif->bif_brlin, brl_next) {
		total++;
	}
	SIMPLEQ_FOREACH(n, &bif->bif_brlout, brl_next) {
		total++;
	}

	if (bc->ifbrl_len == 0) {
		i = total;
		goto done;
	}

	reqs = mallocarray(total, sizeof(*reqs), M_TEMP, M_NOWAIT|M_ZERO);
	if (reqs == NULL)
		goto done;

	SIMPLEQ_FOREACH(n, &bif->bif_brlin, brl_next) {
		if (bc->ifbrl_len < (i + 1) * sizeof(*reqs))
			goto done;
		req = &reqs[i];
		strlcpy(req->ifbr_name, sc->sc_if.if_xname, IFNAMSIZ);
		strlcpy(req->ifbr_ifsname, bif->ifp->if_xname, IFNAMSIZ);
		req->ifbr_action = n->brl_action;
		req->ifbr_flags = n->brl_flags;
		req->ifbr_src = n->brl_src;
		req->ifbr_dst = n->brl_dst;
		req->ifbr_arpf = n->brl_arpf;
#if NPF > 0
		req->ifbr_tagname[0] = '\0';
		if (n->brl_tag)
			pf_tag2tagname(n->brl_tag, req->ifbr_tagname);
#endif
		i++;
	}

	SIMPLEQ_FOREACH(n, &bif->bif_brlout, brl_next) {
		if (bc->ifbrl_len < (i + 1) * sizeof(*reqs))
			goto done;
		req = &reqs[i];
		strlcpy(req->ifbr_name, sc->sc_if.if_xname, IFNAMSIZ);
		strlcpy(req->ifbr_ifsname, bif->ifp->if_xname, IFNAMSIZ);
		req->ifbr_action = n->brl_action;
		req->ifbr_flags = n->brl_flags;
		req->ifbr_src = n->brl_src;
		req->ifbr_dst = n->brl_dst;
		req->ifbr_arpf = n->brl_arpf;
#if NPF > 0
		req->ifbr_tagname[0] = '\0';
		if (n->brl_tag)
			pf_tag2tagname(n->brl_tag, req->ifbr_tagname);
#endif
		i++;
	}

	error = copyout(reqs, bc->ifbrl_buf, i * sizeof(*reqs));
done:
	free(reqs, M_TEMP, total * sizeof(*reqs));
	bc->ifbrl_len = i * sizeof(*reqs);
	return (error);
}

u_int8_t
bridge_arpfilter(struct brl_node *n, struct ether_header *eh, struct mbuf *m)
{
	struct ether_arp	 ea;

	if (!(n->brl_arpf.brla_flags & (BRLA_ARP|BRLA_RARP)))
		return (1);

	if (ntohs(eh->ether_type) != ETHERTYPE_ARP)
		return (0);
	if (m->m_pkthdr.len < ETHER_HDR_LEN + sizeof(ea))
		return (0);	/* log error? */
	m_copydata(m, ETHER_HDR_LEN, sizeof(ea), &ea);

	if (ntohs(ea.arp_hrd) != ARPHRD_ETHER ||
	    ntohs(ea.arp_pro) != ETHERTYPE_IP ||
	    ea.arp_hln != ETHER_ADDR_LEN ||
	    ea.arp_pln != sizeof(struct in_addr))
		return (0);
	if ((n->brl_arpf.brla_flags & BRLA_ARP) &&
	    ntohs(ea.arp_op) != ARPOP_REQUEST &&
	    ntohs(ea.arp_op) != ARPOP_REPLY)
		return (0);
	if ((n->brl_arpf.brla_flags & BRLA_RARP) &&
	    ntohs(ea.arp_op) != ARPOP_REVREQUEST &&
	    ntohs(ea.arp_op) != ARPOP_REVREPLY)
		return (0);
	if (n->brl_arpf.brla_op && ntohs(ea.arp_op) != n->brl_arpf.brla_op)
		return (0);
	if (n->brl_arpf.brla_flags & BRLA_SHA &&
	    memcmp(ea.arp_sha, &n->brl_arpf.brla_sha, ETHER_ADDR_LEN))
		return (0);
	if (n->brl_arpf.brla_flags & BRLA_THA &&
	    memcmp(ea.arp_tha, &n->brl_arpf.brla_tha, ETHER_ADDR_LEN))
		return (0);
	if (n->brl_arpf.brla_flags & BRLA_SPA &&
	    memcmp(ea.arp_spa, &n->brl_arpf.brla_spa, sizeof(struct in_addr)))
		return (0);
	if (n->brl_arpf.brla_flags & BRLA_TPA &&
	    memcmp(ea.arp_tpa, &n->brl_arpf.brla_tpa, sizeof(struct in_addr)))
		return (0);

	return (1);
}

u_int8_t
bridge_filterrule(struct brl_head *h, struct ether_header *eh, struct mbuf *m)
{
	struct brl_node *n;
	u_int8_t action, flags;

	if (SIMPLEQ_EMPTY(h))
		return (BRL_ACTION_PASS);

	KERNEL_LOCK();
	SIMPLEQ_FOREACH(n, h, brl_next) {
		if (!bridge_arpfilter(n, eh, m))
			continue;
		flags = n->brl_flags & (BRL_FLAG_SRCVALID|BRL_FLAG_DSTVALID);
		if (flags == 0)
			goto return_action;
		if (flags == (BRL_FLAG_SRCVALID|BRL_FLAG_DSTVALID)) {
			if (memcmp(eh->ether_shost, &n->brl_src,
			    ETHER_ADDR_LEN))
				continue;
			if (memcmp(eh->ether_dhost, &n->brl_dst,
			    ETHER_ADDR_LEN))
				continue;
			goto return_action;
		}
		if (flags == BRL_FLAG_SRCVALID) {
			if (memcmp(eh->ether_shost, &n->brl_src,
			    ETHER_ADDR_LEN))
				continue;
			goto return_action;
		}
		if (flags == BRL_FLAG_DSTVALID) {
			if (memcmp(eh->ether_dhost, &n->brl_dst,
			    ETHER_ADDR_LEN))
				continue;
			goto return_action;
		}
	}
	KERNEL_UNLOCK();
	return (BRL_ACTION_PASS);

return_action:
#if NPF > 0
	pf_tag_packet(m, n->brl_tag, -1);
#endif
	action = n->brl_action;
	KERNEL_UNLOCK();
	return (action);
}

int
bridge_addrule(struct bridge_iflist *bif, struct ifbrlreq *req, int out)
{
	struct brl_node *n;

	n = malloc(sizeof(*n), M_DEVBUF, M_NOWAIT);
	if (n == NULL)
		return (ENOMEM);
	bcopy(&req->ifbr_src, &n->brl_src, sizeof(struct ether_addr));
	bcopy(&req->ifbr_dst, &n->brl_dst, sizeof(struct ether_addr));
	n->brl_action = req->ifbr_action;
	n->brl_flags = req->ifbr_flags;
	n->brl_arpf = req->ifbr_arpf;
#if NPF > 0
	if (req->ifbr_tagname[0])
		n->brl_tag = pf_tagname2tag(req->ifbr_tagname, 1);
	else
		n->brl_tag = 0;
#endif

	KERNEL_ASSERT_LOCKED();

	if (out) {
		n->brl_flags &= ~BRL_FLAG_IN;
		n->brl_flags |= BRL_FLAG_OUT;
		SIMPLEQ_INSERT_TAIL(&bif->bif_brlout, n, brl_next);
	} else {
		n->brl_flags &= ~BRL_FLAG_OUT;
		n->brl_flags |= BRL_FLAG_IN;
		SIMPLEQ_INSERT_TAIL(&bif->bif_brlin, n, brl_next);
	}
	return (0);
}

void
bridge_flushrule(struct bridge_iflist *bif)
{
	struct brl_node *p;

	KERNEL_ASSERT_LOCKED();

	while (!SIMPLEQ_EMPTY(&bif->bif_brlin)) {
		p = SIMPLEQ_FIRST(&bif->bif_brlin);
		SIMPLEQ_REMOVE_HEAD(&bif->bif_brlin, brl_next);
#if NPF > 0
		pf_tag_unref(p->brl_tag);
#endif
		free(p, M_DEVBUF, sizeof *p);
	}
	while (!SIMPLEQ_EMPTY(&bif->bif_brlout)) {
		p = SIMPLEQ_FIRST(&bif->bif_brlout);
		SIMPLEQ_REMOVE_HEAD(&bif->bif_brlout, brl_next);
#if NPF > 0
		pf_tag_unref(p->brl_tag);
#endif
		free(p, M_DEVBUF, sizeof *p);
	}
}

struct bridge_tunneltag *
bridge_tunnel(struct mbuf *m)
{
	struct m_tag    *mtag;

	if ((mtag = m_tag_find(m, PACKET_TAG_TUNNEL, NULL)) == NULL)
		return (NULL);

	return ((struct bridge_tunneltag *)(mtag + 1));
}

struct bridge_tunneltag *
bridge_tunneltag(struct mbuf *m)
{
	struct m_tag	*mtag;

	if ((mtag = m_tag_find(m, PACKET_TAG_TUNNEL, NULL)) == NULL) {
		mtag = m_tag_get(PACKET_TAG_TUNNEL,
		    sizeof(struct bridge_tunneltag), M_NOWAIT);
		if (mtag == NULL)
			return (NULL);
		bzero(mtag + 1, sizeof(struct bridge_tunneltag));
		m_tag_prepend(m, mtag);
	}

	return ((struct bridge_tunneltag *)(mtag + 1));
}

void
bridge_tunneluntag(struct mbuf *m)
{
	struct m_tag    *mtag;
	if ((mtag = m_tag_find(m, PACKET_TAG_TUNNEL, NULL)) != NULL)
		m_tag_delete(m, mtag);
}

void
bridge_copyaddr(struct sockaddr *src, struct sockaddr *dst)
{
	if (src != NULL && src->sa_family != AF_UNSPEC)
		memcpy(dst, src, src->sa_len);
	else {
		dst->sa_family = AF_UNSPEC;
		dst->sa_len = 0;
	}
}

void
bridge_copytag(struct bridge_tunneltag *src, struct bridge_tunneltag *dst)
{
	if (src == NULL) {
		memset(dst, 0, sizeof(*dst));
	} else {
		bridge_copyaddr(&src->brtag_peer.sa, &dst->brtag_peer.sa);
		bridge_copyaddr(&src->brtag_local.sa, &dst->brtag_local.sa);
		dst->brtag_id = src->brtag_id;
	}
}

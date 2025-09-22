/*	$OpenBSD: trunklacp.c,v 1.33 2025/07/07 02:28:50 jsg Exp $ */
/*	$NetBSD: ieee8023ad_lacp.c,v 1.3 2005/12/11 12:24:54 christos Exp $ */
/*	$FreeBSD:ieee8023ad_lacp.c,v 1.15 2008/03/16 19:25:30 thompsa Exp $ */

/*
 * Copyright (c)2005 YAMAMOTO Takashi,
 * Copyright (c)2008 Andrew Thompson <thompsa@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/task.h>
#include <sys/timeout.h>

#include <crypto/siphash.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_trunk.h>
#include <net/trunklacp.h>

const u_int8_t ethermulticastaddr_slowprotocols[ETHER_ADDR_LEN] =
    { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x02 };

const struct tlv_template lacp_info_tlv_template[] = {
	{ LACP_TYPE_ACTORINFO,
	    sizeof(struct tlvhdr) + sizeof(struct lacp_peerinfo) },
	{ LACP_TYPE_PARTNERINFO,
	    sizeof(struct tlvhdr) + sizeof(struct lacp_peerinfo) },
	{ LACP_TYPE_COLLECTORINFO,
	    sizeof(struct tlvhdr) + sizeof(struct lacp_collectorinfo) },
	{ 0, 0 },
};

const struct tlv_template marker_info_tlv_template[] = {
	{ MARKER_TYPE_INFO,
	    sizeof(struct tlvhdr) + sizeof(struct lacp_markerinfo) },
	{ 0, 0 },
};

const struct tlv_template marker_response_tlv_template[] = {
	{ MARKER_TYPE_RESPONSE,
	    sizeof(struct tlvhdr) + sizeof(struct lacp_markerinfo) },
	{ 0, 0 },
};

typedef void (*lacp_timer_func_t)(struct lacp_port *);

void		lacp_default_partner(struct lacp_softc *,
		    struct lacp_peerinfo *);
void		lacp_fill_actorinfo(struct lacp_port *, struct lacp_peerinfo *);
void		lacp_fill_markerinfo(struct lacp_port *,
		    struct lacp_markerinfo *);

u_int64_t	lacp_aggregator_bandwidth(struct lacp_aggregator *);
void		lacp_suppress_distributing(struct lacp_softc *,
		    struct lacp_aggregator *);
void		lacp_transit_expire(void *);
void		lacp_update_portmap(struct lacp_softc *);
void		lacp_select_active_aggregator(struct lacp_softc *);
u_int16_t	lacp_compose_key(struct lacp_port *);
int		tlv_check(const void *, size_t, const struct tlvhdr *,
		    const struct tlv_template *, int);
void		lacp_tick(void *);

void		lacp_fill_aggregator_id(struct lacp_aggregator *,
		    const struct lacp_port *);
void		lacp_fill_aggregator_id_peer(struct lacp_peerinfo *,
		    const struct lacp_peerinfo *);
int		lacp_aggregator_is_compatible(const struct lacp_aggregator *,
		    const struct lacp_port *);
int		lacp_peerinfo_is_compatible(const struct lacp_peerinfo *,
		    const struct lacp_peerinfo *);

struct lacp_aggregator *lacp_aggregator_get(struct lacp_softc *,
		    struct lacp_port *);
void		lacp_aggregator_addref(struct lacp_softc *,
		    struct lacp_aggregator *);
void		lacp_aggregator_delref(struct lacp_softc *,
		    struct lacp_aggregator *);

/* receive machine */

void		lacp_input_process(void *);
int		lacp_pdu_input(struct lacp_port *, struct mbuf *);
int		lacp_marker_input(struct lacp_port *, struct mbuf *);
void		lacp_sm_rx(struct lacp_port *, const struct lacpdu *);
void		lacp_sm_rx_timer(struct lacp_port *);
void		lacp_sm_rx_set_expired(struct lacp_port *);
void		lacp_sm_rx_update_ntt(struct lacp_port *,
		    const struct lacpdu *);
void		lacp_sm_rx_record_pdu(struct lacp_port *,
		    const struct lacpdu *);
void		lacp_sm_rx_update_selected(struct lacp_port *,
		    const struct lacpdu *);
void		lacp_sm_rx_record_default(struct lacp_port *);
void		lacp_sm_rx_update_default_selected(struct lacp_port *);
void		lacp_sm_rx_update_selected_from_peerinfo(struct lacp_port *,
		    const struct lacp_peerinfo *);

/* mux machine */

void		lacp_sm_mux(struct lacp_port *);
void		lacp_set_mux(struct lacp_port *, enum lacp_mux_state);
void		lacp_sm_mux_timer(struct lacp_port *);

/* periodic transmit machine */

void		lacp_sm_ptx_update_timeout(struct lacp_port *, u_int8_t);
void		lacp_sm_ptx_tx_schedule(struct lacp_port *);
void		lacp_sm_ptx_timer(struct lacp_port *);

/* transmit machine */

void		lacp_sm_tx(struct lacp_port *);
void		lacp_sm_assert_ntt(struct lacp_port *);

void		lacp_run_timers(struct lacp_port *);
int		lacp_compare_peerinfo(const struct lacp_peerinfo *,
		    const struct lacp_peerinfo *);
int		lacp_compare_systemid(const struct lacp_systemid *,
		    const struct lacp_systemid *);
void		lacp_port_enable(struct lacp_port *);
void		lacp_port_disable(struct lacp_port *);
void		lacp_select(struct lacp_port *);
void		lacp_unselect(struct lacp_port *);
void		lacp_disable_collecting(struct lacp_port *);
void		lacp_enable_collecting(struct lacp_port *);
void		lacp_disable_distributing(struct lacp_port *);
void		lacp_enable_distributing(struct lacp_port *);
int		lacp_xmit_lacpdu(struct lacp_port *);
int		lacp_xmit_marker(struct lacp_port *);

#if defined(LACP_DEBUG)
void		lacp_dump_lacpdu(const struct lacpdu *);
const char	*lacp_format_partner(const struct lacp_peerinfo *, char *,
		    size_t);
const char	*lacp_format_lagid(const struct lacp_peerinfo *,
		    const struct lacp_peerinfo *, char *, size_t);
const char	*lacp_format_lagid_aggregator(const struct lacp_aggregator *,
		    char *, size_t);
const char	*lacp_format_state(u_int8_t, char *, size_t);
const char	*lacp_format_mac(const u_int8_t *, char *, size_t);
const char	*lacp_format_systemid(const struct lacp_systemid *, char *,
		    size_t);
const char	*lacp_format_portid(const struct lacp_portid *, char *,
		    size_t);
void		lacp_dprintf(const struct lacp_port *, const char *, ...)
		    __attribute__((__format__(__printf__, 2, 3)));
#define	LACP_DPRINTF(a)	lacp_dprintf a
#else
#define LACP_DPRINTF(a) /* nothing */
#endif

const lacp_timer_func_t lacp_timer_funcs[LACP_NTIMER] = {
	[LACP_TIMER_CURRENT_WHILE] = lacp_sm_rx_timer,
	[LACP_TIMER_PERIODIC] = lacp_sm_ptx_timer,
	[LACP_TIMER_WAIT_WHILE] = lacp_sm_mux_timer,
};

void
lacp_default_partner(struct lacp_softc *lsc, struct lacp_peerinfo *peer)
{
	peer->lip_systemid.lsi_prio = lsc->lsc_sys_prio;
	peer->lip_key = 0;
	peer->lip_portid.lpi_prio = lsc->lsc_port_prio;
	peer->lip_state = LACP_STATE_SYNC | LACP_STATE_AGGREGATION |
	    LACP_STATE_COLLECTING | LACP_STATE_DISTRIBUTING;
}

int
lacp_input(struct trunk_port *tp, struct mbuf *m)
{
	struct lacp_port *lp = LACP_PORT(tp);
	struct lacp_softc *lsc = lp->lp_lsc;
	struct lacp_aggregator *la = lp->lp_aggregator;
	struct ether_header *eh;
	u_int8_t subtype;

	eh = mtod(m, struct ether_header *);

	if (ntohs(eh->ether_type) == ETHERTYPE_SLOW) {
		if (m->m_pkthdr.len < (sizeof(*eh) + sizeof(subtype)))
			return (-1);

		m_copydata(m, sizeof(*eh), sizeof(subtype), &subtype);
		switch (subtype) {
		case SLOWPROTOCOLS_SUBTYPE_LACP:
		case SLOWPROTOCOLS_SUBTYPE_MARKER:
			mq_enqueue(&lp->lp_mq, m);
			task_add(systq, &lsc->lsc_input);
			return (1);
		}
	}

	/*
	 * If the port is not collecting or not in the active aggregator then
	 * free and return.
	 */
	/* This port is joined to the active aggregator */
	if ((lp->lp_state & LACP_STATE_COLLECTING) == 0 ||
	    la == NULL || la != lsc->lsc_active_aggregator) {
		m_freem(m);
		return (-1);
	}

	/* Not a subtype we are interested in */
	return (0);
}

void
lacp_input_process(void *arg)
{
	struct lacp_softc *lsc = arg;
	struct lacp_port *lp;
	struct mbuf *m;
	u_int8_t subtype;

	LIST_FOREACH(lp, &lsc->lsc_ports, lp_next) {
		while ((m = mq_dequeue(&lp->lp_mq)) != NULL) {
			m_copydata(m, sizeof(struct ether_header),
			    sizeof(subtype), &subtype);

			switch (subtype) {
			case SLOWPROTOCOLS_SUBTYPE_LACP:
				lacp_pdu_input(lp, m);
				break;

			case SLOWPROTOCOLS_SUBTYPE_MARKER:
				lacp_marker_input(lp, m);
				break;
			}
		}
	}
}

/*
 * lacp_pdu_input: process lacpdu
 */
int
lacp_pdu_input(struct lacp_port *lp, struct mbuf *m)
{
	struct lacpdu *du;
	int error = 0;

	if (m->m_pkthdr.len != sizeof(*du))
		goto bad;

	if (m->m_len < sizeof(*du)) {
		m = m_pullup(m, sizeof(*du));
		if (m == NULL)
			return (ENOMEM);
	}
	du = mtod(m, struct lacpdu *);

	if (memcmp(&du->ldu_eh.ether_dhost,
	    &ethermulticastaddr_slowprotocols, ETHER_ADDR_LEN))
		goto bad;

	/*
	 * ignore the version for compatibility with
	 * the future protocol revisions.
	 */
#if 0
	if (du->ldu_sph.sph_version != 1)
		goto bad;
#endif

	/*
	 * ignore tlv types for compatibility with the
	 * future protocol revisions. (IEEE 802.3-2005 43.4.12)
	 */
	if (tlv_check(du, sizeof(*du), &du->ldu_tlv_actor,
	    lacp_info_tlv_template, 0))
		goto bad;

#if defined(LACP_DEBUG)
	LACP_DPRINTF((lp, "lacpdu receive\n"));
	lacp_dump_lacpdu(du);
#endif /* defined(LACP_DEBUG) */

	lacp_sm_rx(lp, du);

	m_freem(m);
	return (error);

bad:
	m_freem(m);
	return (EINVAL);
}

void
lacp_fill_actorinfo(struct lacp_port *lp, struct lacp_peerinfo *info)
{
	struct lacp_softc *lsc = lp->lp_lsc;
	struct trunk_port *tp = lp->lp_trunk;
	struct trunk_softc *sc = tp->tp_trunk;

	info->lip_systemid.lsi_prio = htons(lsc->lsc_sys_prio);
	memcpy(&info->lip_systemid.lsi_mac,
	    sc->tr_ac.ac_enaddr, ETHER_ADDR_LEN);
	info->lip_portid.lpi_prio = htons(lsc->lsc_port_prio);
	info->lip_portid.lpi_portno = htons(lp->lp_ifp->if_index);
	info->lip_state = lp->lp_state;
}

void
lacp_fill_markerinfo(struct lacp_port *lp, struct lacp_markerinfo *info)
{
	struct ifnet *ifp = lp->lp_ifp;

	/* Fill in the port index and system id (encoded as the MAC) */
	info->mi_rq_port = htons(ifp->if_index);
	memcpy(&info->mi_rq_system, lp->lp_systemid.lsi_mac, ETHER_ADDR_LEN);
	info->mi_rq_xid = htonl(0);
}

int
lacp_xmit_lacpdu(struct lacp_port *lp)
{
	struct lacp_softc *lsc = lp->lp_lsc;
	struct trunk_port *tp = lp->lp_trunk;
	struct mbuf *m;
	struct lacpdu *du;
	int error;

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOMEM);
	m->m_len = m->m_pkthdr.len = sizeof(*du);
	m->m_pkthdr.pf.prio = lsc->lsc_ifq_prio;

	du = mtod(m, struct lacpdu *);
	memset(du, 0, sizeof(*du));

	memcpy(&du->ldu_eh.ether_dhost, ethermulticastaddr_slowprotocols,
	    ETHER_ADDR_LEN);
	memcpy(&du->ldu_eh.ether_shost, tp->tp_lladdr, ETHER_ADDR_LEN);
	du->ldu_eh.ether_type = htons(ETHERTYPE_SLOW);

	du->ldu_sph.sph_subtype = SLOWPROTOCOLS_SUBTYPE_LACP;
	du->ldu_sph.sph_version = 1;

	TLV_SET(&du->ldu_tlv_actor, LACP_TYPE_ACTORINFO, sizeof(du->ldu_actor));
	du->ldu_actor = lp->lp_actor;

	TLV_SET(&du->ldu_tlv_partner, LACP_TYPE_PARTNERINFO,
	    sizeof(du->ldu_partner));
	du->ldu_partner = lp->lp_partner;

	TLV_SET(&du->ldu_tlv_collector, LACP_TYPE_COLLECTORINFO,
	    sizeof(du->ldu_collector));
	du->ldu_collector.lci_maxdelay = 0;

#if defined(LACP_DEBUG)
	LACP_DPRINTF((lp, "lacpdu transmit\n"));
	lacp_dump_lacpdu(du);
#endif /* defined(LACP_DEBUG) */

	m->m_flags |= M_MCAST;

	/*
	 * XXX should use higher priority queue.
	 * otherwise network congestion can break aggregation.
	 */
	error = if_enqueue(lp->lp_ifp, m);
	return (error);
}

int
lacp_xmit_marker(struct lacp_port *lp)
{
	struct lacp_softc *lsc = lp->lp_lsc;
	struct trunk_port *tp = lp->lp_trunk;
	struct mbuf *m;
	struct markerdu *mdu;
	int error;

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOMEM);
	m->m_len = m->m_pkthdr.len = sizeof(*mdu);
	m->m_pkthdr.pf.prio = lsc->lsc_ifq_prio;

	mdu = mtod(m, struct markerdu *);
	memset(mdu, 0, sizeof(*mdu));

	memcpy(&mdu->mdu_eh.ether_dhost, ethermulticastaddr_slowprotocols,
	    ETHER_ADDR_LEN);
	memcpy(&mdu->mdu_eh.ether_shost, tp->tp_lladdr, ETHER_ADDR_LEN);
	mdu->mdu_eh.ether_type = htons(ETHERTYPE_SLOW);

	mdu->mdu_sph.sph_subtype = SLOWPROTOCOLS_SUBTYPE_MARKER;
	mdu->mdu_sph.sph_version = 1;

	/* Bump the transaction id and copy over the marker info */
	lp->lp_marker.mi_rq_xid = htonl(ntohl(lp->lp_marker.mi_rq_xid) + 1);
	TLV_SET(&mdu->mdu_tlv, MARKER_TYPE_INFO, sizeof(mdu->mdu_info));
	mdu->mdu_info = lp->lp_marker;

	LACP_DPRINTF((lp, "marker transmit, port=%u, sys=%s, id=%u\n",
	    ntohs(mdu->mdu_info.mi_rq_port),
	    ether_sprintf(mdu->mdu_info.mi_rq_system),
	    ntohl(mdu->mdu_info.mi_rq_xid)));

	m->m_flags |= M_MCAST;
	error = if_enqueue(lp->lp_ifp, m);
	return (error);
}

void
lacp_linkstate(struct trunk_port *tp)
{
	struct lacp_port *lp = LACP_PORT(tp);
	u_int8_t old_state;
	u_int16_t old_key;

	old_state = lp->lp_state;
	old_key = lp->lp_key;

	/*
	 * If the port is not an active full duplex Ethernet link then it can
	 * not be aggregated.
	 */
	if (tp->tp_link_state == LINK_STATE_UNKNOWN ||
	    tp->tp_link_state == LINK_STATE_FULL_DUPLEX)
		lacp_port_enable(lp);
	else
		lacp_port_disable(lp);

	lp->lp_key = lacp_compose_key(lp);

	if (old_state != lp->lp_state || old_key != lp->lp_key) {
		LACP_DPRINTF((lp, "-> UNSELECTED\n"));
		lp->lp_selected = LACP_UNSELECTED;
	}
}

void
lacp_tick(void *arg)
{
	struct lacp_softc *lsc = arg;
	struct lacp_port *lp;

	LIST_FOREACH(lp, &lsc->lsc_ports, lp_next) {
		if ((lp->lp_state & LACP_STATE_AGGREGATION) == 0)
			continue;

		lacp_run_timers(lp);

		lacp_select(lp);
		lacp_sm_mux(lp);
		lacp_sm_tx(lp);
		lacp_sm_ptx_tx_schedule(lp);
	}
	timeout_add_sec(&lsc->lsc_callout, 1);
}

int
lacp_port_create(struct trunk_port *tp)
{
	struct trunk_softc *sc = tp->tp_trunk;
	struct lacp_softc *lsc = LACP_SOFTC(sc);
	struct lacp_port *lp;
	struct ifnet *ifp = tp->tp_if;
	struct ifreq ifr;
	int error;

	bzero(&ifr, sizeof(ifr));
	ifr.ifr_addr.sa_family = AF_UNSPEC;
	ifr.ifr_addr.sa_len = ETHER_ADDR_LEN;
	bcopy(&ethermulticastaddr_slowprotocols,
	    ifr.ifr_addr.sa_data, ETHER_ADDR_LEN);

	error = ether_addmulti(&ifr, (struct arpcom *)ifp);
	if (error && error != ENETRESET) {
		printf("%s: ADDMULTI failed on %s\n", __func__, tp->tp_ifname);
		return (error);
	}

	lp = malloc(sizeof(struct lacp_port),
	    M_DEVBUF, M_NOWAIT|M_ZERO);
	if (lp == NULL)
		return (ENOMEM);

	tp->tp_psc = (caddr_t)lp;
	lp->lp_ifp = ifp;
	lp->lp_trunk = tp;
	lp->lp_lsc = lsc;

	mq_init(&lp->lp_mq, 8, IPL_NET);

	LIST_INSERT_HEAD(&lsc->lsc_ports, lp, lp_next);

	lacp_fill_actorinfo(lp, &lp->lp_actor);
	lacp_fill_markerinfo(lp, &lp->lp_marker);
	lp->lp_state =
	    (lsc->lsc_mode ? LACP_STATE_ACTIVITY : 0) |
	    (lsc->lsc_timeout ? LACP_STATE_TIMEOUT : 0);
	lp->lp_aggregator = NULL;
	lacp_sm_rx_set_expired(lp);

	lacp_linkstate(tp);

	return (0);
}

void
lacp_port_destroy(struct trunk_port *tp)
{
	struct lacp_port *lp = LACP_PORT(tp);
	struct mbuf *m;
	int i;

	for (i = 0; i < LACP_NTIMER; i++)
		LACP_TIMER_DISARM(lp, i);

	lacp_disable_collecting(lp);
	lacp_disable_distributing(lp);
	lacp_unselect(lp);

	LIST_REMOVE(lp, lp_next);

	while ((m = mq_dequeue(&lp->lp_mq)) != NULL)
		m_freem(m);

	free(lp, M_DEVBUF, sizeof(*lp));
}

void
lacp_req(struct trunk_softc *sc, caddr_t data)
{
	struct lacp_opreq *req = (struct lacp_opreq *)data;
	struct lacp_softc *lsc = LACP_SOFTC(sc);
	struct lacp_aggregator *la = lsc->lsc_active_aggregator;

	bzero(req, sizeof(struct lacp_opreq));
	if (la != NULL) {
		req->actor_prio = ntohs(la->la_actor.lip_systemid.lsi_prio);
		memcpy(&req->actor_mac, &la->la_actor.lip_systemid.lsi_mac,
		    ETHER_ADDR_LEN);
		req->actor_key = ntohs(la->la_actor.lip_key);
		req->actor_portprio = ntohs(la->la_actor.lip_portid.lpi_prio);
		req->actor_portno = ntohs(la->la_actor.lip_portid.lpi_portno);
		req->actor_state = la->la_actor.lip_state;

		req->partner_prio = ntohs(la->la_partner.lip_systemid.lsi_prio);
		memcpy(&req->partner_mac, &la->la_partner.lip_systemid.lsi_mac,
		    ETHER_ADDR_LEN);
		req->partner_key = ntohs(la->la_partner.lip_key);
		req->partner_portprio =
		    ntohs(la->la_partner.lip_portid.lpi_prio);
		req->partner_portno =
		    ntohs(la->la_partner.lip_portid.lpi_portno);
		req->partner_state = la->la_partner.lip_state;
	}
}

u_int
lacp_port_status(struct trunk_port *lgp)
{
	struct lacp_port	*lp = LACP_PORT(lgp);
	struct lacp_softc	*lsc = lp->lp_lsc;
	struct lacp_aggregator	*la = lp->lp_aggregator;
	u_int			 flags = 0;

	/* This port is joined to the active aggregator */
	if (la != NULL && la == lsc->lsc_active_aggregator)
		flags |= TRUNK_PORT_ACTIVE;

	if (lp->lp_state & LACP_STATE_COLLECTING)
		flags |= TRUNK_PORT_COLLECTING;
	if (lp->lp_state & LACP_STATE_DISTRIBUTING)
		flags |= TRUNK_PORT_DISTRIBUTING;

	return (flags);
}

void
lacp_portreq(struct trunk_port *tp, caddr_t data)
{
	struct lacp_opreq *req = (struct lacp_opreq *)data;
	struct lacp_port *lp = LACP_PORT(tp);

	req->actor_prio = ntohs(lp->lp_actor.lip_systemid.lsi_prio);
	memcpy(&req->actor_mac, &lp->lp_actor.lip_systemid.lsi_mac,
	    ETHER_ADDR_LEN);
	req->actor_key = ntohs(lp->lp_actor.lip_key);
	req->actor_portprio = ntohs(lp->lp_actor.lip_portid.lpi_prio);
	req->actor_portno = ntohs(lp->lp_actor.lip_portid.lpi_portno);
	req->actor_state = lp->lp_actor.lip_state;

	req->partner_prio = ntohs(lp->lp_partner.lip_systemid.lsi_prio);
	memcpy(&req->partner_mac, &lp->lp_partner.lip_systemid.lsi_mac,
	    ETHER_ADDR_LEN);
	req->partner_key = ntohs(lp->lp_partner.lip_key);
	req->partner_portprio = ntohs(lp->lp_partner.lip_portid.lpi_prio);
	req->partner_portno = ntohs(lp->lp_partner.lip_portid.lpi_portno);
	req->partner_state = lp->lp_partner.lip_state;
}

void
lacp_disable_collecting(struct lacp_port *lp)
{
	LACP_DPRINTF((lp, "collecting disabled\n"));
	lp->lp_state &= ~LACP_STATE_COLLECTING;
}

void
lacp_enable_collecting(struct lacp_port *lp)
{
	LACP_DPRINTF((lp, "collecting enabled\n"));
	lp->lp_state |= LACP_STATE_COLLECTING;
}

void
lacp_disable_distributing(struct lacp_port *lp)
{
	struct lacp_aggregator *la = lp->lp_aggregator;
	struct lacp_softc *lsc = lp->lp_lsc;
#if defined(LACP_DEBUG)
	char buf[LACP_LAGIDSTR_MAX+1];
#endif /* defined(LACP_DEBUG) */

	if (la == NULL || (lp->lp_state & LACP_STATE_DISTRIBUTING) == 0)
		return;

	KASSERT(!TAILQ_EMPTY(&la->la_ports));
	KASSERT(la->la_nports > 0);
	KASSERT(la->la_refcnt >= la->la_nports);

	LACP_DPRINTF((lp, "disable distributing on aggregator %s, "
	    "nports %d -> %d\n",
	    lacp_format_lagid_aggregator(la, buf, sizeof(buf)),
	    la->la_nports, la->la_nports - 1));

	TAILQ_REMOVE(&la->la_ports, lp, lp_dist_q);
	la->la_nports--;

	if (lsc->lsc_active_aggregator == la) {
		lacp_suppress_distributing(lsc, la);
		lacp_select_active_aggregator(lsc);
		/* regenerate the port map, the active aggregator has changed */
		lacp_update_portmap(lsc);
	}

	lp->lp_state &= ~LACP_STATE_DISTRIBUTING;
}

void
lacp_enable_distributing(struct lacp_port *lp)
{
	struct lacp_aggregator *la = lp->lp_aggregator;
	struct lacp_softc *lsc = lp->lp_lsc;
#if defined(LACP_DEBUG)
	char buf[LACP_LAGIDSTR_MAX+1];
#endif /* defined(LACP_DEBUG) */

	if ((lp->lp_state & LACP_STATE_DISTRIBUTING) != 0)
		return;

	LACP_DPRINTF((lp, "enable distributing on aggregator %s, "
	    "nports %d -> %d\n",
	    lacp_format_lagid_aggregator(la, buf, sizeof(buf)),
	    la->la_nports, la->la_nports + 1));

	KASSERT(la->la_refcnt > la->la_nports);
	TAILQ_INSERT_HEAD(&la->la_ports, lp, lp_dist_q);
	la->la_nports++;

	lp->lp_state |= LACP_STATE_DISTRIBUTING;

	if (lsc->lsc_active_aggregator == la) {
		lacp_suppress_distributing(lsc, la);
		lacp_update_portmap(lsc);
	} else
		/* try to become the active aggregator */
		lacp_select_active_aggregator(lsc);
}

void
lacp_transit_expire(void *vp)
{
	struct lacp_softc *lsc = vp;

	LACP_DPRINTF((NULL, "%s\n", __func__));
	lsc->lsc_suppress_distributing = 0;
}

int
lacp_attach(struct trunk_softc *sc)
{
	struct lacp_softc *lsc;

	lsc = malloc(sizeof(struct lacp_softc),
	    M_DEVBUF, M_NOWAIT|M_ZERO);
	if (lsc == NULL)
		return (ENOMEM);

	sc->tr_psc = (caddr_t)lsc;
	lsc->lsc_softc = sc;

	arc4random_buf(&lsc->lsc_hashkey, sizeof(lsc->lsc_hashkey));
	lsc->lsc_active_aggregator = NULL;
	TAILQ_INIT(&lsc->lsc_aggregators);
	LIST_INIT(&lsc->lsc_ports);

	/* set default admin values */
	lsc->lsc_mode = LACP_DEFAULT_MODE;
	lsc->lsc_timeout = LACP_DEFAULT_TIMEOUT;
	lsc->lsc_sys_prio = LACP_DEFAULT_SYSTEM_PRIO;
	lsc->lsc_port_prio = LACP_DEFAULT_PORT_PRIO;
	lsc->lsc_ifq_prio = LACP_DEFAULT_IFQ_PRIO;

	timeout_set(&lsc->lsc_transit_callout, lacp_transit_expire, lsc);
	timeout_set(&lsc->lsc_callout, lacp_tick, lsc);
	task_set(&lsc->lsc_input, lacp_input_process, lsc);

	/* if the trunk is already up then do the same */
	if (sc->tr_ac.ac_if.if_flags & IFF_RUNNING)
		lacp_init(sc);

	return (0);
}

int
lacp_detach(struct trunk_softc *sc)
{
	struct lacp_softc *lsc = LACP_SOFTC(sc);

	KASSERT(TAILQ_EMPTY(&lsc->lsc_aggregators));
	KASSERT(lsc->lsc_active_aggregator == NULL);

	sc->tr_psc = NULL;
	timeout_del(&lsc->lsc_transit_callout);
	timeout_del(&lsc->lsc_callout);

	free(lsc, M_DEVBUF, sizeof(*lsc));
	return (0);
}

void
lacp_init(struct trunk_softc *sc)
{
	struct lacp_softc *lsc = LACP_SOFTC(sc);

	timeout_add_sec(&lsc->lsc_callout, 1);
}

void
lacp_stop(struct trunk_softc *sc)
{
	struct lacp_softc *lsc = LACP_SOFTC(sc);

	timeout_del(&lsc->lsc_transit_callout);
	timeout_del(&lsc->lsc_callout);
}

struct trunk_port *
lacp_select_tx_port(struct trunk_softc *sc, struct mbuf *m)
{
	struct lacp_softc *lsc = LACP_SOFTC(sc);
	struct lacp_portmap *pm;
	struct lacp_port *lp;
	u_int32_t hash;

	if (__predict_false(lsc->lsc_suppress_distributing)) {
		LACP_DPRINTF((NULL, "%s: waiting transit\n", __func__));
		return (NULL);
	}

	pm = &lsc->lsc_pmap[lsc->lsc_activemap];
	if (pm->pm_count == 0) {
		LACP_DPRINTF((NULL, "%s: no active aggregator\n", __func__));
		return (NULL);
	}

	hash = trunk_hashmbuf(m, &lsc->lsc_hashkey);
	hash %= pm->pm_count;
	lp = pm->pm_map[hash];

	KASSERT((lp->lp_state & LACP_STATE_DISTRIBUTING) != 0);

	return (lp->lp_trunk);
}

/*
 * lacp_suppress_distributing: drop transmit packets for a while
 * to preserve packet ordering.
 */
void
lacp_suppress_distributing(struct lacp_softc *lsc, struct lacp_aggregator *la)
{
	struct lacp_port *lp;

	if (lsc->lsc_active_aggregator != la)
		return;

	LACP_DPRINTF((NULL, "%s\n", __func__));
	lsc->lsc_suppress_distributing = 1;

	/* send a marker frame down each port to verify the queues are empty */
	LIST_FOREACH(lp, &lsc->lsc_ports, lp_next) {
		lp->lp_flags |= LACP_PORT_MARK;
		lacp_xmit_marker(lp);
	}

	/* set a timeout for the marker frames */
	timeout_add_msec(&lsc->lsc_transit_callout, LACP_TRANSIT_DELAY);
}

int
lacp_compare_peerinfo(const struct lacp_peerinfo *a,
    const struct lacp_peerinfo *b)
{
	return (memcmp(a, b, offsetof(struct lacp_peerinfo, lip_state)));
}

int
lacp_compare_systemid(const struct lacp_systemid *a,
    const struct lacp_systemid *b)
{
	return (memcmp(a, b, sizeof(*a)));
}

#if 0	/* unused */
int
lacp_compare_portid(const struct lacp_portid *a,
    const struct lacp_portid *b)
{
	return (memcmp(a, b, sizeof(*a)));
}
#endif

u_int64_t
lacp_aggregator_bandwidth(struct lacp_aggregator *la)
{
	struct lacp_port *lp;
	u_int64_t speed;

	lp = TAILQ_FIRST(&la->la_ports);
	if (lp == NULL)
		return (0);

	speed = lp->lp_ifp->if_baudrate;
	speed *= la->la_nports;
	if (speed == 0) {
		LACP_DPRINTF((lp, "speed 0? media=0x%x nports=%d\n",
		    lp->lp_media, la->la_nports));
	}

	return (speed);
}

/*
 * lacp_select_active_aggregator: select an aggregator to be used to transmit
 * packets from trunk(4) interface.
 */
void
lacp_select_active_aggregator(struct lacp_softc *lsc)
{
	struct lacp_aggregator *la;
	struct lacp_aggregator *best_la = NULL;
	u_int64_t best_speed = 0;
#if defined(LACP_DEBUG)
	char buf[LACP_LAGIDSTR_MAX+1];
#endif /* defined(LACP_DEBUG) */

	LACP_DPRINTF((NULL, "%s:\n", __func__));

	TAILQ_FOREACH(la, &lsc->lsc_aggregators, la_q) {
		u_int64_t speed;

		if (la->la_nports == 0)
			continue;

		speed = lacp_aggregator_bandwidth(la);
		LACP_DPRINTF((NULL, "%s, speed=%jd, nports=%d\n",
		    lacp_format_lagid_aggregator(la, buf, sizeof(buf)),
		    speed, la->la_nports));

		/*
		 * This aggregator is chosen if
		 *      the partner has a better system priority
		 *  or, the total aggregated speed is higher
		 *  or, it is already the chosen aggregator
		 */
		if ((best_la == NULL || LACP_SYS_PRI(la->la_partner) <
		     LACP_SYS_PRI(best_la->la_partner)) ||
		    speed > best_speed ||
		    (speed == best_speed &&
		    la == lsc->lsc_active_aggregator)) {
			best_la = la;
			best_speed = speed;
		}
	}

	KASSERT(best_la == NULL || best_la->la_nports > 0);
	KASSERT(best_la == NULL || !TAILQ_EMPTY(&best_la->la_ports));

#if defined(LACP_DEBUG)
	if (lsc->lsc_active_aggregator != best_la) {
		LACP_DPRINTF((NULL, "active aggregator changed\n"));
		LACP_DPRINTF((NULL, "old %s\n",
		    lacp_format_lagid_aggregator(lsc->lsc_active_aggregator,
		    buf, sizeof(buf))));
	} else
		LACP_DPRINTF((NULL, "active aggregator not changed\n"));

	LACP_DPRINTF((NULL, "new %s\n",
	    lacp_format_lagid_aggregator(best_la, buf, sizeof(buf))));
#endif /* defined(LACP_DEBUG) */

	if (lsc->lsc_active_aggregator != best_la) {
		lsc->lsc_active_aggregator = best_la;
		lacp_update_portmap(lsc);
		if (best_la)
			lacp_suppress_distributing(lsc, best_la);
	}
}

/*
 * Updated the inactive portmap array with the new list of ports and
 * make it live.
 */
void
lacp_update_portmap(struct lacp_softc *lsc)
{
	struct lacp_aggregator *la;
	struct lacp_portmap *p;
	struct lacp_port *lp;
	u_int newmap;
	int i;

	newmap = lsc->lsc_activemap == 0 ? 1 : 0;
	p = &lsc->lsc_pmap[newmap];
	la = lsc->lsc_active_aggregator;
	bzero(p, sizeof(struct lacp_portmap));

	if (la != NULL && la->la_nports > 0) {
		p->pm_count = la->la_nports;
		i = 0;
		TAILQ_FOREACH(lp, &la->la_ports, lp_dist_q)
			p->pm_map[i++] = lp;
		KASSERT(i == p->pm_count);
	}

	/* switch the active portmap over */
	lsc->lsc_activemap = newmap;
	LACP_DPRINTF((NULL, "Set table %d with %d ports\n",
		    lsc->lsc_activemap,
		    lsc->lsc_pmap[lsc->lsc_activemap].pm_count));
}

u_int16_t
lacp_compose_key(struct lacp_port *lp)
{
	struct trunk_port *tp = lp->lp_trunk;
	struct trunk_softc *sc = tp->tp_trunk;
	u_int64_t speed;
	u_int16_t key;

	if ((lp->lp_state & LACP_STATE_AGGREGATION) == 0) {
		/* bit 0..14: (some bits of) if_index of this port */
		key = lp->lp_ifp->if_index;

		/* non-aggregatable */
		key |= 0x8000;
	} else {
		/* bit 0..2: speed indication */
		speed = lp->lp_ifp->if_baudrate;
		if (speed == 0)
			key = 0;
		else if (speed <= IF_Mbps(1))
			key = 1;
		else if (speed <= IF_Mbps(10))
			key = 2;
		else if (speed <= IF_Mbps(100))
			key = 3;
		else if (speed <= IF_Gbps(1))
			key = 4;
		else if (speed <= IF_Gbps(10))
			key = 5;
		else if (speed <= IF_Gbps(100))
			key = 6;
		else
			key = 7;

		/* bit 3..13: (some bits of) if_index of the trunk device */
		key |= sc->tr_ac.ac_if.if_index << 3;

		/* bit 14: the port active flag (includes link state) */
		if (TRUNK_PORTACTIVE(tp))
			key |= 0x4000;
		else
			key &= ~0x4000;

		/* clear the non-aggregatable bit */
		key &= ~0x8000;
	}
	return (htons(key));
}

void
lacp_aggregator_addref(struct lacp_softc *lsc, struct lacp_aggregator *la)
{
#if defined(LACP_DEBUG)
	char buf[LACP_LAGIDSTR_MAX+1];
#endif

	LACP_DPRINTF((NULL, "%s: lagid=%s, refcnt %d -> %d\n",
	    __func__,
	    lacp_format_lagid(&la->la_actor, &la->la_partner,
	    buf, sizeof(buf)),
	    la->la_refcnt, la->la_refcnt + 1));

	KASSERT(la->la_refcnt > 0);
	la->la_refcnt++;
	KASSERT(la->la_refcnt > la->la_nports);
}

void
lacp_aggregator_delref(struct lacp_softc *lsc, struct lacp_aggregator *la)
{
#if defined(LACP_DEBUG)
	char buf[LACP_LAGIDSTR_MAX+1];
#endif

	LACP_DPRINTF((NULL, "%s: lagid=%s, refcnt %d -> %d\n",
	    __func__,
	    lacp_format_lagid(&la->la_actor, &la->la_partner,
	    buf, sizeof(buf)),
	    la->la_refcnt, la->la_refcnt - 1));

	KASSERT(la->la_refcnt > la->la_nports);
	la->la_refcnt--;
	if (la->la_refcnt > 0)
		return;

	KASSERT(la->la_refcnt == 0);
	KASSERT(lsc->lsc_active_aggregator != la);

	TAILQ_REMOVE(&lsc->lsc_aggregators, la, la_q);

	free(la, M_DEVBUF, sizeof(*la));
}

/*
 * lacp_aggregator_get: allocate an aggregator.
 */
struct lacp_aggregator *
lacp_aggregator_get(struct lacp_softc *lsc, struct lacp_port *lp)
{
	struct lacp_aggregator *la;

	la = malloc(sizeof(*la), M_DEVBUF, M_NOWAIT);
	if (la) {
		la->la_refcnt = 1;
		la->la_nports = 0;
		TAILQ_INIT(&la->la_ports);
		la->la_pending = 0;
		TAILQ_INSERT_TAIL(&lsc->lsc_aggregators, la, la_q);
	}

	return (la);
}

/*
 * lacp_fill_aggregator_id: setup a newly allocated aggregator from a port.
 */
void
lacp_fill_aggregator_id(struct lacp_aggregator *la, const struct lacp_port *lp)
{
	lacp_fill_aggregator_id_peer(&la->la_partner, &lp->lp_partner);
	lacp_fill_aggregator_id_peer(&la->la_actor, &lp->lp_actor);

	la->la_actor.lip_state = lp->lp_state & LACP_STATE_AGGREGATION;
}

void
lacp_fill_aggregator_id_peer(struct lacp_peerinfo *lpi_aggr,
    const struct lacp_peerinfo *lpi_port)
{
	memset(lpi_aggr, 0, sizeof(*lpi_aggr));
	lpi_aggr->lip_systemid = lpi_port->lip_systemid;
	lpi_aggr->lip_key = lpi_port->lip_key;
}

/*
 * lacp_aggregator_is_compatible: check if a port can join to an aggregator.
 */
int
lacp_aggregator_is_compatible(const struct lacp_aggregator *la,
    const struct lacp_port *lp)
{
	if (!(lp->lp_state & LACP_STATE_AGGREGATION) ||
	    !(lp->lp_partner.lip_state & LACP_STATE_AGGREGATION))
		return (0);

	if (!(la->la_actor.lip_state & LACP_STATE_AGGREGATION))
		return (0);

	if (!lacp_peerinfo_is_compatible(&la->la_partner, &lp->lp_partner))
		return (0);

	if (!lacp_peerinfo_is_compatible(&la->la_actor, &lp->lp_actor))
		return (0);

	return (1);
}

int
lacp_peerinfo_is_compatible(const struct lacp_peerinfo *a,
    const struct lacp_peerinfo *b)
{
	if (memcmp(&a->lip_systemid, &b->lip_systemid,
	    sizeof(a->lip_systemid)))
		return (0);

	if (memcmp(&a->lip_key, &b->lip_key, sizeof(a->lip_key)))
		return (0);

	return (1);
}

void
lacp_port_enable(struct lacp_port *lp)
{
	lp->lp_state |= LACP_STATE_AGGREGATION;
}

void
lacp_port_disable(struct lacp_port *lp)
{
	lacp_set_mux(lp, LACP_MUX_DETACHED);

	lp->lp_state &= ~LACP_STATE_AGGREGATION;
	lp->lp_selected = LACP_UNSELECTED;
	lacp_sm_rx_record_default(lp);
	lp->lp_partner.lip_state &= ~LACP_STATE_AGGREGATION;
	lp->lp_state &= ~LACP_STATE_EXPIRED;
}

/*
 * lacp_select: select an aggregator.  create one if necessary.
 */
void
lacp_select(struct lacp_port *lp)
{
	struct lacp_softc *lsc = lp->lp_lsc;
	struct lacp_aggregator *la;
#if defined(LACP_DEBUG)
	char buf[LACP_LAGIDSTR_MAX+1];
#endif

	if (lp->lp_aggregator)
		return;

	KASSERT(!LACP_TIMER_ISARMED(lp, LACP_TIMER_WAIT_WHILE));

	LACP_DPRINTF((lp, "port lagid=%s\n",
	    lacp_format_lagid(&lp->lp_actor, &lp->lp_partner,
	    buf, sizeof(buf))));

	TAILQ_FOREACH(la, &lsc->lsc_aggregators, la_q) {
		if (lacp_aggregator_is_compatible(la, lp))
			break;
	}

	if (la == NULL) {
		la = lacp_aggregator_get(lsc, lp);
		if (la == NULL) {
			LACP_DPRINTF((lp, "aggregator creation failed\n"));

			/*
			 * will retry on the next tick.
			 */

			return;
		}
		lacp_fill_aggregator_id(la, lp);
		LACP_DPRINTF((lp, "aggregator created\n"));
	} else {
		LACP_DPRINTF((lp, "compatible aggregator found\n"));
		if (la->la_refcnt == LACP_MAX_PORTS)
			return;
		lacp_aggregator_addref(lsc, la);
	}

	LACP_DPRINTF((lp, "aggregator lagid=%s\n",
	    lacp_format_lagid(&la->la_actor, &la->la_partner,
	    buf, sizeof(buf))));

	lp->lp_aggregator = la;
	lp->lp_selected = LACP_SELECTED;
}

/*
 * lacp_unselect: finish unselect/detach process.
 */
void
lacp_unselect(struct lacp_port *lp)
{
	struct lacp_softc *lsc = lp->lp_lsc;
	struct lacp_aggregator *la = lp->lp_aggregator;

	KASSERT(!LACP_TIMER_ISARMED(lp, LACP_TIMER_WAIT_WHILE));

	if (la == NULL)
		return;

	lp->lp_aggregator = NULL;
	lacp_aggregator_delref(lsc, la);
}

/* mux machine */
void
lacp_sm_mux(struct lacp_port *lp)
{
	enum lacp_mux_state new_state;
	int p_sync =
	    (lp->lp_partner.lip_state & LACP_STATE_SYNC) != 0;
	int p_collecting =
	    (lp->lp_partner.lip_state & LACP_STATE_COLLECTING) != 0;
	enum lacp_selected selected = lp->lp_selected;
	struct lacp_aggregator *la;

	/* LACP_DPRINTF((lp, "%s: state %d\n", __func__, lp->lp_mux_state)); */

re_eval:
	la = lp->lp_aggregator;
	KASSERT(lp->lp_mux_state == LACP_MUX_DETACHED || la != NULL);
	new_state = lp->lp_mux_state;
	switch (lp->lp_mux_state) {
	case LACP_MUX_DETACHED:
		if (selected != LACP_UNSELECTED)
			new_state = LACP_MUX_WAITING;
		break;
	case LACP_MUX_WAITING:
		KASSERT(la->la_pending > 0 ||
		    !LACP_TIMER_ISARMED(lp, LACP_TIMER_WAIT_WHILE));
		if (selected == LACP_SELECTED && la->la_pending == 0)
			new_state = LACP_MUX_ATTACHED;
		else if (selected == LACP_UNSELECTED)
			new_state = LACP_MUX_DETACHED;
		break;
	case LACP_MUX_ATTACHED:
		if (selected == LACP_SELECTED && p_sync)
			new_state = LACP_MUX_COLLECTING;
		else if (selected != LACP_SELECTED)
			new_state = LACP_MUX_DETACHED;
		break;
	case LACP_MUX_COLLECTING:
		if (selected == LACP_SELECTED && p_sync && p_collecting)
			new_state = LACP_MUX_DISTRIBUTING;
		else if (selected != LACP_SELECTED || !p_sync)
			new_state = LACP_MUX_ATTACHED;
		break;
	case LACP_MUX_DISTRIBUTING:
		if (selected != LACP_SELECTED || !p_sync || !p_collecting)
			new_state = LACP_MUX_COLLECTING;
		break;
	default:
		panic("%s: unknown state", __func__);
	}

	if (lp->lp_mux_state == new_state)
		return;

	lacp_set_mux(lp, new_state);
	goto re_eval;
}

void
lacp_set_mux(struct lacp_port *lp, enum lacp_mux_state new_state)
{
	struct lacp_aggregator *la = lp->lp_aggregator;

	if (lp->lp_mux_state == new_state)
		return;

	switch (new_state) {
	case LACP_MUX_DETACHED:
		lp->lp_state &= ~LACP_STATE_SYNC;
		lacp_disable_distributing(lp);
		lacp_disable_collecting(lp);
		lacp_sm_assert_ntt(lp);
		/* cancel timer */
		if (LACP_TIMER_ISARMED(lp, LACP_TIMER_WAIT_WHILE)) {
			KASSERT(la->la_pending > 0);
			la->la_pending--;
		}
		LACP_TIMER_DISARM(lp, LACP_TIMER_WAIT_WHILE);
		lacp_unselect(lp);
		break;
	case LACP_MUX_WAITING:
		LACP_TIMER_ARM(lp, LACP_TIMER_WAIT_WHILE,
		    LACP_AGGREGATE_WAIT_TIME);
		la->la_pending++;
		break;
	case LACP_MUX_ATTACHED:
		lp->lp_state |= LACP_STATE_SYNC;
		lacp_disable_collecting(lp);
		lacp_sm_assert_ntt(lp);
		break;
	case LACP_MUX_COLLECTING:
		lacp_enable_collecting(lp);
		lacp_disable_distributing(lp);
		lacp_sm_assert_ntt(lp);
		break;
	case LACP_MUX_DISTRIBUTING:
		lacp_enable_distributing(lp);
		break;
	default:
		panic("%s: unknown state", __func__);
	}

	LACP_DPRINTF((lp, "mux_state %d -> %d\n", lp->lp_mux_state, new_state));

	lp->lp_mux_state = new_state;
}

void
lacp_sm_mux_timer(struct lacp_port *lp)
{
	struct lacp_aggregator *la = lp->lp_aggregator;
#if defined(LACP_DEBUG)
	char buf[LACP_LAGIDSTR_MAX+1];
#endif

	KASSERT(la->la_pending > 0);

	LACP_DPRINTF((lp, "%s: aggregator %s, pending %d -> %d\n", __func__,
	    lacp_format_lagid(&la->la_actor, &la->la_partner,
	    buf, sizeof(buf)),
	    la->la_pending, la->la_pending - 1));

	la->la_pending--;
}

/* periodic transmit machine */
void
lacp_sm_ptx_update_timeout(struct lacp_port *lp, u_int8_t oldpstate)
{
	if (LACP_STATE_EQ(oldpstate, lp->lp_partner.lip_state,
	    LACP_STATE_TIMEOUT))
		return;

	LACP_DPRINTF((lp, "partner timeout changed\n"));

	/*
	 * FAST_PERIODIC -> SLOW_PERIODIC
	 * or
	 * SLOW_PERIODIC (-> PERIODIC_TX) -> FAST_PERIODIC
	 *
	 * let lacp_sm_ptx_tx_schedule to update timeout.
	 */

	LACP_TIMER_DISARM(lp, LACP_TIMER_PERIODIC);

	/* if timeout has been shortened, assert NTT. */
	if ((lp->lp_partner.lip_state & LACP_STATE_TIMEOUT))
		lacp_sm_assert_ntt(lp);
}

void
lacp_sm_ptx_tx_schedule(struct lacp_port *lp)
{
	int timeout;

	if (!(lp->lp_state & LACP_STATE_ACTIVITY) &&
	    !(lp->lp_partner.lip_state & LACP_STATE_ACTIVITY)) {

		/* NO_PERIODIC */
		LACP_TIMER_DISARM(lp, LACP_TIMER_PERIODIC);
		return;
	}

	if (LACP_TIMER_ISARMED(lp, LACP_TIMER_PERIODIC))
		return;

	timeout = (lp->lp_partner.lip_state & LACP_STATE_TIMEOUT) ?
	    LACP_FAST_PERIODIC_TIME : LACP_SLOW_PERIODIC_TIME;

	LACP_TIMER_ARM(lp, LACP_TIMER_PERIODIC, timeout);
}

void
lacp_sm_ptx_timer(struct lacp_port *lp)
{
	lacp_sm_assert_ntt(lp);
}

void
lacp_sm_rx(struct lacp_port *lp, const struct lacpdu *du)
{
	int timeout;

	/* check LACP_DISABLED first */
	if (!(lp->lp_state & LACP_STATE_AGGREGATION))
		return;

	/* check loopback condition. */
	if (!lacp_compare_systemid(&du->ldu_actor.lip_systemid,
	    &lp->lp_actor.lip_systemid))
		return;

	/*
	 * EXPIRED, DEFAULTED, CURRENT -> CURRENT
	 */
	lacp_sm_rx_update_selected(lp, du);
	lacp_sm_rx_update_ntt(lp, du);
	lacp_sm_rx_record_pdu(lp, du);

	timeout = (lp->lp_state & LACP_STATE_TIMEOUT) ?
	    LACP_SHORT_TIMEOUT_TIME : LACP_LONG_TIMEOUT_TIME;
	LACP_TIMER_ARM(lp, LACP_TIMER_CURRENT_WHILE, timeout);

	lp->lp_state &= ~LACP_STATE_EXPIRED;

	/* kick transmit machine without waiting the next tick. */
	lacp_sm_tx(lp);
}

void
lacp_sm_rx_set_expired(struct lacp_port *lp)
{
	lp->lp_partner.lip_state &= ~LACP_STATE_SYNC;
	lp->lp_partner.lip_state |= LACP_STATE_TIMEOUT;
	LACP_TIMER_ARM(lp, LACP_TIMER_CURRENT_WHILE, LACP_SHORT_TIMEOUT_TIME);
	lp->lp_state |= LACP_STATE_EXPIRED;
}

void
lacp_sm_rx_timer(struct lacp_port *lp)
{
	if ((lp->lp_state & LACP_STATE_EXPIRED) == 0) {
		/* CURRENT -> EXPIRED */
		LACP_DPRINTF((lp, "%s: CURRENT -> EXPIRED\n", __func__));
		lacp_sm_rx_set_expired(lp);
	} else {
		/* EXPIRED -> DEFAULTED */
		LACP_DPRINTF((lp, "%s: EXPIRED -> DEFAULTED\n", __func__));
		lacp_sm_rx_update_default_selected(lp);
		lacp_sm_rx_record_default(lp);
		lp->lp_state &= ~LACP_STATE_EXPIRED;
	}
}

void
lacp_sm_rx_record_pdu(struct lacp_port *lp, const struct lacpdu *du)
{
	int active;
	u_int8_t oldpstate;
#if defined(LACP_DEBUG)
	char buf[LACP_STATESTR_MAX+1];
#endif

	/* LACP_DPRINTF((lp, "%s\n", __func__)); */

	oldpstate = lp->lp_partner.lip_state;

	active = (du->ldu_actor.lip_state & LACP_STATE_ACTIVITY)
	    || ((lp->lp_state & LACP_STATE_ACTIVITY) &&
	    (du->ldu_partner.lip_state & LACP_STATE_ACTIVITY));

	lp->lp_partner = du->ldu_actor;
	if (active &&
	    ((LACP_STATE_EQ(lp->lp_state, du->ldu_partner.lip_state,
	    LACP_STATE_AGGREGATION) &&
	    !lacp_compare_peerinfo(&lp->lp_actor, &du->ldu_partner))
	    || (du->ldu_partner.lip_state & LACP_STATE_AGGREGATION) == 0)) {
		/* XXX nothing? */
	} else
		lp->lp_partner.lip_state &= ~LACP_STATE_SYNC;

	lp->lp_state &= ~LACP_STATE_DEFAULTED;

	if (oldpstate != lp->lp_partner.lip_state) {
		LACP_DPRINTF((lp, "old pstate %s\n",
		    lacp_format_state(oldpstate, buf, sizeof(buf))));
		LACP_DPRINTF((lp, "new pstate %s\n",
		    lacp_format_state(lp->lp_partner.lip_state, buf,
		    sizeof(buf))));
	}

	lacp_sm_ptx_update_timeout(lp, oldpstate);
}

void
lacp_sm_rx_update_ntt(struct lacp_port *lp, const struct lacpdu *du)
{
	/* LACP_DPRINTF((lp, "%s\n", __func__)); */

	if (lacp_compare_peerinfo(&lp->lp_actor, &du->ldu_partner) ||
	    !LACP_STATE_EQ(lp->lp_state, du->ldu_partner.lip_state,
	    LACP_STATE_ACTIVITY | LACP_STATE_SYNC | LACP_STATE_AGGREGATION)) {
		LACP_DPRINTF((lp, "%s: assert ntt\n", __func__));
		lacp_sm_assert_ntt(lp);
	}
}

void
lacp_sm_rx_record_default(struct lacp_port *lp)
{
	struct lacp_softc *lsc;
	u_int8_t oldpstate;

	lsc = lp->lp_lsc;

	/* LACP_DPRINTF((lp, "%s\n", __func__)); */

	oldpstate = lp->lp_partner.lip_state;
	lacp_default_partner(lsc, &(lp->lp_partner));
	lp->lp_state |= LACP_STATE_DEFAULTED;
	lacp_sm_ptx_update_timeout(lp, oldpstate);
}

void
lacp_sm_rx_update_selected_from_peerinfo(struct lacp_port *lp,
    const struct lacp_peerinfo *info)
{
	/* LACP_DPRINTF((lp, "%s\n", __func__)); */

	if (lacp_compare_peerinfo(&lp->lp_partner, info) ||
	    !LACP_STATE_EQ(lp->lp_partner.lip_state, info->lip_state,
	    LACP_STATE_AGGREGATION)) {
		lp->lp_selected = LACP_UNSELECTED;
		/* mux machine will clean up lp->lp_aggregator */
	}
}

void
lacp_sm_rx_update_selected(struct lacp_port *lp, const struct lacpdu *du)
{
	/* LACP_DPRINTF((lp, "%s\n", __func__)); */

	lacp_sm_rx_update_selected_from_peerinfo(lp, &du->ldu_actor);
}

void
lacp_sm_rx_update_default_selected(struct lacp_port *lp)
{
	struct lacp_softc *lsc;
	struct lacp_peerinfo peer;

	lsc = lp->lp_lsc;
	lacp_default_partner(lsc, &peer);
	/* LACP_DPRINTF((lp, "%s\n", __func__)); */

	lacp_sm_rx_update_selected_from_peerinfo(lp, &peer);
}

/* transmit machine */

void
lacp_sm_tx(struct lacp_port *lp)
{
	int error;

	if (!(lp->lp_state & LACP_STATE_AGGREGATION)
#if 1
	    || (!(lp->lp_state & LACP_STATE_ACTIVITY)
	    && !(lp->lp_partner.lip_state & LACP_STATE_ACTIVITY))
#endif
	    ) {
		lp->lp_flags &= ~LACP_PORT_NTT;
	}

	if (!(lp->lp_flags & LACP_PORT_NTT))
		return;

	/* Rate limit to 3 PDUs per LACP_FAST_PERIODIC_TIME */
	if (ppsratecheck(&lp->lp_last_lacpdu, &lp->lp_lacpdu_sent,
		    (3 / LACP_FAST_PERIODIC_TIME)) == 0) {
		LACP_DPRINTF((lp, "rate limited pdu\n"));
		return;
	}

	error = lacp_xmit_lacpdu(lp);

	if (error == 0)
		lp->lp_flags &= ~LACP_PORT_NTT;
	else
		LACP_DPRINTF((lp, "lacpdu transmit failure, error %d\n",
		    error));
}

void
lacp_sm_assert_ntt(struct lacp_port *lp)
{
	lp->lp_flags |= LACP_PORT_NTT;
}

void
lacp_run_timers(struct lacp_port *lp)
{
	int i;

	for (i = 0; i < LACP_NTIMER; i++) {
		KASSERT(lp->lp_timer[i] >= 0);
		if (lp->lp_timer[i] == 0)
			continue;
		else if (--lp->lp_timer[i] <= 0) {
			if (lacp_timer_funcs[i])
				(*lacp_timer_funcs[i])(lp);
		}
	}
}

int
lacp_marker_input(struct lacp_port *lp, struct mbuf *m)
{
	struct lacp_softc *lsc = lp->lp_lsc;
	struct trunk_port *tp = lp->lp_trunk;
	struct lacp_port *lp2;
	struct markerdu *mdu;
	int error = 0;
	int pending = 0;

	if (m->m_pkthdr.len != sizeof(*mdu))
		goto bad;

	if ((m->m_flags & M_MCAST) == 0)
		goto bad;

	if (m->m_len < sizeof(*mdu)) {
		m = m_pullup(m, sizeof(*mdu));
		if (m == NULL)
			return (ENOMEM);
	}

	mdu = mtod(m, struct markerdu *);

	if (memcmp(&mdu->mdu_eh.ether_dhost,
	    &ethermulticastaddr_slowprotocols, ETHER_ADDR_LEN))
		goto bad;

	if (mdu->mdu_sph.sph_version != 1)
		goto bad;

	switch (mdu->mdu_tlv.tlv_type) {
	case MARKER_TYPE_INFO:
		if (tlv_check(mdu, sizeof(*mdu), &mdu->mdu_tlv,
		    marker_info_tlv_template, 1))
			goto bad;

		mdu->mdu_tlv.tlv_type = MARKER_TYPE_RESPONSE;
		memcpy(&mdu->mdu_eh.ether_dhost,
		    &ethermulticastaddr_slowprotocols, ETHER_ADDR_LEN);
		memcpy(&mdu->mdu_eh.ether_shost,
		    tp->tp_lladdr, ETHER_ADDR_LEN);
		error = if_enqueue(lp->lp_ifp, m);
		break;

	case MARKER_TYPE_RESPONSE:
		if (tlv_check(mdu, sizeof(*mdu), &mdu->mdu_tlv,
		    marker_response_tlv_template, 1))
			goto bad;

		LACP_DPRINTF((lp, "marker response, port=%u, sys=%s, id=%u\n",
		    ntohs(mdu->mdu_info.mi_rq_port),
		    ether_sprintf(mdu->mdu_info.mi_rq_system),
		    ntohl(mdu->mdu_info.mi_rq_xid)));

		/* Verify that it is the last marker we sent out */
		if (memcmp(&mdu->mdu_info, &lp->lp_marker,
		    sizeof(struct lacp_markerinfo)))
			goto bad;

		lp->lp_flags &= ~LACP_PORT_MARK;

		if (lsc->lsc_suppress_distributing) {
			/* Check if any ports are waiting for a response */
			LIST_FOREACH(lp2, &lsc->lsc_ports, lp_next) {
				if (lp2->lp_flags & LACP_PORT_MARK) {
					pending = 1;
					break;
				}
			}

			if (pending == 0) {
				/* All interface queues are clear */
				LACP_DPRINTF((NULL, "queue flush complete\n"));
				lsc->lsc_suppress_distributing = 0;
			}
		}
		break;

	default:
		goto bad;
	}

	m_freem(m);
	return (error);

bad:
	LACP_DPRINTF((lp, "bad marker frame\n"));
	m_freem(m);
	return (EINVAL);
}

int
tlv_check(const void *p, size_t size, const struct tlvhdr *tlv,
    const struct tlv_template *tmpl, int check_type)
{
	while (/* CONSTCOND */ 1) {
		if ((const char *)tlv - (const char *)p + sizeof(*tlv) > size)
			return (EINVAL);

		if ((check_type && tlv->tlv_type != tmpl->tmpl_type) ||
		    tlv->tlv_length != tmpl->tmpl_length)
			return (EINVAL);

		if (tmpl->tmpl_type == 0)
			break;

		tlv = (const struct tlvhdr *)
		    ((const char *)tlv + tlv->tlv_length);
		tmpl++;
	}

	return (0);
}

#if defined(LACP_DEBUG)
const char *
lacp_format_mac(const u_int8_t *mac, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%02X-%02X-%02X-%02X-%02X-%02X",
	    (int)mac[0],
	    (int)mac[1],
	    (int)mac[2],
	    (int)mac[3],
	    (int)mac[4],
	    (int)mac[5]);

	return (buf);
}

const char *
lacp_format_systemid(const struct lacp_systemid *sysid,
    char *buf, size_t buflen)
{
	char macbuf[LACP_MACSTR_MAX+1];

	snprintf(buf, buflen, "%04X,%s",
	    ntohs(sysid->lsi_prio),
	    lacp_format_mac(sysid->lsi_mac, macbuf, sizeof(macbuf)));

	return (buf);
}

const char *
lacp_format_portid(const struct lacp_portid *portid, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%04X,%04X",
	    ntohs(portid->lpi_prio),
	    ntohs(portid->lpi_portno));

	return (buf);
}

const char *
lacp_format_partner(const struct lacp_peerinfo *peer, char *buf, size_t buflen)
{
	char sysid[LACP_SYSTEMIDSTR_MAX+1];
	char portid[LACP_PORTIDSTR_MAX+1];

	snprintf(buf, buflen, "(%s,%04X,%s)",
	    lacp_format_systemid(&peer->lip_systemid, sysid, sizeof(sysid)),
	    ntohs(peer->lip_key),
	    lacp_format_portid(&peer->lip_portid, portid, sizeof(portid)));

	return (buf);
}

const char *
lacp_format_lagid(const struct lacp_peerinfo *a,
    const struct lacp_peerinfo *b, char *buf, size_t buflen)
{
	char astr[LACP_PARTNERSTR_MAX+1];
	char bstr[LACP_PARTNERSTR_MAX+1];

#if 0
	/*
	 * there's a convention to display small numbered peer
	 * in the left.
	 */
	if (lacp_compare_peerinfo(a, b) > 0) {
		const struct lacp_peerinfo *t;

		t = a;
		a = b;
		b = t;
	}
#endif

	snprintf(buf, buflen, "[%s,%s]",
	    lacp_format_partner(a, astr, sizeof(astr)),
	    lacp_format_partner(b, bstr, sizeof(bstr)));

	return (buf);
}

const char *
lacp_format_lagid_aggregator(const struct lacp_aggregator *la,
    char *buf, size_t buflen)
{
	if (la == NULL)
		return ("(none)");

	return (lacp_format_lagid(&la->la_actor, &la->la_partner, buf, buflen));
}

const char *
lacp_format_state(u_int8_t state, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%b", state, LACP_STATE_BITS);
	return (buf);
}

void
lacp_dump_lacpdu(const struct lacpdu *du)
{
	char buf[LACP_PARTNERSTR_MAX+1];
	char buf2[LACP_STATESTR_MAX+1];

	printf("actor=%s\n",
	    lacp_format_partner(&du->ldu_actor, buf, sizeof(buf)));
	printf("actor.state=%s\n",
	    lacp_format_state(du->ldu_actor.lip_state, buf2, sizeof(buf2)));
	printf("partner=%s\n",
	    lacp_format_partner(&du->ldu_partner, buf, sizeof(buf)));
	printf("partner.state=%s\n",
	    lacp_format_state(du->ldu_partner.lip_state, buf2, sizeof(buf2)));

	printf("maxdelay=%d\n", ntohs(du->ldu_collector.lci_maxdelay));
}

void
lacp_dprintf(const struct lacp_port *lp, const char *fmt, ...)
{
	va_list va;

	if (lp)
		printf("%s: ", lp->lp_ifp->if_xname);

	va_start(va, fmt);
	vprintf(fmt, va);
	va_end(va);
}
#endif

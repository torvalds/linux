/*	$NetBSD: ieee8023ad_lacp.c,v 1.3 2005/12/11 12:24:54 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ratelimit.h"

#include <sys/param.h>
#include <sys/callout.h>
#include <sys/eventhandler.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h> /* hz */
#include <sys/socket.h> /* for net/if.h */
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <machine/stdarg.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/taskqueue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/ethernet.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net/if_lagg.h>
#include <net/ieee8023ad_lacp.h>

/*
 * actor system priority and port priority.
 * XXX should be configurable.
 */

#define	LACP_SYSTEM_PRIO	0x8000
#define	LACP_PORT_PRIO		0x8000

const uint8_t ethermulticastaddr_slowprotocols[ETHER_ADDR_LEN] =
    { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x02 };

static const struct tlv_template lacp_info_tlv_template[] = {
	{ LACP_TYPE_ACTORINFO,
	    sizeof(struct tlvhdr) + sizeof(struct lacp_peerinfo) },
	{ LACP_TYPE_PARTNERINFO,
	    sizeof(struct tlvhdr) + sizeof(struct lacp_peerinfo) },
	{ LACP_TYPE_COLLECTORINFO,
	    sizeof(struct tlvhdr) + sizeof(struct lacp_collectorinfo) },
	{ 0, 0 },
};

static const struct tlv_template marker_info_tlv_template[] = {
	{ MARKER_TYPE_INFO,
	    sizeof(struct tlvhdr) + sizeof(struct lacp_markerinfo) },
	{ 0, 0 },
};

static const struct tlv_template marker_response_tlv_template[] = {
	{ MARKER_TYPE_RESPONSE,
	    sizeof(struct tlvhdr) + sizeof(struct lacp_markerinfo) },
	{ 0, 0 },
};

typedef void (*lacp_timer_func_t)(struct lacp_port *);

static void	lacp_fill_actorinfo(struct lacp_port *, struct lacp_peerinfo *);
static void	lacp_fill_markerinfo(struct lacp_port *,
		    struct lacp_markerinfo *);

static uint64_t	lacp_aggregator_bandwidth(struct lacp_aggregator *);
static void	lacp_suppress_distributing(struct lacp_softc *,
		    struct lacp_aggregator *);
static void	lacp_transit_expire(void *);
static void	lacp_update_portmap(struct lacp_softc *);
static void	lacp_select_active_aggregator(struct lacp_softc *);
static uint16_t	lacp_compose_key(struct lacp_port *);
static int	tlv_check(const void *, size_t, const struct tlvhdr *,
		    const struct tlv_template *, boolean_t);
static void	lacp_tick(void *);

static void	lacp_fill_aggregator_id(struct lacp_aggregator *,
		    const struct lacp_port *);
static void	lacp_fill_aggregator_id_peer(struct lacp_peerinfo *,
		    const struct lacp_peerinfo *);
static int	lacp_aggregator_is_compatible(const struct lacp_aggregator *,
		    const struct lacp_port *);
static int	lacp_peerinfo_is_compatible(const struct lacp_peerinfo *,
		    const struct lacp_peerinfo *);

static struct lacp_aggregator *lacp_aggregator_get(struct lacp_softc *,
		    struct lacp_port *);
static void	lacp_aggregator_addref(struct lacp_softc *,
		    struct lacp_aggregator *);
static void	lacp_aggregator_delref(struct lacp_softc *,
		    struct lacp_aggregator *);

/* receive machine */

static int	lacp_pdu_input(struct lacp_port *, struct mbuf *);
static int	lacp_marker_input(struct lacp_port *, struct mbuf *);
static void	lacp_sm_rx(struct lacp_port *, const struct lacpdu *);
static void	lacp_sm_rx_timer(struct lacp_port *);
static void	lacp_sm_rx_set_expired(struct lacp_port *);
static void	lacp_sm_rx_update_ntt(struct lacp_port *,
		    const struct lacpdu *);
static void	lacp_sm_rx_record_pdu(struct lacp_port *,
		    const struct lacpdu *);
static void	lacp_sm_rx_update_selected(struct lacp_port *,
		    const struct lacpdu *);
static void	lacp_sm_rx_record_default(struct lacp_port *);
static void	lacp_sm_rx_update_default_selected(struct lacp_port *);
static void	lacp_sm_rx_update_selected_from_peerinfo(struct lacp_port *,
		    const struct lacp_peerinfo *);

/* mux machine */

static void	lacp_sm_mux(struct lacp_port *);
static void	lacp_set_mux(struct lacp_port *, enum lacp_mux_state);
static void	lacp_sm_mux_timer(struct lacp_port *);

/* periodic transmit machine */

static void	lacp_sm_ptx_update_timeout(struct lacp_port *, uint8_t);
static void	lacp_sm_ptx_tx_schedule(struct lacp_port *);
static void	lacp_sm_ptx_timer(struct lacp_port *);

/* transmit machine */

static void	lacp_sm_tx(struct lacp_port *);
static void	lacp_sm_assert_ntt(struct lacp_port *);

static void	lacp_run_timers(struct lacp_port *);
static int	lacp_compare_peerinfo(const struct lacp_peerinfo *,
		    const struct lacp_peerinfo *);
static int	lacp_compare_systemid(const struct lacp_systemid *,
		    const struct lacp_systemid *);
static void	lacp_port_enable(struct lacp_port *);
static void	lacp_port_disable(struct lacp_port *);
static void	lacp_select(struct lacp_port *);
static void	lacp_unselect(struct lacp_port *);
static void	lacp_disable_collecting(struct lacp_port *);
static void	lacp_enable_collecting(struct lacp_port *);
static void	lacp_disable_distributing(struct lacp_port *);
static void	lacp_enable_distributing(struct lacp_port *);
static int	lacp_xmit_lacpdu(struct lacp_port *);
static int	lacp_xmit_marker(struct lacp_port *);

/* Debugging */

static void	lacp_dump_lacpdu(const struct lacpdu *);
static const char *lacp_format_partner(const struct lacp_peerinfo *, char *,
		    size_t);
static const char *lacp_format_lagid(const struct lacp_peerinfo *,
		    const struct lacp_peerinfo *, char *, size_t);
static const char *lacp_format_lagid_aggregator(const struct lacp_aggregator *,
		    char *, size_t);
static const char *lacp_format_state(uint8_t, char *, size_t);
static const char *lacp_format_mac(const uint8_t *, char *, size_t);
static const char *lacp_format_systemid(const struct lacp_systemid *, char *,
		    size_t);
static const char *lacp_format_portid(const struct lacp_portid *, char *,
		    size_t);
static void	lacp_dprintf(const struct lacp_port *, const char *, ...)
		    __attribute__((__format__(__printf__, 2, 3)));

VNET_DEFINE_STATIC(int, lacp_debug);
#define	V_lacp_debug	VNET(lacp_debug)
SYSCTL_NODE(_net_link_lagg, OID_AUTO, lacp, CTLFLAG_RD, 0, "ieee802.3ad");
SYSCTL_INT(_net_link_lagg_lacp, OID_AUTO, debug, CTLFLAG_RWTUN | CTLFLAG_VNET,
    &VNET_NAME(lacp_debug), 0, "Enable LACP debug logging (1=debug, 2=trace)");

VNET_DEFINE_STATIC(int, lacp_default_strict_mode) = 1;
SYSCTL_INT(_net_link_lagg_lacp, OID_AUTO, default_strict_mode,
    CTLFLAG_RWTUN | CTLFLAG_VNET, &VNET_NAME(lacp_default_strict_mode), 0,
    "LACP strict protocol compliance default");

#define LACP_DPRINTF(a) if (V_lacp_debug & 0x01) { lacp_dprintf a ; }
#define LACP_TRACE(a) if (V_lacp_debug & 0x02) { lacp_dprintf(a,"%s\n",__func__); }
#define LACP_TPRINTF(a) if (V_lacp_debug & 0x04) { lacp_dprintf a ; }

/*
 * partner administration variables.
 * XXX should be configurable.
 */

static const struct lacp_peerinfo lacp_partner_admin_optimistic = {
	.lip_systemid = { .lsi_prio = 0xffff },
	.lip_portid = { .lpi_prio = 0xffff },
	.lip_state = LACP_STATE_SYNC | LACP_STATE_AGGREGATION |
	    LACP_STATE_COLLECTING | LACP_STATE_DISTRIBUTING,
};

static const struct lacp_peerinfo lacp_partner_admin_strict = {
	.lip_systemid = { .lsi_prio = 0xffff },
	.lip_portid = { .lpi_prio = 0xffff },
	.lip_state = 0,
};

static const lacp_timer_func_t lacp_timer_funcs[LACP_NTIMER] = {
	[LACP_TIMER_CURRENT_WHILE] = lacp_sm_rx_timer,
	[LACP_TIMER_PERIODIC] = lacp_sm_ptx_timer,
	[LACP_TIMER_WAIT_WHILE] = lacp_sm_mux_timer,
};

struct mbuf *
lacp_input(struct lagg_port *lgp, struct mbuf *m)
{
	struct lacp_port *lp = LACP_PORT(lgp);
	uint8_t subtype;

	if (m->m_pkthdr.len < sizeof(struct ether_header) + sizeof(subtype)) {
		m_freem(m);
		return (NULL);
	}

	m_copydata(m, sizeof(struct ether_header), sizeof(subtype), &subtype);
	switch (subtype) {
		case SLOWPROTOCOLS_SUBTYPE_LACP:
			lacp_pdu_input(lp, m);
			return (NULL);

		case SLOWPROTOCOLS_SUBTYPE_MARKER:
			lacp_marker_input(lp, m);
			return (NULL);
	}

	/* Not a subtype we are interested in */
	return (m);
}

/*
 * lacp_pdu_input: process lacpdu
 */
static int
lacp_pdu_input(struct lacp_port *lp, struct mbuf *m)
{
	struct lacp_softc *lsc = lp->lp_lsc;
	struct lacpdu *du;
	int error = 0;

	if (m->m_pkthdr.len != sizeof(*du)) {
		goto bad;
	}

	if ((m->m_flags & M_MCAST) == 0) {
		goto bad;
	}

	if (m->m_len < sizeof(*du)) {
		m = m_pullup(m, sizeof(*du));
		if (m == NULL) {
			return (ENOMEM);
		}
	}

	du = mtod(m, struct lacpdu *);

	if (memcmp(&du->ldu_eh.ether_dhost,
	    &ethermulticastaddr_slowprotocols, ETHER_ADDR_LEN)) {
		goto bad;
	}

	/*
	 * ignore the version for compatibility with
	 * the future protocol revisions.
	 */
#if 0
	if (du->ldu_sph.sph_version != 1) {
		goto bad;
	}
#endif

	/*
	 * ignore tlv types for compatibility with
	 * the future protocol revisions.
	 */
	if (tlv_check(du, sizeof(*du), &du->ldu_tlv_actor,
	    lacp_info_tlv_template, FALSE)) {
		goto bad;
	}

        if (V_lacp_debug > 0) {
		lacp_dprintf(lp, "lacpdu receive\n");
		lacp_dump_lacpdu(du);
	}

	if ((1 << lp->lp_ifp->if_dunit) & lp->lp_lsc->lsc_debug.lsc_rx_test) {
		LACP_TPRINTF((lp, "Dropping RX PDU\n"));
		goto bad;
	}

	LACP_LOCK(lsc);
	lacp_sm_rx(lp, du);
	LACP_UNLOCK(lsc);

	m_freem(m);
	return (error);

bad:
	m_freem(m);
	return (EINVAL);
}

static void
lacp_fill_actorinfo(struct lacp_port *lp, struct lacp_peerinfo *info)
{
	struct lagg_port *lgp = lp->lp_lagg;
	struct lagg_softc *sc = lgp->lp_softc;

	info->lip_systemid.lsi_prio = htons(LACP_SYSTEM_PRIO);
	memcpy(&info->lip_systemid.lsi_mac,
	    IF_LLADDR(sc->sc_ifp), ETHER_ADDR_LEN);
	info->lip_portid.lpi_prio = htons(LACP_PORT_PRIO);
	info->lip_portid.lpi_portno = htons(lp->lp_ifp->if_index);
	info->lip_state = lp->lp_state;
}

static void
lacp_fill_markerinfo(struct lacp_port *lp, struct lacp_markerinfo *info)
{
	struct ifnet *ifp = lp->lp_ifp;

	/* Fill in the port index and system id (encoded as the MAC) */
	info->mi_rq_port = htons(ifp->if_index);
	memcpy(&info->mi_rq_system, lp->lp_systemid.lsi_mac, ETHER_ADDR_LEN);
	info->mi_rq_xid = htonl(0);
}

static int
lacp_xmit_lacpdu(struct lacp_port *lp)
{
	struct lagg_port *lgp = lp->lp_lagg;
	struct mbuf *m;
	struct lacpdu *du;
	int error;

	LACP_LOCK_ASSERT(lp->lp_lsc);

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL) {
		return (ENOMEM);
	}
	m->m_len = m->m_pkthdr.len = sizeof(*du);

	du = mtod(m, struct lacpdu *);
	memset(du, 0, sizeof(*du));

	memcpy(&du->ldu_eh.ether_dhost, ethermulticastaddr_slowprotocols,
	    ETHER_ADDR_LEN);
	memcpy(&du->ldu_eh.ether_shost, lgp->lp_lladdr, ETHER_ADDR_LEN);
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

	if (V_lacp_debug > 0) {
		lacp_dprintf(lp, "lacpdu transmit\n");
		lacp_dump_lacpdu(du);
	}

	m->m_flags |= M_MCAST;

	/*
	 * XXX should use higher priority queue.
	 * otherwise network congestion can break aggregation.
	 */

	error = lagg_enqueue(lp->lp_ifp, m);
	return (error);
}

static int
lacp_xmit_marker(struct lacp_port *lp)
{
	struct lagg_port *lgp = lp->lp_lagg;
	struct mbuf *m;
	struct markerdu *mdu;
	int error;

	LACP_LOCK_ASSERT(lp->lp_lsc);

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL) {
		return (ENOMEM);
	}
	m->m_len = m->m_pkthdr.len = sizeof(*mdu);

	mdu = mtod(m, struct markerdu *);
	memset(mdu, 0, sizeof(*mdu));

	memcpy(&mdu->mdu_eh.ether_dhost, ethermulticastaddr_slowprotocols,
	    ETHER_ADDR_LEN);
	memcpy(&mdu->mdu_eh.ether_shost, lgp->lp_lladdr, ETHER_ADDR_LEN);
	mdu->mdu_eh.ether_type = htons(ETHERTYPE_SLOW);

	mdu->mdu_sph.sph_subtype = SLOWPROTOCOLS_SUBTYPE_MARKER;
	mdu->mdu_sph.sph_version = 1;

	/* Bump the transaction id and copy over the marker info */
	lp->lp_marker.mi_rq_xid = htonl(ntohl(lp->lp_marker.mi_rq_xid) + 1);
	TLV_SET(&mdu->mdu_tlv, MARKER_TYPE_INFO, sizeof(mdu->mdu_info));
	mdu->mdu_info = lp->lp_marker;

	LACP_DPRINTF((lp, "marker transmit, port=%u, sys=%6D, id=%u\n",
	    ntohs(mdu->mdu_info.mi_rq_port), mdu->mdu_info.mi_rq_system, ":",
	    ntohl(mdu->mdu_info.mi_rq_xid)));

	m->m_flags |= M_MCAST;
	error = lagg_enqueue(lp->lp_ifp, m);
	return (error);
}

void
lacp_linkstate(struct lagg_port *lgp)
{
	struct lacp_port *lp = LACP_PORT(lgp);
	struct lacp_softc *lsc = lp->lp_lsc;
	struct ifnet *ifp = lgp->lp_ifp;
	struct ifmediareq ifmr;
	int error = 0;
	u_int media;
	uint8_t old_state;
	uint16_t old_key;

	bzero((char *)&ifmr, sizeof(ifmr));
	error = (*ifp->if_ioctl)(ifp, SIOCGIFXMEDIA, (caddr_t)&ifmr);
	if (error != 0) {
		bzero((char *)&ifmr, sizeof(ifmr));
		error = (*ifp->if_ioctl)(ifp, SIOCGIFMEDIA, (caddr_t)&ifmr);
	}
	if (error != 0)
		return;

	LACP_LOCK(lsc);
	media = ifmr.ifm_active;
	LACP_DPRINTF((lp, "media changed 0x%x -> 0x%x, ether = %d, fdx = %d, "
	    "link = %d\n", lp->lp_media, media, IFM_TYPE(media) == IFM_ETHER,
	    (media & IFM_FDX) != 0, ifp->if_link_state == LINK_STATE_UP));
	old_state = lp->lp_state;
	old_key = lp->lp_key;

	lp->lp_media = media;
	/*
	 * If the port is not an active full duplex Ethernet link then it can
	 * not be aggregated.
	 */
	if (IFM_TYPE(media) != IFM_ETHER || (media & IFM_FDX) == 0 ||
	    ifp->if_link_state != LINK_STATE_UP) {
		lacp_port_disable(lp);
	} else {
		lacp_port_enable(lp);
	}
	lp->lp_key = lacp_compose_key(lp);

	if (old_state != lp->lp_state || old_key != lp->lp_key) {
		LACP_DPRINTF((lp, "-> UNSELECTED\n"));
		lp->lp_selected = LACP_UNSELECTED;
	}
	LACP_UNLOCK(lsc);
}

static void
lacp_tick(void *arg)
{
	struct lacp_softc *lsc = arg;
	struct lacp_port *lp;

	LIST_FOREACH(lp, &lsc->lsc_ports, lp_next) {
		if ((lp->lp_state & LACP_STATE_AGGREGATION) == 0)
			continue;

		CURVNET_SET(lp->lp_ifp->if_vnet);
		lacp_run_timers(lp);

		lacp_select(lp);
		lacp_sm_mux(lp);
		lacp_sm_tx(lp);
		lacp_sm_ptx_tx_schedule(lp);
		CURVNET_RESTORE();
	}
	callout_reset(&lsc->lsc_callout, hz, lacp_tick, lsc);
}

int
lacp_port_create(struct lagg_port *lgp)
{
	struct lagg_softc *sc = lgp->lp_softc;
	struct lacp_softc *lsc = LACP_SOFTC(sc);
	struct lacp_port *lp;
	struct ifnet *ifp = lgp->lp_ifp;
	struct sockaddr_dl sdl;
	struct ifmultiaddr *rifma = NULL;
	int error;

	link_init_sdl(ifp, (struct sockaddr *)&sdl, IFT_ETHER);
	sdl.sdl_alen = ETHER_ADDR_LEN;

	bcopy(&ethermulticastaddr_slowprotocols,
	    LLADDR(&sdl), ETHER_ADDR_LEN);
	error = if_addmulti(ifp, (struct sockaddr *)&sdl, &rifma);
	if (error) {
		printf("%s: ADDMULTI failed on %s\n", __func__,
		    lgp->lp_ifp->if_xname);
		return (error);
	}

	lp = malloc(sizeof(struct lacp_port),
	    M_DEVBUF, M_NOWAIT|M_ZERO);
	if (lp == NULL)
		return (ENOMEM);

	LACP_LOCK(lsc);
	lgp->lp_psc = lp;
	lp->lp_ifp = ifp;
	lp->lp_lagg = lgp;
	lp->lp_lsc = lsc;
	lp->lp_ifma = rifma;

	LIST_INSERT_HEAD(&lsc->lsc_ports, lp, lp_next);

	lacp_fill_actorinfo(lp, &lp->lp_actor);
	lacp_fill_markerinfo(lp, &lp->lp_marker);
	lp->lp_state = LACP_STATE_ACTIVITY;
	lp->lp_aggregator = NULL;
	lacp_sm_rx_set_expired(lp);
	LACP_UNLOCK(lsc);
	lacp_linkstate(lgp);

	return (0);
}

void
lacp_port_destroy(struct lagg_port *lgp)
{
	struct lacp_port *lp = LACP_PORT(lgp);
	struct lacp_softc *lsc = lp->lp_lsc;
	int i;

	LACP_LOCK(lsc);
	for (i = 0; i < LACP_NTIMER; i++) {
		LACP_TIMER_DISARM(lp, i);
	}

	lacp_disable_collecting(lp);
	lacp_disable_distributing(lp);
	lacp_unselect(lp);

	LIST_REMOVE(lp, lp_next);
	LACP_UNLOCK(lsc);

	/* The address may have already been removed by if_purgemaddrs() */
	if (!lgp->lp_detaching)
		if_delmulti_ifma(lp->lp_ifma);

	free(lp, M_DEVBUF);
}

void
lacp_req(struct lagg_softc *sc, void *data)
{
	struct lacp_opreq *req = (struct lacp_opreq *)data;
	struct lacp_softc *lsc = LACP_SOFTC(sc);
	struct lacp_aggregator *la;

	bzero(req, sizeof(struct lacp_opreq));
	
	/*
	 * If the LACP softc is NULL, return with the opreq structure full of
	 * zeros.  It is normal for the softc to be NULL while the lagg is
	 * being destroyed.
	 */
	if (NULL == lsc)
		return;

	la = lsc->lsc_active_aggregator;
	LACP_LOCK(lsc);
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
		req->partner_portprio = ntohs(la->la_partner.lip_portid.lpi_prio);
		req->partner_portno = ntohs(la->la_partner.lip_portid.lpi_portno);
		req->partner_state = la->la_partner.lip_state;
	}
	LACP_UNLOCK(lsc);
}

void
lacp_portreq(struct lagg_port *lgp, void *data)
{
	struct lacp_opreq *req = (struct lacp_opreq *)data;
	struct lacp_port *lp = LACP_PORT(lgp);
	struct lacp_softc *lsc = lp->lp_lsc;

	LACP_LOCK(lsc);
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
	LACP_UNLOCK(lsc);
}

static void
lacp_disable_collecting(struct lacp_port *lp)
{
	LACP_DPRINTF((lp, "collecting disabled\n"));
	lp->lp_state &= ~LACP_STATE_COLLECTING;
}

static void
lacp_enable_collecting(struct lacp_port *lp)
{
	LACP_DPRINTF((lp, "collecting enabled\n"));
	lp->lp_state |= LACP_STATE_COLLECTING;
}

static void
lacp_disable_distributing(struct lacp_port *lp)
{
	struct lacp_aggregator *la = lp->lp_aggregator;
	struct lacp_softc *lsc = lp->lp_lsc;
	struct lagg_softc *sc = lsc->lsc_softc;
	char buf[LACP_LAGIDSTR_MAX+1];

	LACP_LOCK_ASSERT(lsc);

	if (la == NULL || (lp->lp_state & LACP_STATE_DISTRIBUTING) == 0) {
		return;
	}

	KASSERT(!TAILQ_EMPTY(&la->la_ports), ("no aggregator ports"));
	KASSERT(la->la_nports > 0, ("nports invalid (%d)", la->la_nports));
	KASSERT(la->la_refcnt >= la->la_nports, ("aggregator refcnt invalid"));

	LACP_DPRINTF((lp, "disable distributing on aggregator %s, "
	    "nports %d -> %d\n",
	    lacp_format_lagid_aggregator(la, buf, sizeof(buf)),
	    la->la_nports, la->la_nports - 1));

	TAILQ_REMOVE(&la->la_ports, lp, lp_dist_q);
	la->la_nports--;
	sc->sc_active = la->la_nports;

	if (lsc->lsc_active_aggregator == la) {
		lacp_suppress_distributing(lsc, la);
		lacp_select_active_aggregator(lsc);
		/* regenerate the port map, the active aggregator has changed */
		lacp_update_portmap(lsc);
	}

	lp->lp_state &= ~LACP_STATE_DISTRIBUTING;
	if_link_state_change(sc->sc_ifp,
	    sc->sc_active ? LINK_STATE_UP : LINK_STATE_DOWN);
}

static void
lacp_enable_distributing(struct lacp_port *lp)
{
	struct lacp_aggregator *la = lp->lp_aggregator;
	struct lacp_softc *lsc = lp->lp_lsc;
	struct lagg_softc *sc = lsc->lsc_softc;
	char buf[LACP_LAGIDSTR_MAX+1];

	LACP_LOCK_ASSERT(lsc);

	if ((lp->lp_state & LACP_STATE_DISTRIBUTING) != 0) {
		return;
	}

	LACP_DPRINTF((lp, "enable distributing on aggregator %s, "
	    "nports %d -> %d\n",
	    lacp_format_lagid_aggregator(la, buf, sizeof(buf)),
	    la->la_nports, la->la_nports + 1));

	KASSERT(la->la_refcnt > la->la_nports, ("aggregator refcnt invalid"));
	TAILQ_INSERT_HEAD(&la->la_ports, lp, lp_dist_q);
	la->la_nports++;
	sc->sc_active = la->la_nports;

	lp->lp_state |= LACP_STATE_DISTRIBUTING;

	if (lsc->lsc_active_aggregator == la) {
		lacp_suppress_distributing(lsc, la);
		lacp_update_portmap(lsc);
	} else
		/* try to become the active aggregator */
		lacp_select_active_aggregator(lsc);

	if_link_state_change(sc->sc_ifp,
	    sc->sc_active ? LINK_STATE_UP : LINK_STATE_DOWN);
}

static void
lacp_transit_expire(void *vp)
{
	struct lacp_softc *lsc = vp;

	LACP_LOCK_ASSERT(lsc);

	CURVNET_SET(lsc->lsc_softc->sc_ifp->if_vnet);
	LACP_TRACE(NULL);
	CURVNET_RESTORE();

	lsc->lsc_suppress_distributing = FALSE;
}

void
lacp_attach(struct lagg_softc *sc)
{
	struct lacp_softc *lsc;

	lsc = malloc(sizeof(struct lacp_softc), M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_psc = lsc;
	lsc->lsc_softc = sc;

	lsc->lsc_hashkey = m_ether_tcpip_hash_init();
	lsc->lsc_active_aggregator = NULL;
	lsc->lsc_strict_mode = VNET(lacp_default_strict_mode);
	LACP_LOCK_INIT(lsc);
	TAILQ_INIT(&lsc->lsc_aggregators);
	LIST_INIT(&lsc->lsc_ports);

	callout_init_mtx(&lsc->lsc_transit_callout, &lsc->lsc_mtx, 0);
	callout_init_mtx(&lsc->lsc_callout, &lsc->lsc_mtx, 0);

	/* if the lagg is already up then do the same */
	if (sc->sc_ifp->if_drv_flags & IFF_DRV_RUNNING)
		lacp_init(sc);
}

void
lacp_detach(void *psc)
{
	struct lacp_softc *lsc = (struct lacp_softc *)psc;

	KASSERT(TAILQ_EMPTY(&lsc->lsc_aggregators),
	    ("aggregators still active"));
	KASSERT(lsc->lsc_active_aggregator == NULL,
	    ("aggregator still attached"));

	callout_drain(&lsc->lsc_transit_callout);
	callout_drain(&lsc->lsc_callout);

	LACP_LOCK_DESTROY(lsc);
	free(lsc, M_DEVBUF);
}

void
lacp_init(struct lagg_softc *sc)
{
	struct lacp_softc *lsc = LACP_SOFTC(sc);

	LACP_LOCK(lsc);
	callout_reset(&lsc->lsc_callout, hz, lacp_tick, lsc);
	LACP_UNLOCK(lsc);
}

void
lacp_stop(struct lagg_softc *sc)
{
	struct lacp_softc *lsc = LACP_SOFTC(sc);

	LACP_LOCK(lsc);
	callout_stop(&lsc->lsc_transit_callout);
	callout_stop(&lsc->lsc_callout);
	LACP_UNLOCK(lsc);
}

struct lagg_port *
lacp_select_tx_port(struct lagg_softc *sc, struct mbuf *m)
{
	struct lacp_softc *lsc = LACP_SOFTC(sc);
	struct lacp_portmap *pm;
	struct lacp_port *lp;
	uint32_t hash;

	if (__predict_false(lsc->lsc_suppress_distributing)) {
		LACP_DPRINTF((NULL, "%s: waiting transit\n", __func__));
		return (NULL);
	}

	pm = &lsc->lsc_pmap[lsc->lsc_activemap];
	if (pm->pm_count == 0) {
		LACP_DPRINTF((NULL, "%s: no active aggregator\n", __func__));
		return (NULL);
	}

	if ((sc->sc_opts & LAGG_OPT_USE_FLOWID) &&
	    M_HASHTYPE_GET(m) != M_HASHTYPE_NONE)
		hash = m->m_pkthdr.flowid >> sc->flowid_shift;
	else
		hash = m_ether_tcpip_hash(sc->sc_flags, m, lsc->lsc_hashkey);
	hash %= pm->pm_count;
	lp = pm->pm_map[hash];

	KASSERT((lp->lp_state & LACP_STATE_DISTRIBUTING) != 0,
	    ("aggregated port is not distributing"));

	return (lp->lp_lagg);
}

#ifdef RATELIMIT
struct lagg_port *
lacp_select_tx_port_by_hash(struct lagg_softc *sc, uint32_t flowid)
{
	struct lacp_softc *lsc = LACP_SOFTC(sc);
	struct lacp_portmap *pm;
	struct lacp_port *lp;
	uint32_t hash;

	if (__predict_false(lsc->lsc_suppress_distributing)) {
		LACP_DPRINTF((NULL, "%s: waiting transit\n", __func__));
		return (NULL);
	}

	pm = &lsc->lsc_pmap[lsc->lsc_activemap];
	if (pm->pm_count == 0) {
		LACP_DPRINTF((NULL, "%s: no active aggregator\n", __func__));
		return (NULL);
	}

	hash = flowid >> sc->flowid_shift;
	hash %= pm->pm_count;
	lp = pm->pm_map[hash];

	return (lp->lp_lagg);
}
#endif

/*
 * lacp_suppress_distributing: drop transmit packets for a while
 * to preserve packet ordering.
 */

static void
lacp_suppress_distributing(struct lacp_softc *lsc, struct lacp_aggregator *la)
{
	struct lacp_port *lp;

	if (lsc->lsc_active_aggregator != la) {
		return;
	}

	LACP_TRACE(NULL);

	lsc->lsc_suppress_distributing = TRUE;

	/* send a marker frame down each port to verify the queues are empty */
	LIST_FOREACH(lp, &lsc->lsc_ports, lp_next) {
		lp->lp_flags |= LACP_PORT_MARK;
		lacp_xmit_marker(lp);
	}

	/* set a timeout for the marker frames */
	callout_reset(&lsc->lsc_transit_callout,
	    LACP_TRANSIT_DELAY * hz / 1000, lacp_transit_expire, lsc);
}

static int
lacp_compare_peerinfo(const struct lacp_peerinfo *a,
    const struct lacp_peerinfo *b)
{
	return (memcmp(a, b, offsetof(struct lacp_peerinfo, lip_state)));
}

static int
lacp_compare_systemid(const struct lacp_systemid *a,
    const struct lacp_systemid *b)
{
	return (memcmp(a, b, sizeof(*a)));
}

#if 0	/* unused */
static int
lacp_compare_portid(const struct lacp_portid *a,
    const struct lacp_portid *b)
{
	return (memcmp(a, b, sizeof(*a)));
}
#endif

static uint64_t
lacp_aggregator_bandwidth(struct lacp_aggregator *la)
{
	struct lacp_port *lp;
	uint64_t speed;

	lp = TAILQ_FIRST(&la->la_ports);
	if (lp == NULL) {
		return (0);
	}

	speed = ifmedia_baudrate(lp->lp_media);
	speed *= la->la_nports;
	if (speed == 0) {
		LACP_DPRINTF((lp, "speed 0? media=0x%x nports=%d\n",
		    lp->lp_media, la->la_nports));
	}

	return (speed);
}

/*
 * lacp_select_active_aggregator: select an aggregator to be used to transmit
 * packets from lagg(4) interface.
 */

static void
lacp_select_active_aggregator(struct lacp_softc *lsc)
{
	struct lacp_aggregator *la;
	struct lacp_aggregator *best_la = NULL;
	uint64_t best_speed = 0;
	char buf[LACP_LAGIDSTR_MAX+1];

	LACP_TRACE(NULL);

	TAILQ_FOREACH(la, &lsc->lsc_aggregators, la_q) {
		uint64_t speed;

		if (la->la_nports == 0) {
			continue;
		}

		speed = lacp_aggregator_bandwidth(la);
		LACP_DPRINTF((NULL, "%s, speed=%jd, nports=%d\n",
		    lacp_format_lagid_aggregator(la, buf, sizeof(buf)),
		    speed, la->la_nports));

		/*
		 * This aggregator is chosen if the partner has a better
		 * system priority or, the total aggregated speed is higher
		 * or, it is already the chosen aggregator
		 */
		if ((best_la != NULL && LACP_SYS_PRI(la->la_partner) <
		    LACP_SYS_PRI(best_la->la_partner)) ||
		    speed > best_speed ||
		    (speed == best_speed &&
		    la == lsc->lsc_active_aggregator)) {
			best_la = la;
			best_speed = speed;
		}
	}

	KASSERT(best_la == NULL || best_la->la_nports > 0,
	    ("invalid aggregator refcnt"));
	KASSERT(best_la == NULL || !TAILQ_EMPTY(&best_la->la_ports),
	    ("invalid aggregator list"));

	if (lsc->lsc_active_aggregator != best_la) {
		LACP_DPRINTF((NULL, "active aggregator changed\n"));
		LACP_DPRINTF((NULL, "old %s\n",
		    lacp_format_lagid_aggregator(lsc->lsc_active_aggregator,
		    buf, sizeof(buf))));
	} else {
		LACP_DPRINTF((NULL, "active aggregator not changed\n"));
	}
	LACP_DPRINTF((NULL, "new %s\n",
	    lacp_format_lagid_aggregator(best_la, buf, sizeof(buf))));

	if (lsc->lsc_active_aggregator != best_la) {
		lsc->lsc_active_aggregator = best_la;
		lacp_update_portmap(lsc);
		if (best_la) {
			lacp_suppress_distributing(lsc, best_la);
		}
	}
}

/*
 * Updated the inactive portmap array with the new list of ports and
 * make it live.
 */
static void
lacp_update_portmap(struct lacp_softc *lsc)
{
	struct lagg_softc *sc = lsc->lsc_softc;
	struct lacp_aggregator *la;
	struct lacp_portmap *p;
	struct lacp_port *lp;
	uint64_t speed;
	u_int newmap;
	int i;

	newmap = lsc->lsc_activemap == 0 ? 1 : 0;
	p = &lsc->lsc_pmap[newmap];
	la = lsc->lsc_active_aggregator;
	speed = 0;
	bzero(p, sizeof(struct lacp_portmap));

	if (la != NULL && la->la_nports > 0) {
		p->pm_count = la->la_nports;
		i = 0;
		TAILQ_FOREACH(lp, &la->la_ports, lp_dist_q)
			p->pm_map[i++] = lp;
		KASSERT(i == p->pm_count, ("Invalid port count"));
		speed = lacp_aggregator_bandwidth(la);
	}
	sc->sc_ifp->if_baudrate = speed;

	/* switch the active portmap over */
	atomic_store_rel_int(&lsc->lsc_activemap, newmap);
	LACP_DPRINTF((NULL, "Set table %d with %d ports\n",
		    lsc->lsc_activemap,
		    lsc->lsc_pmap[lsc->lsc_activemap].pm_count));
}

static uint16_t
lacp_compose_key(struct lacp_port *lp)
{
	struct lagg_port *lgp = lp->lp_lagg;
	struct lagg_softc *sc = lgp->lp_softc;
	u_int media = lp->lp_media;
	uint16_t key;

	if ((lp->lp_state & LACP_STATE_AGGREGATION) == 0) {

		/*
		 * non-aggregatable links should have unique keys.
		 *
		 * XXX this isn't really unique as if_index is 16 bit.
		 */

		/* bit 0..14:	(some bits of) if_index of this port */
		key = lp->lp_ifp->if_index;
		/* bit 15:	1 */
		key |= 0x8000;
	} else {
		u_int subtype = IFM_SUBTYPE(media);

		KASSERT(IFM_TYPE(media) == IFM_ETHER, ("invalid media type"));
		KASSERT((media & IFM_FDX) != 0, ("aggregating HDX interface"));

		/* bit 0..4:	IFM_SUBTYPE modulo speed */
		switch (subtype) {
		case IFM_10_T:
		case IFM_10_2:
		case IFM_10_5:
		case IFM_10_STP:
		case IFM_10_FL:
			key = IFM_10_T;
			break;
		case IFM_100_TX:
		case IFM_100_FX:
		case IFM_100_T4:
		case IFM_100_VG:
		case IFM_100_T2:
		case IFM_100_T:
		case IFM_100_SGMII:
			key = IFM_100_TX;
			break;
		case IFM_1000_SX:
		case IFM_1000_LX:
		case IFM_1000_CX:
		case IFM_1000_T:
		case IFM_1000_KX:
		case IFM_1000_SGMII:
		case IFM_1000_CX_SGMII:
			key = IFM_1000_SX;
			break;
		case IFM_10G_LR:
		case IFM_10G_SR:
		case IFM_10G_CX4:
		case IFM_10G_TWINAX:
		case IFM_10G_TWINAX_LONG:
		case IFM_10G_LRM:
		case IFM_10G_T:
		case IFM_10G_KX4:
		case IFM_10G_KR:
		case IFM_10G_CR1:
		case IFM_10G_ER:
		case IFM_10G_SFI:
		case IFM_10G_AOC:
			key = IFM_10G_LR;
			break;
		case IFM_20G_KR2:
			key = IFM_20G_KR2;
			break;
		case IFM_2500_KX:
		case IFM_2500_T:
		case IFM_2500_X:
			key = IFM_2500_KX;
			break;
		case IFM_5000_T:
		case IFM_5000_KR:
		case IFM_5000_KR_S:
		case IFM_5000_KR1:
			key = IFM_5000_T;
			break;
		case IFM_50G_PCIE:
		case IFM_50G_CR2:
		case IFM_50G_KR2:
		case IFM_50G_SR2:
		case IFM_50G_LR2:
		case IFM_50G_LAUI2_AC:
		case IFM_50G_LAUI2:
		case IFM_50G_AUI2_AC:
		case IFM_50G_AUI2:
		case IFM_50G_CP:
		case IFM_50G_SR:
		case IFM_50G_LR:
		case IFM_50G_FR:
		case IFM_50G_KR_PAM4:
		case IFM_50G_AUI1_AC:
		case IFM_50G_AUI1:
			key = IFM_50G_PCIE;
			break;
		case IFM_56G_R4:
			key = IFM_56G_R4;
			break;
		case IFM_25G_PCIE:
		case IFM_25G_CR:
		case IFM_25G_KR:
		case IFM_25G_SR:
		case IFM_25G_LR:
		case IFM_25G_ACC:
		case IFM_25G_AOC:
		case IFM_25G_T:
		case IFM_25G_CR_S:
		case IFM_25G_CR1:
		case IFM_25G_KR_S:
		case IFM_25G_AUI:
		case IFM_25G_KR1:
			key = IFM_25G_PCIE;
			break;
		case IFM_40G_CR4:
		case IFM_40G_SR4:
		case IFM_40G_LR4:
		case IFM_40G_XLPPI:
		case IFM_40G_KR4:
		case IFM_40G_XLAUI:
		case IFM_40G_XLAUI_AC:
		case IFM_40G_ER4:
			key = IFM_40G_CR4;
			break;
		case IFM_100G_CR4:
		case IFM_100G_SR4:
		case IFM_100G_KR4:
		case IFM_100G_LR4:
		case IFM_100G_CAUI4_AC:
		case IFM_100G_CAUI4:
		case IFM_100G_AUI4_AC:
		case IFM_100G_AUI4:
		case IFM_100G_CR_PAM4:
		case IFM_100G_KR_PAM4:
		case IFM_100G_CP2:
		case IFM_100G_SR2:
		case IFM_100G_DR:
		case IFM_100G_KR2_PAM4:
		case IFM_100G_CAUI2_AC:
		case IFM_100G_CAUI2:
		case IFM_100G_AUI2_AC:
		case IFM_100G_AUI2:
			key = IFM_100G_CR4;
			break;
		case IFM_200G_CR4_PAM4:
		case IFM_200G_SR4:
		case IFM_200G_FR4:
		case IFM_200G_LR4:
		case IFM_200G_DR4:
		case IFM_200G_KR4_PAM4:
		case IFM_200G_AUI4_AC:
		case IFM_200G_AUI4:
		case IFM_200G_AUI8_AC:
		case IFM_200G_AUI8:
			key = IFM_200G_CR4_PAM4;
			break;
		case IFM_400G_FR8:
		case IFM_400G_LR8:
		case IFM_400G_DR4:
		case IFM_400G_AUI8_AC:
		case IFM_400G_AUI8:
			key = IFM_400G_FR8;
			break;
		default:
			key = subtype;
			break;
		}
		/* bit 5..14:	(some bits of) if_index of lagg device */
		key |= 0x7fe0 & ((sc->sc_ifp->if_index) << 5);
		/* bit 15:	0 */
	}
	return (htons(key));
}

static void
lacp_aggregator_addref(struct lacp_softc *lsc, struct lacp_aggregator *la)
{
	char buf[LACP_LAGIDSTR_MAX+1];

	LACP_DPRINTF((NULL, "%s: lagid=%s, refcnt %d -> %d\n",
	    __func__,
	    lacp_format_lagid(&la->la_actor, &la->la_partner,
	    buf, sizeof(buf)),
	    la->la_refcnt, la->la_refcnt + 1));

	KASSERT(la->la_refcnt > 0, ("refcount <= 0"));
	la->la_refcnt++;
	KASSERT(la->la_refcnt > la->la_nports, ("invalid refcount"));
}

static void
lacp_aggregator_delref(struct lacp_softc *lsc, struct lacp_aggregator *la)
{
	char buf[LACP_LAGIDSTR_MAX+1];

	LACP_DPRINTF((NULL, "%s: lagid=%s, refcnt %d -> %d\n",
	    __func__,
	    lacp_format_lagid(&la->la_actor, &la->la_partner,
	    buf, sizeof(buf)),
	    la->la_refcnt, la->la_refcnt - 1));

	KASSERT(la->la_refcnt > la->la_nports, ("invalid refcnt"));
	la->la_refcnt--;
	if (la->la_refcnt > 0) {
		return;
	}

	KASSERT(la->la_refcnt == 0, ("refcount not zero"));
	KASSERT(lsc->lsc_active_aggregator != la, ("aggregator active"));

	TAILQ_REMOVE(&lsc->lsc_aggregators, la, la_q);

	free(la, M_DEVBUF);
}

/*
 * lacp_aggregator_get: allocate an aggregator.
 */

static struct lacp_aggregator *
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

static void
lacp_fill_aggregator_id(struct lacp_aggregator *la, const struct lacp_port *lp)
{
	lacp_fill_aggregator_id_peer(&la->la_partner, &lp->lp_partner);
	lacp_fill_aggregator_id_peer(&la->la_actor, &lp->lp_actor);

	la->la_actor.lip_state = lp->lp_state & LACP_STATE_AGGREGATION;
}

static void
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

static int
lacp_aggregator_is_compatible(const struct lacp_aggregator *la,
    const struct lacp_port *lp)
{
	if (!(lp->lp_state & LACP_STATE_AGGREGATION) ||
	    !(lp->lp_partner.lip_state & LACP_STATE_AGGREGATION)) {
		return (0);
	}

	if (!(la->la_actor.lip_state & LACP_STATE_AGGREGATION)) {
		return (0);
	}

	if (!lacp_peerinfo_is_compatible(&la->la_partner, &lp->lp_partner)) {
		return (0);
	}

	if (!lacp_peerinfo_is_compatible(&la->la_actor, &lp->lp_actor)) {
		return (0);
	}

	return (1);
}

static int
lacp_peerinfo_is_compatible(const struct lacp_peerinfo *a,
    const struct lacp_peerinfo *b)
{
	if (memcmp(&a->lip_systemid, &b->lip_systemid,
	    sizeof(a->lip_systemid))) {
		return (0);
	}

	if (memcmp(&a->lip_key, &b->lip_key, sizeof(a->lip_key))) {
		return (0);
	}

	return (1);
}

static void
lacp_port_enable(struct lacp_port *lp)
{
	lp->lp_state |= LACP_STATE_AGGREGATION;
}

static void
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
static void
lacp_select(struct lacp_port *lp)
{
	struct lacp_softc *lsc = lp->lp_lsc;
	struct lacp_aggregator *la;
	char buf[LACP_LAGIDSTR_MAX+1];

	if (lp->lp_aggregator) {
		return;
	}

	/* If we haven't heard from our peer, skip this step. */
	if (lp->lp_state & LACP_STATE_DEFAULTED)
		return;

	KASSERT(!LACP_TIMER_ISARMED(lp, LACP_TIMER_WAIT_WHILE),
	    ("timer_wait_while still active"));

	LACP_DPRINTF((lp, "port lagid=%s\n",
	    lacp_format_lagid(&lp->lp_actor, &lp->lp_partner,
	    buf, sizeof(buf))));

	TAILQ_FOREACH(la, &lsc->lsc_aggregators, la_q) {
		if (lacp_aggregator_is_compatible(la, lp)) {
			break;
		}
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

static void
lacp_unselect(struct lacp_port *lp)
{
	struct lacp_softc *lsc = lp->lp_lsc;
	struct lacp_aggregator *la = lp->lp_aggregator;

	KASSERT(!LACP_TIMER_ISARMED(lp, LACP_TIMER_WAIT_WHILE),
	    ("timer_wait_while still active"));

	if (la == NULL) {
		return;
	}

	lp->lp_aggregator = NULL;
	lacp_aggregator_delref(lsc, la);
}

/* mux machine */

static void
lacp_sm_mux(struct lacp_port *lp)
{
	struct lagg_port *lgp = lp->lp_lagg;
	struct lagg_softc *sc = lgp->lp_softc;
	enum lacp_mux_state new_state;
	boolean_t p_sync =
		    (lp->lp_partner.lip_state & LACP_STATE_SYNC) != 0;
	boolean_t p_collecting =
	    (lp->lp_partner.lip_state & LACP_STATE_COLLECTING) != 0;
	enum lacp_selected selected = lp->lp_selected;
	struct lacp_aggregator *la;

	if (V_lacp_debug > 1)
		lacp_dprintf(lp, "%s: state= 0x%x, selected= 0x%x, "
		    "p_sync= 0x%x, p_collecting= 0x%x\n", __func__,
		    lp->lp_mux_state, selected, p_sync, p_collecting);

re_eval:
	la = lp->lp_aggregator;
	KASSERT(lp->lp_mux_state == LACP_MUX_DETACHED || la != NULL,
	    ("MUX not detached"));
	new_state = lp->lp_mux_state;
	switch (lp->lp_mux_state) {
	case LACP_MUX_DETACHED:
		if (selected != LACP_UNSELECTED) {
			new_state = LACP_MUX_WAITING;
		}
		break;
	case LACP_MUX_WAITING:
		KASSERT(la->la_pending > 0 ||
		    !LACP_TIMER_ISARMED(lp, LACP_TIMER_WAIT_WHILE),
		    ("timer_wait_while still active"));
		if (selected == LACP_SELECTED && la->la_pending == 0) {
			new_state = LACP_MUX_ATTACHED;
		} else if (selected == LACP_UNSELECTED) {
			new_state = LACP_MUX_DETACHED;
		}
		break;
	case LACP_MUX_ATTACHED:
		if (selected == LACP_SELECTED && p_sync) {
			new_state = LACP_MUX_COLLECTING;
		} else if (selected != LACP_SELECTED) {
			new_state = LACP_MUX_DETACHED;
		}
		break;
	case LACP_MUX_COLLECTING:
		if (selected == LACP_SELECTED && p_sync && p_collecting) {
			new_state = LACP_MUX_DISTRIBUTING;
		} else if (selected != LACP_SELECTED || !p_sync) {
			new_state = LACP_MUX_ATTACHED;
		}
		break;
	case LACP_MUX_DISTRIBUTING:
		if (selected != LACP_SELECTED || !p_sync || !p_collecting) {
			new_state = LACP_MUX_COLLECTING;
			lacp_dprintf(lp, "Interface stopped DISTRIBUTING, possible flapping\n");
			sc->sc_flapping++;
		}
		break;
	default:
		panic("%s: unknown state", __func__);
	}

	if (lp->lp_mux_state == new_state) {
		return;
	}

	lacp_set_mux(lp, new_state);
	goto re_eval;
}

static void
lacp_set_mux(struct lacp_port *lp, enum lacp_mux_state new_state)
{
	struct lacp_aggregator *la = lp->lp_aggregator;

	if (lp->lp_mux_state == new_state) {
		return;
	}

	switch (new_state) {
	case LACP_MUX_DETACHED:
		lp->lp_state &= ~LACP_STATE_SYNC;
		lacp_disable_distributing(lp);
		lacp_disable_collecting(lp);
		lacp_sm_assert_ntt(lp);
		/* cancel timer */
		if (LACP_TIMER_ISARMED(lp, LACP_TIMER_WAIT_WHILE)) {
			KASSERT(la->la_pending > 0,
			    ("timer_wait_while not active"));
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

static void
lacp_sm_mux_timer(struct lacp_port *lp)
{
	struct lacp_aggregator *la = lp->lp_aggregator;
	char buf[LACP_LAGIDSTR_MAX+1];

	KASSERT(la->la_pending > 0, ("no pending event"));

	LACP_DPRINTF((lp, "%s: aggregator %s, pending %d -> %d\n", __func__,
	    lacp_format_lagid(&la->la_actor, &la->la_partner,
	    buf, sizeof(buf)),
	    la->la_pending, la->la_pending - 1));

	la->la_pending--;
}

/* periodic transmit machine */

static void
lacp_sm_ptx_update_timeout(struct lacp_port *lp, uint8_t oldpstate)
{
	if (LACP_STATE_EQ(oldpstate, lp->lp_partner.lip_state,
	    LACP_STATE_TIMEOUT)) {
		return;
	}

	LACP_DPRINTF((lp, "partner timeout changed\n"));

	/*
	 * FAST_PERIODIC -> SLOW_PERIODIC
	 * or
	 * SLOW_PERIODIC (-> PERIODIC_TX) -> FAST_PERIODIC
	 *
	 * let lacp_sm_ptx_tx_schedule to update timeout.
	 */

	LACP_TIMER_DISARM(lp, LACP_TIMER_PERIODIC);

	/*
	 * if timeout has been shortened, assert NTT.
	 */

	if ((lp->lp_partner.lip_state & LACP_STATE_TIMEOUT)) {
		lacp_sm_assert_ntt(lp);
	}
}

static void
lacp_sm_ptx_tx_schedule(struct lacp_port *lp)
{
	int timeout;

	if (!(lp->lp_state & LACP_STATE_ACTIVITY) &&
	    !(lp->lp_partner.lip_state & LACP_STATE_ACTIVITY)) {

		/*
		 * NO_PERIODIC
		 */

		LACP_TIMER_DISARM(lp, LACP_TIMER_PERIODIC);
		return;
	}

	if (LACP_TIMER_ISARMED(lp, LACP_TIMER_PERIODIC)) {
		return;
	}

	timeout = (lp->lp_partner.lip_state & LACP_STATE_TIMEOUT) ?
	    LACP_FAST_PERIODIC_TIME : LACP_SLOW_PERIODIC_TIME;

	LACP_TIMER_ARM(lp, LACP_TIMER_PERIODIC, timeout);
}

static void
lacp_sm_ptx_timer(struct lacp_port *lp)
{
	lacp_sm_assert_ntt(lp);
}

static void
lacp_sm_rx(struct lacp_port *lp, const struct lacpdu *du)
{
	int timeout;

	/*
	 * check LACP_DISABLED first
	 */

	if (!(lp->lp_state & LACP_STATE_AGGREGATION)) {
		return;
	}

	/*
	 * check loopback condition.
	 */

	if (!lacp_compare_systemid(&du->ldu_actor.lip_systemid,
	    &lp->lp_actor.lip_systemid)) {
		return;
	}

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

	/*
	 * kick transmit machine without waiting the next tick.
	 */

	lacp_sm_tx(lp);
}

static void
lacp_sm_rx_set_expired(struct lacp_port *lp)
{
	lp->lp_partner.lip_state &= ~LACP_STATE_SYNC;
	lp->lp_partner.lip_state |= LACP_STATE_TIMEOUT;
	LACP_TIMER_ARM(lp, LACP_TIMER_CURRENT_WHILE, LACP_SHORT_TIMEOUT_TIME);
	lp->lp_state |= LACP_STATE_EXPIRED;
}

static void
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

static void
lacp_sm_rx_record_pdu(struct lacp_port *lp, const struct lacpdu *du)
{
	boolean_t active;
	uint8_t oldpstate;
	char buf[LACP_STATESTR_MAX+1];

	LACP_TRACE(lp);

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
		/*
		 * XXX Maintain legacy behavior of leaving the
		 * LACP_STATE_SYNC bit unchanged from the partner's
		 * advertisement if lsc_strict_mode is false.
		 * TODO: We should re-examine the concept of the "strict mode"
		 * to ensure it makes sense to maintain a non-strict mode.
		 */
		if (lp->lp_lsc->lsc_strict_mode)
			lp->lp_partner.lip_state |= LACP_STATE_SYNC;
	} else {
		lp->lp_partner.lip_state &= ~LACP_STATE_SYNC;
	}

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

static void
lacp_sm_rx_update_ntt(struct lacp_port *lp, const struct lacpdu *du)
{

	LACP_TRACE(lp);

	if (lacp_compare_peerinfo(&lp->lp_actor, &du->ldu_partner) ||
	    !LACP_STATE_EQ(lp->lp_state, du->ldu_partner.lip_state,
	    LACP_STATE_ACTIVITY | LACP_STATE_SYNC | LACP_STATE_AGGREGATION)) {
		LACP_DPRINTF((lp, "%s: assert ntt\n", __func__));
		lacp_sm_assert_ntt(lp);
	}
}

static void
lacp_sm_rx_record_default(struct lacp_port *lp)
{
	uint8_t oldpstate;

	LACP_TRACE(lp);

	oldpstate = lp->lp_partner.lip_state;
	if (lp->lp_lsc->lsc_strict_mode)
		lp->lp_partner = lacp_partner_admin_strict;
	else
		lp->lp_partner = lacp_partner_admin_optimistic;
	lp->lp_state |= LACP_STATE_DEFAULTED;
	lacp_sm_ptx_update_timeout(lp, oldpstate);
}

static void
lacp_sm_rx_update_selected_from_peerinfo(struct lacp_port *lp,
    const struct lacp_peerinfo *info)
{

	LACP_TRACE(lp);

	if (lacp_compare_peerinfo(&lp->lp_partner, info) ||
	    !LACP_STATE_EQ(lp->lp_partner.lip_state, info->lip_state,
	    LACP_STATE_AGGREGATION)) {
		lp->lp_selected = LACP_UNSELECTED;
		/* mux machine will clean up lp->lp_aggregator */
	}
}

static void
lacp_sm_rx_update_selected(struct lacp_port *lp, const struct lacpdu *du)
{

	LACP_TRACE(lp);

	lacp_sm_rx_update_selected_from_peerinfo(lp, &du->ldu_actor);
}

static void
lacp_sm_rx_update_default_selected(struct lacp_port *lp)
{

	LACP_TRACE(lp);

	if (lp->lp_lsc->lsc_strict_mode)
		lacp_sm_rx_update_selected_from_peerinfo(lp,
		    &lacp_partner_admin_strict);
	else
		lacp_sm_rx_update_selected_from_peerinfo(lp,
		    &lacp_partner_admin_optimistic);
}

/* transmit machine */

static void
lacp_sm_tx(struct lacp_port *lp)
{
	int error = 0;

	if (!(lp->lp_state & LACP_STATE_AGGREGATION)
#if 1
	    || (!(lp->lp_state & LACP_STATE_ACTIVITY)
	    && !(lp->lp_partner.lip_state & LACP_STATE_ACTIVITY))
#endif
	    ) {
		lp->lp_flags &= ~LACP_PORT_NTT;
	}

	if (!(lp->lp_flags & LACP_PORT_NTT)) {
		return;
	}

	/* Rate limit to 3 PDUs per LACP_FAST_PERIODIC_TIME */
	if (ppsratecheck(&lp->lp_last_lacpdu, &lp->lp_lacpdu_sent,
		    (3 / LACP_FAST_PERIODIC_TIME)) == 0) {
		LACP_DPRINTF((lp, "rate limited pdu\n"));
		return;
	}

	if (((1 << lp->lp_ifp->if_dunit) & lp->lp_lsc->lsc_debug.lsc_tx_test) == 0) {
		error = lacp_xmit_lacpdu(lp);
	} else {
		LACP_TPRINTF((lp, "Dropping TX PDU\n"));
	}

	if (error == 0) {
		lp->lp_flags &= ~LACP_PORT_NTT;
	} else {
		LACP_DPRINTF((lp, "lacpdu transmit failure, error %d\n",
		    error));
	}
}

static void
lacp_sm_assert_ntt(struct lacp_port *lp)
{

	lp->lp_flags |= LACP_PORT_NTT;
}

static void
lacp_run_timers(struct lacp_port *lp)
{
	int i;

	for (i = 0; i < LACP_NTIMER; i++) {
		KASSERT(lp->lp_timer[i] >= 0,
		    ("invalid timer value %d", lp->lp_timer[i]));
		if (lp->lp_timer[i] == 0) {
			continue;
		} else if (--lp->lp_timer[i] <= 0) {
			if (lacp_timer_funcs[i]) {
				(*lacp_timer_funcs[i])(lp);
			}
		}
	}
}

int
lacp_marker_input(struct lacp_port *lp, struct mbuf *m)
{
	struct lacp_softc *lsc = lp->lp_lsc;
	struct lagg_port *lgp = lp->lp_lagg;
	struct lacp_port *lp2;
	struct markerdu *mdu;
	int error = 0;
	int pending = 0;

	if (m->m_pkthdr.len != sizeof(*mdu)) {
		goto bad;
	}

	if ((m->m_flags & M_MCAST) == 0) {
		goto bad;
	}

	if (m->m_len < sizeof(*mdu)) {
		m = m_pullup(m, sizeof(*mdu));
		if (m == NULL) {
			return (ENOMEM);
		}
	}

	mdu = mtod(m, struct markerdu *);

	if (memcmp(&mdu->mdu_eh.ether_dhost,
	    &ethermulticastaddr_slowprotocols, ETHER_ADDR_LEN)) {
		goto bad;
	}

	if (mdu->mdu_sph.sph_version != 1) {
		goto bad;
	}

	switch (mdu->mdu_tlv.tlv_type) {
	case MARKER_TYPE_INFO:
		if (tlv_check(mdu, sizeof(*mdu), &mdu->mdu_tlv,
		    marker_info_tlv_template, TRUE)) {
			goto bad;
		}
		mdu->mdu_tlv.tlv_type = MARKER_TYPE_RESPONSE;
		memcpy(&mdu->mdu_eh.ether_dhost,
		    &ethermulticastaddr_slowprotocols, ETHER_ADDR_LEN);
		memcpy(&mdu->mdu_eh.ether_shost,
		    lgp->lp_lladdr, ETHER_ADDR_LEN);
		error = lagg_enqueue(lp->lp_ifp, m);
		break;

	case MARKER_TYPE_RESPONSE:
		if (tlv_check(mdu, sizeof(*mdu), &mdu->mdu_tlv,
		    marker_response_tlv_template, TRUE)) {
			goto bad;
		}
		LACP_DPRINTF((lp, "marker response, port=%u, sys=%6D, id=%u\n",
		    ntohs(mdu->mdu_info.mi_rq_port), mdu->mdu_info.mi_rq_system,
		    ":", ntohl(mdu->mdu_info.mi_rq_xid)));

		/* Verify that it is the last marker we sent out */
		if (memcmp(&mdu->mdu_info, &lp->lp_marker,
		    sizeof(struct lacp_markerinfo)))
			goto bad;

		LACP_LOCK(lsc);
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
				lsc->lsc_suppress_distributing = FALSE;
			}
		}
		LACP_UNLOCK(lsc);
		m_freem(m);
		break;

	default:
		goto bad;
	}

	return (error);

bad:
	LACP_DPRINTF((lp, "bad marker frame\n"));
	m_freem(m);
	return (EINVAL);
}

static int
tlv_check(const void *p, size_t size, const struct tlvhdr *tlv,
    const struct tlv_template *tmpl, boolean_t check_type)
{
	while (/* CONSTCOND */ 1) {
		if ((const char *)tlv - (const char *)p + sizeof(*tlv) > size) {
			return (EINVAL);
		}
		if ((check_type && tlv->tlv_type != tmpl->tmpl_type) ||
		    tlv->tlv_length != tmpl->tmpl_length) {
			return (EINVAL);
		}
		if (tmpl->tmpl_type == 0) {
			break;
		}
		tlv = (const struct tlvhdr *)
		    ((const char *)tlv + tlv->tlv_length);
		tmpl++;
	}

	return (0);
}

/* Debugging */
const char *
lacp_format_mac(const uint8_t *mac, char *buf, size_t buflen)
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
	if (la == NULL) {
		return ("(none)");
	}

	return (lacp_format_lagid(&la->la_actor, &la->la_partner, buf, buflen));
}

const char *
lacp_format_state(uint8_t state, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%b", state, LACP_STATE_BITS);
	return (buf);
}

static void
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

static void
lacp_dprintf(const struct lacp_port *lp, const char *fmt, ...)
{
	va_list va;

	if (lp) {
		printf("%s: ", lp->lp_ifp->if_xname);
	}

	va_start(va, fmt);
	vprintf(fmt, va);
	va_end(va);
}

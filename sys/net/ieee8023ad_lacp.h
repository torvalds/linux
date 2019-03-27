/*	$NetBSD: ieee8023ad_impl.h,v 1.2 2005/12/10 23:21:39 elad Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c)2005 YAMAMOTO Takashi,
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
 *
 * $FreeBSD$
 */

/*
 * IEEE802.3ad LACP
 *
 * implementation details.
 */

#define	LACP_TIMER_CURRENT_WHILE	0
#define	LACP_TIMER_PERIODIC		1
#define	LACP_TIMER_WAIT_WHILE		2
#define	LACP_NTIMER			3

#define	LACP_TIMER_ARM(port, timer, val) \
	(port)->lp_timer[(timer)] = (val)
#define	LACP_TIMER_DISARM(port, timer) \
	(port)->lp_timer[(timer)] = 0
#define	LACP_TIMER_ISARMED(port, timer) \
	((port)->lp_timer[(timer)] > 0)

/*
 * IEEE802.3ad LACP
 *
 * protocol definitions.
 */

#define	LACP_STATE_ACTIVITY	(1<<0)
#define	LACP_STATE_TIMEOUT	(1<<1)
#define	LACP_STATE_AGGREGATION	(1<<2)
#define	LACP_STATE_SYNC		(1<<3)
#define	LACP_STATE_COLLECTING	(1<<4)
#define	LACP_STATE_DISTRIBUTING	(1<<5)
#define	LACP_STATE_DEFAULTED	(1<<6)
#define	LACP_STATE_EXPIRED	(1<<7)

#define	LACP_PORT_NTT		0x00000001
#define	LACP_PORT_MARK		0x00000002

#define	LACP_STATE_BITS		\
	"\020"			\
	"\001ACTIVITY"		\
	"\002TIMEOUT"		\
	"\003AGGREGATION"	\
	"\004SYNC"		\
	"\005COLLECTING"	\
	"\006DISTRIBUTING"	\
	"\007DEFAULTED"		\
	"\010EXPIRED"

#ifdef _KERNEL
/*
 * IEEE802.3 slow protocols
 *
 * protocol (on-wire) definitions.
 *
 * XXX should be elsewhere.
 */

#define	SLOWPROTOCOLS_SUBTYPE_LACP	1
#define	SLOWPROTOCOLS_SUBTYPE_MARKER	2

struct slowprothdr {
	uint8_t		sph_subtype;
	uint8_t		sph_version;
} __packed;

/*
 * TLV on-wire structure.
 */

struct tlvhdr {
	uint8_t		tlv_type;
	uint8_t		tlv_length;
	/* uint8_t tlv_value[]; */
} __packed;

/*
 * ... and our implementation.
 */

#define	TLV_SET(tlv, type, length) \
	do { \
		(tlv)->tlv_type = (type); \
		(tlv)->tlv_length = sizeof(*tlv) + (length); \
	} while (/*CONSTCOND*/0)

struct tlv_template {
	uint8_t			tmpl_type;
	uint8_t			tmpl_length;
};

struct lacp_systemid {
	uint16_t		lsi_prio;
	uint8_t			lsi_mac[6];
} __packed;

struct lacp_portid {
	uint16_t		lpi_prio;
	uint16_t		lpi_portno;
} __packed;

struct lacp_peerinfo {
	struct lacp_systemid	lip_systemid;
	uint16_t		lip_key;
	struct lacp_portid	lip_portid;
	uint8_t			lip_state;
	uint8_t			lip_resv[3];
} __packed;

struct lacp_collectorinfo {
	uint16_t		lci_maxdelay;
	uint8_t			lci_resv[12];
} __packed;

struct lacpdu {
	struct ether_header	ldu_eh;
	struct slowprothdr	ldu_sph;

	struct tlvhdr		ldu_tlv_actor;
	struct lacp_peerinfo	ldu_actor;
	struct tlvhdr		ldu_tlv_partner;
	struct lacp_peerinfo	ldu_partner;
	struct tlvhdr		ldu_tlv_collector;
	struct lacp_collectorinfo ldu_collector;
	struct tlvhdr		ldu_tlv_term;
	uint8_t			ldu_resv[50];
} __packed;

/*
 * IEEE802.3ad marker protocol
 *
 * protocol (on-wire) definitions.
 */
struct lacp_markerinfo {
	uint16_t		mi_rq_port;
	uint8_t			mi_rq_system[ETHER_ADDR_LEN];
	uint32_t		mi_rq_xid;
	uint8_t			mi_pad[2];
} __packed;

struct markerdu {
	struct ether_header	mdu_eh;
	struct slowprothdr	mdu_sph;

	struct tlvhdr		mdu_tlv;
	struct lacp_markerinfo	mdu_info;
	struct tlvhdr		mdu_tlv_term;
	uint8_t			mdu_resv[90];
} __packed;

#define	MARKER_TYPE_INFO	0x01
#define	MARKER_TYPE_RESPONSE	0x02

enum lacp_selected {
	LACP_UNSELECTED,
	LACP_STANDBY,	/* not used in this implementation */
	LACP_SELECTED,
};

enum lacp_mux_state {
	LACP_MUX_DETACHED,
	LACP_MUX_WAITING,
	LACP_MUX_ATTACHED,
	LACP_MUX_COLLECTING,
	LACP_MUX_DISTRIBUTING,
};

#define	LACP_MAX_PORTS		32

struct lacp_portmap {
	int			pm_count;
	struct lacp_port	*pm_map[LACP_MAX_PORTS];
};

struct lacp_port {
	TAILQ_ENTRY(lacp_port)	lp_dist_q;
	LIST_ENTRY(lacp_port)	lp_next;
	struct lacp_softc	*lp_lsc;
	struct lagg_port	*lp_lagg;
	struct ifnet		*lp_ifp;
	struct lacp_peerinfo	lp_partner;
	struct lacp_peerinfo	lp_actor;
	struct lacp_markerinfo	lp_marker;
#define	lp_state	lp_actor.lip_state
#define	lp_key		lp_actor.lip_key
#define	lp_systemid	lp_actor.lip_systemid
	struct timeval		lp_last_lacpdu;
	int			lp_lacpdu_sent;
	enum lacp_mux_state	lp_mux_state;
	enum lacp_selected	lp_selected;
	int			lp_flags;
	u_int			lp_media; /* XXX redundant */
	int			lp_timer[LACP_NTIMER];
	struct ifmultiaddr	*lp_ifma;

	struct lacp_aggregator	*lp_aggregator;
};

struct lacp_aggregator {
	TAILQ_ENTRY(lacp_aggregator)	la_q;
	int			la_refcnt; /* num of ports which selected us */
	int			la_nports; /* num of distributing ports  */
	TAILQ_HEAD(, lacp_port)	la_ports; /* distributing ports */
	struct lacp_peerinfo	la_partner;
	struct lacp_peerinfo	la_actor;
	int			la_pending; /* number of ports in wait_while */
};

struct lacp_softc {
	struct lagg_softc	*lsc_softc;
	struct mtx		lsc_mtx;
	struct lacp_aggregator	*lsc_active_aggregator;
	TAILQ_HEAD(, lacp_aggregator) lsc_aggregators;
	boolean_t		lsc_suppress_distributing;
	struct callout		lsc_transit_callout;
	struct callout		lsc_callout;
	LIST_HEAD(, lacp_port)	lsc_ports;
	struct lacp_portmap	lsc_pmap[2];
	volatile u_int		lsc_activemap;
	u_int32_t		lsc_hashkey;
	struct {
		u_int32_t	lsc_rx_test;
		u_int32_t	lsc_tx_test;
	} lsc_debug;
	u_int32_t		lsc_strict_mode;
	boolean_t		lsc_fast_timeout; /* if set, fast timeout */
};

#define	LACP_TYPE_ACTORINFO	1
#define	LACP_TYPE_PARTNERINFO	2
#define	LACP_TYPE_COLLECTORINFO	3

/* timeout values (in sec) */
#define	LACP_FAST_PERIODIC_TIME		(1)
#define	LACP_SLOW_PERIODIC_TIME		(30)
#define	LACP_SHORT_TIMEOUT_TIME		(3 * LACP_FAST_PERIODIC_TIME)
#define	LACP_LONG_TIMEOUT_TIME		(3 * LACP_SLOW_PERIODIC_TIME)
#define	LACP_CHURN_DETECTION_TIME	(60)
#define	LACP_AGGREGATE_WAIT_TIME	(2)
#define	LACP_TRANSIT_DELAY		3000	/* in msec */

#define	LACP_STATE_EQ(s1, s2, mask)	\
	((((s1) ^ (s2)) & (mask)) == 0)

#define	LACP_SYS_PRI(peer)	(peer).lip_systemid.lsi_prio

#define	LACP_PORT(_lp)	((struct lacp_port *)(_lp)->lp_psc)
#define	LACP_SOFTC(_sc)	((struct lacp_softc *)(_sc)->sc_psc)

#define LACP_LOCK_INIT(_lsc)		mtx_init(&(_lsc)->lsc_mtx, \
					    "lacp mtx", NULL, MTX_DEF)
#define LACP_LOCK_DESTROY(_lsc)		mtx_destroy(&(_lsc)->lsc_mtx)
#define LACP_LOCK(_lsc)			mtx_lock(&(_lsc)->lsc_mtx)
#define LACP_UNLOCK(_lsc)		mtx_unlock(&(_lsc)->lsc_mtx)
#define LACP_LOCK_ASSERT(_lsc)		mtx_assert(&(_lsc)->lsc_mtx, MA_OWNED)

struct mbuf	*lacp_input(struct lagg_port *, struct mbuf *);
struct lagg_port *lacp_select_tx_port(struct lagg_softc *, struct mbuf *);
#ifdef RATELIMIT
struct lagg_port *lacp_select_tx_port_by_hash(struct lagg_softc *, uint32_t);
#endif
void		lacp_attach(struct lagg_softc *);
void		lacp_detach(void *);
void		lacp_init(struct lagg_softc *);
void		lacp_stop(struct lagg_softc *);
int		lacp_port_create(struct lagg_port *);
void		lacp_port_destroy(struct lagg_port *);
void		lacp_linkstate(struct lagg_port *);
void		lacp_req(struct lagg_softc *, void *);
void		lacp_portreq(struct lagg_port *, void *);

static __inline int
lacp_isactive(struct lagg_port *lgp)
{
	struct lacp_port *lp = LACP_PORT(lgp);
	struct lacp_softc *lsc = lp->lp_lsc;
	struct lacp_aggregator *la = lp->lp_aggregator;

	/* This port is joined to the active aggregator */
	if (la != NULL && la == lsc->lsc_active_aggregator)
		return (1);

	return (0);
}

static __inline int
lacp_iscollecting(struct lagg_port *lgp)
{
	struct lacp_port *lp = LACP_PORT(lgp);

	return ((lp->lp_state & LACP_STATE_COLLECTING) != 0);
}

static __inline int
lacp_isdistributing(struct lagg_port *lgp)
{
	struct lacp_port *lp = LACP_PORT(lgp);

	return ((lp->lp_state & LACP_STATE_DISTRIBUTING) != 0);
}

/* following constants don't include terminating NUL */
#define	LACP_MACSTR_MAX		(2*6 + 5)
#define	LACP_SYSTEMPRIOSTR_MAX	(4)
#define	LACP_SYSTEMIDSTR_MAX	(LACP_SYSTEMPRIOSTR_MAX + 1 + LACP_MACSTR_MAX)
#define	LACP_PORTPRIOSTR_MAX	(4)
#define	LACP_PORTNOSTR_MAX	(4)
#define	LACP_PORTIDSTR_MAX	(LACP_PORTPRIOSTR_MAX + 1 + LACP_PORTNOSTR_MAX)
#define	LACP_KEYSTR_MAX		(4)
#define	LACP_PARTNERSTR_MAX	\
	(1 + LACP_SYSTEMIDSTR_MAX + 1 + LACP_KEYSTR_MAX + 1 \
	+ LACP_PORTIDSTR_MAX + 1)
#define	LACP_LAGIDSTR_MAX	\
	(1 + LACP_PARTNERSTR_MAX + 1 + LACP_PARTNERSTR_MAX + 1)
#define	LACP_STATESTR_MAX	(255) /* XXX */
#endif	/* _KERNEL */

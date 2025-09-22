/*	$OpenBSD: if_aggr.c,v 1.50 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2019 The University of Queensland
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

/*
 * This driver implements 802.1AX Link Aggregation (formerly 802.3ad)
 *
 * The specification describes systems with multiple ports that that
 * can dynamically form aggregations. The relationships between ports
 * and aggregations is such that arbitrary ports connected to ports
 * on other systems may move between aggregations, and there can be
 * as many aggregations as ports. An aggregation in this model is
 * effectively an interface, and becomes the point that Ethernet traffic
 * enters and leaves the system. The spec also contains a description
 * of the Link Aggregation Control Protocol (LACP) for use on the wire,
 * and how to process it and select ports and aggregations based on
 * it.
 *
 * This driver implements a simplified or constrained model where each
 * aggr(4) interface is effectively an independent system, and will
 * only support one aggregation. This supports the use of the kernel
 * interface as a static entity that is created and configured once,
 * and has the link "come up" when that one aggregation is selected
 * by the LACP protocol.
 */

/*
 * This code was written by David Gwynne <dlg@uq.edu.au> as part
 * of the Information Technology Infrastructure Group (ITIG) in the
 * Faculty of Engineering, Architecture and Information Technology
 * (EAIT).
 */

/*
 * TODO:
 *
 * - add locking
 * - figure out the Ready_N and Ready logic
 */

#include "bpfilter.h"
#include "kstat.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/rwlock.h>
#include <sys/percpu.h>
#include <sys/smr.h>
#include <sys/task.h>
#include <sys/kstat.h>

#include <net/if.h>
#include <net/if_types.h>

#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <crypto/siphash.h> /* if_trunk.h uses siphash bits */
#include <net/if_trunk.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

/*
 * Link Aggregation Control Protocol (LACP)
 */

struct ether_slowproto_hdr {
	uint8_t		sph_subtype;
	uint8_t		sph_version;
} __packed;

#define SLOWPROTOCOLS_SUBTYPE_LACP	1
#define SLOWPROTOCOLS_SUBTYPE_LACP_MARKER \
					2

#define LACP_VERSION			1

#define LACP_FAST_PERIODIC_TIME		1
#define LACP_SLOW_PERIODIC_TIME		30
#define LACP_TIMEOUT_FACTOR		3
#define LACP_AGGREGATION_WAIT_TIME	2

#define LACP_TX_MACHINE_RATE		3 /* per LACP_FAST_PERIODIC_TIME */

#define LACP_ADDR_C_BRIDGE		{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 }
#define LACP_ADDR_SLOW			{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x02 }
#define LACP_ADDR_SLOW_E64		0x0180c2000002ULL
#define LACP_ADDR_NON_TPMR_BRIDGE	{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x03 }

struct lacp_tlv_hdr {
	uint8_t			lacp_tlv_type;
	uint8_t			lacp_tlv_length;
} __packed __aligned(2);

/* LACP TLV types */

#define LACP_T_TERMINATOR		0x00
#define LACP_T_ACTOR			0x01
#define LACP_T_PARTNER			0x02
#define LACP_T_COLLECTOR		0x03

/* LACPv2 TLV types */

#define LACP_T_PORT_ALGORITHM		0x04
#define LACP_T_PORT_CONVERSATION_ID_DIGEST \
					0x05
#define LACP_T_PORT_CONVERSATION_MASK	0x06
#define LACP_T_PORT_CONVERSATION_SERVICE_MAPPING \
					0x0a

struct lacp_sysid {
	uint16_t		lacp_sysid_priority;
	uint8_t			lacp_sysid_mac[ETHER_ADDR_LEN];
} __packed __aligned(2);

struct lacp_portid {
	uint16_t		lacp_portid_priority;
	uint16_t		lacp_portid_number;
} __packed __aligned(2);

struct lacp_port_info {
	struct lacp_sysid	lacp_sysid;
	uint16_t		lacp_key;
	struct lacp_portid	lacp_portid;
	uint8_t			lacp_state;
	uint8_t			lacp_reserved[3];
} __packed __aligned(2);

#define LACP_STATE_ACTIVITY		(1 << 0)
#define LACP_STATE_TIMEOUT		(1 << 1)
#define LACP_STATE_AGGREGATION		(1 << 2)
#define LACP_STATE_SYNC			(1 << 3)
#define LACP_STATE_COLLECTING		(1 << 4)
#define LACP_STATE_DISTRIBUTING		(1 << 5)
#define LACP_STATE_DEFAULTED		(1 << 6)
#define LACP_STATE_EXPIRED		(1 << 7)

struct lacp_collector_info {
	uint16_t		lacp_maxdelay;
	uint8_t			lacp_reserved[12];
} __packed __aligned(2);

struct lacp_du {
	struct ether_slowproto_hdr
				lacp_du_sph;
	struct lacp_tlv_hdr	lacp_actor_info_tlv;
	struct lacp_port_info	lacp_actor_info;
	struct lacp_tlv_hdr	lacp_partner_info_tlv;
	struct lacp_port_info	lacp_partner_info;
	struct lacp_tlv_hdr	lacp_collector_info_tlv;
	struct lacp_collector_info
				lacp_collector_info;
	/* other TLVs go here */
	struct lacp_tlv_hdr	lacp_terminator;
	uint8_t			lacp_pad[50];
} __packed __aligned(2);

/* Marker TLV types */

#define MARKER_T_INFORMATION		0x01
#define MARKER_T_RESPONSE		0x02

struct marker_info {
	uint16_t		marker_requester_port;
	uint8_t			marker_requester_system[ETHER_ADDR_LEN];
	uint8_t			marker_requester_txid[4];
	uint8_t			marker_pad[2];
} __packed __aligned(2);

struct marker_pdu {
	struct ether_slowproto_hdr
				marker_sph;

	struct lacp_tlv_hdr	marker_info_tlv;
	struct marker_info	marker_info;
	struct lacp_tlv_hdr	marker_terminator;
	uint8_t			marker_pad[90];
} __packed __aligned(2);

enum lacp_rxm_state {
	LACP_RXM_S_BEGIN = 0,
	LACP_RXM_S_INITIALIZE,
	LACP_RXM_S_PORT_DISABLED,
	LACP_RXM_S_EXPIRED,
	LACP_RXM_S_LACP_DISABLED,
	LACP_RXM_S_DEFAULTED,
	LACP_RXM_S_CURRENT,
};

enum lacp_rxm_event {
	LACP_RXM_E_BEGIN,
	LACP_RXM_E_UCT,
	LACP_RXM_E_PORT_MOVED,
	LACP_RXM_E_NOT_PORT_MOVED,
	LACP_RXM_E_PORT_ENABLED,
	LACP_RXM_E_NOT_PORT_ENABLED,
	LACP_RXM_E_LACP_ENABLED,
	LACP_RXM_E_NOT_LACP_ENABLED,
	LACP_RXM_E_LACPDU, /* CtrlMuxN:M_UNITDATA.indication(LACPDU) */
	LACP_RXM_E_TIMER_EXPIRED, /* current_while_timer expired */
};

enum lacp_mux_state {
	LACP_MUX_S_BEGIN = 0,
	LACP_MUX_S_DETACHED,
	LACP_MUX_S_WAITING,
	LACP_MUX_S_ATTACHED,
	LACP_MUX_S_DISTRIBUTING,
	LACP_MUX_S_COLLECTING,
};

enum lacp_mux_event {
	LACP_MUX_E_BEGIN,
	LACP_MUX_E_SELECTED,
	LACP_MUX_E_STANDBY,
	LACP_MUX_E_UNSELECTED,
	LACP_MUX_E_READY,
	LACP_MUX_E_SYNC,
	LACP_MUX_E_NOT_SYNC,
	LACP_MUX_E_COLLECTING,
	LACP_MUX_E_NOT_COLLECTING,
};

/*
 * LACP variables
 */

static const uint8_t lacp_address_slow[ETHER_ADDR_LEN] = LACP_ADDR_SLOW;

static const char *lacp_rxm_state_names[] = {
	"BEGIN",
	"INITIALIZE",
	"PORT_DISABLED",
	"EXPIRED",
	"LACP_DISABLED",
	"DEFAULTED",
	"CURRENT",
};

static const char *lacp_rxm_event_names[] = {
	"BEGIN",
	"UCT",
	"port_moved",
	"!port_moved",
	"port_enabled",
	"!port_enabled",
	"LACP_Enabled",
	"!LACP_Enabled",
	"LACPDU",
	"current_while_timer expired",
};

static const char *lacp_mux_state_names[] = {
	"BEGIN",
	"DETACHED",
	"WAITING",
	"ATTACHED",
	"DISTRIBUTING",
	"COLLECTING",
};

static const char *lacp_mux_event_names[] = {
	"BEGIN",
	"Selected == SELECTED",
	"Selected == STANDBY",
	"Selected == UNSELECTED",
	"Ready",
	"Partner.Sync",
	"! Partner.Sync",
	"Partner.Collecting",
	"! Partner.Collecting",
};

/*
 * aggr interface
 */

#define AGGR_PORT_BITS		5
#define AGGR_FLOWID_SHIFT	(16 - AGGR_PORT_BITS)

#define AGGR_MAX_PORTS		(1 << AGGR_PORT_BITS)
#define AGGR_MAX_SLOW_PKTS	3

struct aggr_multiaddr {
	TAILQ_ENTRY(aggr_multiaddr)
				m_entry;
	unsigned int		m_refs;
	uint8_t			m_addrlo[ETHER_ADDR_LEN];
	uint8_t			m_addrhi[ETHER_ADDR_LEN];
	struct sockaddr_storage m_addr;
};
TAILQ_HEAD(aggr_multiaddrs, aggr_multiaddr);

struct aggr_softc;

enum aggr_port_selected {
	AGGR_PORT_UNSELECTED,
	AGGR_PORT_SELECTED,
	AGGR_PORT_STANDBY,
};

static const char *aggr_port_selected_names[] = {
	"UNSELECTED",
	"SELECTED",
	"STANDBY",
};

struct aggr_proto_count {
	uint64_t		c_pkts;
	uint64_t		c_bytes;
};

#define AGGR_PROTO_TX_LACP	0
#define AGGR_PROTO_TX_MARKER	1
#define AGGR_PROTO_RX_LACP	2
#define AGGR_PROTO_RX_MARKER	3

#define AGGR_PROTO_COUNT	4

struct aggr_port {
	struct ifnet		*p_ifp0;
	struct kstat		*p_kstat;
	struct mutex		 p_mtx;

	uint8_t			 p_lladdr[ETHER_ADDR_LEN];
	uint32_t		 p_mtu;

	int (*p_ioctl)(struct ifnet *, u_long, caddr_t);
	void (*p_input)(struct ifnet *, struct mbuf *, struct netstack *);
	int (*p_output)(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);

	struct task		 p_lhook;
	struct task		 p_dhook;

	struct aggr_softc	*p_aggr;
	TAILQ_ENTRY(aggr_port)	 p_entry;

	unsigned int		 p_collecting;
	unsigned int		 p_distributing;
	TAILQ_ENTRY(aggr_port)	 p_entry_distributing;
	TAILQ_ENTRY(aggr_port)	 p_entry_muxen;

	/* Partner information */
	enum aggr_port_selected	 p_muxed;
	enum aggr_port_selected	 p_selected;		/* Selected */
	struct lacp_port_info	 p_partner;
#define p_partner_state		 p_partner.lacp_state

	uint8_t			 p_actor_state;
	uint8_t			 p_lacp_timeout;

	struct timeout		 p_current_while_timer;
	struct timeout		 p_wait_while_timer;

	/* Receive machine */
	enum lacp_rxm_state	 p_rxm_state;
	struct mbuf_list	 p_rxm_ml;
	struct task		 p_rxm_task;

	/* Periodic Transmission machine */
	struct timeout		 p_ptm_tx;

	/* Mux machine */
	enum lacp_mux_state	 p_mux_state;

	/* Transmit machine */
	int			 p_txm_log[LACP_TX_MACHINE_RATE];
	unsigned int		 p_txm_slot;
	struct timeout		 p_txm_ntt;

	/* Counters */
	struct aggr_proto_count	 p_proto_counts[AGGR_PROTO_COUNT];
	uint64_t		 p_rx_drops;
	uint32_t		 p_nselectch;
};

TAILQ_HEAD(aggr_port_list, aggr_port);

struct aggr_map {
	struct ifnet		*m_ifp0s[AGGR_MAX_PORTS];
};

struct aggr_softc {
	struct arpcom		 sc_ac;
#define sc_if			 sc_ac.ac_if
	unsigned int		 sc_dead;
	unsigned int		 sc_promisc;
	struct ifmedia		 sc_media;

	struct aggr_multiaddrs	 sc_multiaddrs;

	unsigned int		 sc_mix;

	struct aggr_map		 sc_maps[2];
	unsigned int		 sc_map_gen;
	struct aggr_map		*sc_map;

	struct rwlock		 sc_lock;
	struct aggr_port_list	 sc_ports;
	struct aggr_port_list	 sc_distributing;
	struct aggr_port_list	 sc_muxen;
	unsigned int		 sc_nports;
	unsigned int		 sc_ndistributing;

	struct timeout		 sc_tick;

	uint8_t			 sc_lacp_mode;
#define AGGR_LACP_MODE_PASSIVE		0
#define AGGR_LACP_MODE_ACTIVE		1
	uint8_t			 sc_lacp_timeout;
#define AGGR_LACP_TIMEOUT_SLOW		0
#define AGGR_LACP_TIMEOUT_FAST		1
	uint16_t		 sc_lacp_prio;
	uint16_t		 sc_lacp_port_prio;

	struct lacp_sysid	 sc_partner_system;
	uint16_t		 sc_partner_key;
};

#define DPRINTF(_sc, fmt...)	do { \
	if (ISSET((_sc)->sc_if.if_flags, IFF_DEBUG)) \
		printf(fmt); \
} while (0)

static const unsigned int aggr_periodic_times[] = {
	[AGGR_LACP_TIMEOUT_SLOW] = LACP_SLOW_PERIODIC_TIME,
	[AGGR_LACP_TIMEOUT_FAST] = LACP_FAST_PERIODIC_TIME,
};

static int	aggr_clone_create(struct if_clone *, int);
static int	aggr_clone_destroy(struct ifnet *);

static int	aggr_ioctl(struct ifnet *, u_long, caddr_t);
static void	aggr_start(struct ifqueue *);
static int	aggr_enqueue(struct ifnet *, struct mbuf *);

static int	aggr_media_change(struct ifnet *);
static void	aggr_media_status(struct ifnet *, struct ifmediareq *);

static int	aggr_up(struct aggr_softc *);
static int	aggr_down(struct aggr_softc *);
static int	aggr_iff(struct aggr_softc *);

static void	aggr_p_linkch(void *);
static void	aggr_p_detach(void *);
static int	aggr_p_ioctl(struct ifnet *, u_long, caddr_t);
static int	aggr_p_output(struct ifnet *, struct mbuf *,
		    struct sockaddr *, struct rtentry *);

static int	aggr_get_trunk(struct aggr_softc *, struct trunk_reqall *);
static int	aggr_set_options(struct aggr_softc *,
		    const struct trunk_opts *);
static int	aggr_get_options(struct aggr_softc *, struct trunk_opts *);
static int	aggr_set_lladdr(struct aggr_softc *, const struct ifreq *);
static int	aggr_set_mtu(struct aggr_softc *, uint32_t);
static void	aggr_p_dtor(struct aggr_softc *, struct aggr_port *,
		    const char *);
static int	aggr_p_setlladdr(struct aggr_port *, const uint8_t *);
static int	aggr_p_set_mtu(struct aggr_port *, uint32_t);
static int	aggr_add_port(struct aggr_softc *,
		    const struct trunk_reqport *);
static int	aggr_get_port(struct aggr_softc *, struct trunk_reqport *);
static int	aggr_del_port(struct aggr_softc *,
		    const struct trunk_reqport *);
static int	aggr_group(struct aggr_softc *, struct aggr_port *, u_long);
static int	aggr_multi(struct aggr_softc *, struct aggr_port *,
		    const struct aggr_multiaddr *, u_long);
static void	aggr_update_capabilities(struct aggr_softc *);
static void	aggr_set_lacp_mode(struct aggr_softc *, int);
static void	aggr_set_lacp_timeout(struct aggr_softc *, int);
static int	aggr_multi_add(struct aggr_softc *, struct ifreq *);
static int	aggr_multi_del(struct aggr_softc *, struct ifreq *);

static void	aggr_map(struct aggr_softc *);

static void	aggr_record_default(struct aggr_softc *, struct aggr_port *);
static void	aggr_current_while_timer(void *);
static void	aggr_wait_while_timer(void *);
static void	aggr_rx(void *);
static void	aggr_rxm_ev(struct aggr_softc *, struct aggr_port *,
		    enum lacp_rxm_event, const struct lacp_du *);
#define aggr_rxm(_sc, _p, _ev) \
		aggr_rxm_ev((_sc), (_p), (_ev), NULL)
#define aggr_rxm_lacpdu(_sc, _p, _lacpdu) \
		aggr_rxm_ev((_sc), (_p), LACP_RXM_E_LACPDU, (_lacpdu))

static void	aggr_mux(struct aggr_softc *, struct aggr_port *,
		    enum lacp_mux_event);
static int	aggr_mux_ev(struct aggr_softc *, struct aggr_port *,
		    enum lacp_mux_event, int *);

static void	aggr_set_partner_timeout(struct aggr_port *, int);

static void	aggr_ptm_tx(void *);

static void	aggr_transmit_machine(void *);
static void	aggr_ntt(struct aggr_port *);
static void	aggr_ntt_transmit(struct aggr_port *);

static void	aggr_set_selected(struct aggr_port *, enum aggr_port_selected,
		    enum lacp_mux_event);
static void	aggr_unselected(struct aggr_port *);

static void	aggr_selection_logic(struct aggr_softc *, struct aggr_port *);

#if NKSTAT > 0
static void	aggr_port_kstat_attach(struct aggr_port *);
static void	aggr_port_kstat_detach(struct aggr_port *);
#endif

static struct if_clone aggr_cloner =
    IF_CLONE_INITIALIZER("aggr", aggr_clone_create, aggr_clone_destroy);

void
aggrattach(int count)
{
	if_clone_attach(&aggr_cloner);
}

static int
aggr_clone_create(struct if_clone *ifc, int unit)
{
	struct aggr_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO|M_CANFAIL);
	if (sc == NULL)
		return (ENOMEM);

	sc->sc_mix = arc4random();

	ifp = &sc->sc_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	TAILQ_INIT(&sc->sc_multiaddrs);
	rw_init(&sc->sc_lock, "aggrlk");
	TAILQ_INIT(&sc->sc_ports);
	sc->sc_nports = 0;
	TAILQ_INIT(&sc->sc_distributing);
	sc->sc_ndistributing = 0;
	TAILQ_INIT(&sc->sc_muxen);

	sc->sc_map_gen = 0;
	sc->sc_map = NULL; /* no links yet */

	sc->sc_lacp_mode = AGGR_LACP_MODE_ACTIVE;
	sc->sc_lacp_timeout = AGGR_LACP_TIMEOUT_SLOW;
	sc->sc_lacp_prio = 0x8000; /* medium */
	sc->sc_lacp_port_prio = 0x8000; /* medium */

	ifmedia_init(&sc->sc_media, 0, aggr_media_change, aggr_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	ifp->if_softc = sc;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_ioctl = aggr_ioctl;
	ifp->if_qstart = aggr_start;
	ifp->if_enqueue = aggr_enqueue;
	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST | IFF_SIMPLEX;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;
	ifp->if_link_state = LINK_STATE_DOWN;
	ether_fakeaddr(ifp);

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

	ifp->if_llprio = IFQ_MAXPRIO;

	return (0);
}

static int
aggr_clone_destroy(struct ifnet *ifp)
{
	struct aggr_softc *sc = ifp->if_softc;
	struct aggr_port *p;

	NET_LOCK();
	sc->sc_dead = 1;

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		aggr_down(sc);
	NET_UNLOCK();

	ether_ifdetach(ifp);
	if_detach(ifp);

	/* last ref, no need to lock. aggr_p_dtor locks anyway */
	NET_LOCK();
	while ((p = TAILQ_FIRST(&sc->sc_ports)) != NULL)
		aggr_p_dtor(sc, p, "destroy");
	NET_UNLOCK();

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

/*
 * LACP_Enabled
 */
static inline int
aggr_lacp_enabled(struct aggr_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	return (ISSET(ifp->if_flags, IFF_RUNNING));
}

/*
 * port_enabled
 */
static int
aggr_port_enabled(struct aggr_port *p)
{
	struct ifnet *ifp0 = p->p_ifp0;

	if (!ISSET(ifp0->if_flags, IFF_RUNNING))
		return (0);

	if (!LINK_STATE_IS_UP(ifp0->if_link_state))
		return (0);

	return (1);
}

/*
 * port_moved
 *
 * This variable is set to TRUE if the Receive machine for an Aggregation
 * Port is in the PORT_DISABLED state, and the combination of
 * Partner_Oper_System and Partner_Oper_Port_Number in use by that
 * Aggregation Port has been received in an incoming LACPDU on a
 * different Aggregation Port. This variable is set to FALSE once the
 * INITIALIZE state of the Receive machine has set the Partner information
 * for the Aggregation Port to administrative default values.
 *
 * Value: Boolean
*/
static int
aggr_port_moved(struct aggr_softc *sc, struct aggr_port *p)
{
	return (0);
}

static void
aggr_transmit(struct aggr_softc *sc, const struct aggr_map *map, struct mbuf *m)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	uint16_t flow = 0;

#if NBPFILTER > 0
	{
		caddr_t if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_ether(if_bpf, m, BPF_DIRECTION_OUT);
	}
#endif

	if (ISSET(m->m_pkthdr.csum_flags, M_FLOWID))
		flow = m->m_pkthdr.ph_flowid >> AGGR_FLOWID_SHIFT;

	ifp0 = map->m_ifp0s[flow % AGGR_MAX_PORTS];

	if (if_enqueue(ifp0, m) != 0)
		counters_inc(ifp->if_counters, ifc_oerrors);
}

static int
aggr_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	struct aggr_softc *sc;
	const struct aggr_map *map;
	int error = 0;

	if (!ifq_is_priq(&ifp->if_snd))
		return (if_enqueue_ifq(ifp, m));

	sc = ifp->if_softc;

	smr_read_enter();
	map = SMR_PTR_GET(&sc->sc_map);
	if (__predict_false(map == NULL)) {
		m_freem(m);
		error = ENETDOWN;
	} else {
		counters_pkt(ifp->if_counters,
		    ifc_opackets, ifc_obytes, m->m_pkthdr.len);
		aggr_transmit(sc, map, m);
	}
	smr_read_leave();

	return (error);
}

static void
aggr_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct aggr_softc *sc = ifp->if_softc;
	const struct aggr_map *map;

	smr_read_enter();
	map = SMR_PTR_GET(&sc->sc_map);
	if (__predict_false(map == NULL))
		ifq_purge(ifq);
	else {
		struct mbuf *m;

		while ((m = ifq_dequeue(ifq)) != NULL)
			aggr_transmit(sc, map, m);
	}
	smr_read_leave();
}

static inline struct mbuf *
aggr_input_control(struct aggr_port *p, struct mbuf *m, struct netstack *ns)
{
	struct ether_header *eh;
	int hlen = sizeof(*eh);
	uint16_t etype;
	uint64_t dst;

	if (ISSET(m->m_flags, M_VLANTAG))
		return (m);

	eh = mtod(m, struct ether_header *);
	etype = eh->ether_type;
	dst = ether_addr_to_e64((struct ether_addr *)eh->ether_dhost);

	if (__predict_false(etype == htons(ETHERTYPE_SLOW) &&
	    dst == LACP_ADDR_SLOW_E64)) {
		unsigned int rx_proto = AGGR_PROTO_RX_LACP;
		struct ether_slowproto_hdr *sph;
		int drop = 0;

		hlen += sizeof(*sph);
		if (m->m_len < hlen) {
			m = m_pullup(m, hlen);
			if (m == NULL) {
				/* short++ */
				return (NULL);
			}
			eh = mtod(m, struct ether_header *);
		}

		sph = (struct ether_slowproto_hdr *)(eh + 1);
		switch (sph->sph_subtype) {
		case SLOWPROTOCOLS_SUBTYPE_LACP_MARKER:
			rx_proto = AGGR_PROTO_RX_MARKER;
			/* FALLTHROUGH */
		case SLOWPROTOCOLS_SUBTYPE_LACP:
			mtx_enter(&p->p_mtx);
			p->p_proto_counts[rx_proto].c_pkts++;
			p->p_proto_counts[rx_proto].c_bytes += m->m_pkthdr.len;

			if (ml_len(&p->p_rxm_ml) < AGGR_MAX_SLOW_PKTS)
				ml_enqueue(&p->p_rxm_ml, m);
			else {
				p->p_rx_drops++;
				drop = 1;
			}
			mtx_leave(&p->p_mtx);

			if (drop)
				goto drop;
			else
				task_add(systq, &p->p_rxm_task);
			return (NULL);
		default:
			break;
		}
	} else if (__predict_false(etype == htons(ETHERTYPE_LLDP) &&
	    ETH64_IS_8021_RSVD(dst))) {
		/* look at the last nibble of the 802.1 reserved address */
		switch (dst & 0xf) {
		case 0x0: /* Nearest Customer Bridge */
		case 0x3: /* Non-TPMR Bridge */
		case 0xe: /* Nearest Bridge */
			p->p_input(p->p_ifp0, m, ns);
			return (NULL);
		default:
			break;
		}
	}

	return (m);

drop:
	m_freem(m);
	return (NULL);
}

static void
aggr_input(struct ifnet *ifp0, struct mbuf *m, struct netstack *ns)
{
	struct arpcom *ac0 = (struct arpcom *)ifp0;
	struct aggr_port *p = ac0->ac_trunkport;
	struct aggr_softc *sc = p->p_aggr;
	struct ifnet *ifp = &sc->sc_if;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		goto drop;

	m = aggr_input_control(p, m, ns);
	if (m == NULL)
		return;

	if (__predict_false(!p->p_collecting))
		goto drop;

	if (!ISSET(m->m_pkthdr.csum_flags, M_FLOWID))
		m->m_pkthdr.ph_flowid = ifp0->if_index ^ sc->sc_mix;

	if_vinput(ifp, m, ns);

	return;

drop:
	m_freem(m);
}

static int
aggr_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct aggr_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct aggr_port *p;
	int error = 0;

	if (sc->sc_dead)
		return (ENXIO);

	switch (cmd) {
	case SIOCSIFADDR:
		break;

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = aggr_up(sc);
			else
				error = ENETRESET;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = aggr_down(sc);
		}
		break;

	case SIOCSIFXFLAGS:
		TAILQ_FOREACH(p, &sc->sc_ports, p_entry)
			ifsetlro(p->p_ifp0, ISSET(ifr->ifr_flags, IFXF_LRO));
		break;

	case SIOCSIFLLADDR:
		error = aggr_set_lladdr(sc, ifr);
		break;

	case SIOCSTRUNK:
		error = suser(curproc);
		if (error != 0)
			break;

		if (((struct trunk_reqall *)data)->ra_proto !=
		    TRUNK_PROTO_LACP) {
			error = EPROTONOSUPPORT;
			break;
		}

		/* nop */
		break;
	case SIOCGTRUNK:
		error = aggr_get_trunk(sc, (struct trunk_reqall *)data);
		break;

	case SIOCSTRUNKOPTS:
		error = suser(curproc);
		if (error != 0)
			break;

		error = aggr_set_options(sc, (struct trunk_opts *)data);
		break;

	case SIOCGTRUNKOPTS:
		error = aggr_get_options(sc, (struct trunk_opts *)data);
		break;

	case SIOCGTRUNKPORT:
		error = aggr_get_port(sc, (struct trunk_reqport *)data);
		break;
	case SIOCSTRUNKPORT:
		error = suser(curproc);
		if (error != 0)
			break;

		error = aggr_add_port(sc, (struct trunk_reqport *)data);
		break;
	case SIOCSTRUNKDELPORT:
		error = suser(curproc);
		if (error != 0)
			break;

		error = aggr_del_port(sc, (struct trunk_reqport *)data);
		break;

	case SIOCSIFMTU:
		error = aggr_set_mtu(sc, ifr->ifr_mtu);
		break;

	case SIOCADDMULTI:
		error = aggr_multi_add(sc, ifr);
		break;
	case SIOCDELMULTI:
		error = aggr_multi_del(sc, ifr);
		break;

	case SIOCSIFMEDIA:
		error = EOPNOTSUPP;
		break;
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET)
		error = aggr_iff(sc);

	return (error);
}

static int
aggr_get_trunk(struct aggr_softc *sc, struct trunk_reqall *ra)
{
	struct ifnet *ifp = &sc->sc_if;
	struct trunk_reqport rp;
	struct aggr_port *p;
	size_t size = ra->ra_size;
	caddr_t ubuf = (caddr_t)ra->ra_port;
	struct lacp_opreq *req;
	uint8_t state = 0;
	int error = 0;

	if (sc->sc_lacp_mode == AGGR_LACP_MODE_ACTIVE)
		SET(state, LACP_STATE_ACTIVITY);
	if (sc->sc_lacp_timeout == AGGR_LACP_TIMEOUT_FAST)
		SET(state, LACP_STATE_TIMEOUT);

	ra->ra_proto = TRUNK_PROTO_LACP;
	memset(&ra->ra_psc, 0, sizeof(ra->ra_psc));

	/*
	 * aggr(4) does not support Individual links so don't bother
	 * with portprio, portno, and state, as per the spec.
	 */

	req = &ra->ra_lacpreq;
	req->actor_prio = sc->sc_lacp_prio;
	CTASSERT(sizeof(req->actor_mac) == sizeof(sc->sc_ac.ac_enaddr));
	memcpy(req->actor_mac, &sc->sc_ac.ac_enaddr, sizeof(req->actor_mac));
	req->actor_key = ifp->if_index;
	req->actor_state = state;

	req->partner_prio = ntohs(sc->sc_partner_system.lacp_sysid_priority);
	CTASSERT(sizeof(req->partner_mac) ==
	    sizeof(sc->sc_partner_system.lacp_sysid_mac));
	memcpy(req->partner_mac, sc->sc_partner_system.lacp_sysid_mac,
	    sizeof(req->partner_mac));
	req->partner_key = ntohs(sc->sc_partner_key);

	ra->ra_ports = sc->sc_nports;
	TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
		struct ifnet *ifp0;
		struct lacp_opreq *opreq;

		if (size < sizeof(rp))
			break;

		ifp0 = p->p_ifp0;

		CTASSERT(sizeof(rp.rp_ifname) == sizeof(ifp->if_xname));
		CTASSERT(sizeof(rp.rp_portname) == sizeof(ifp0->if_xname));

		memset(&rp, 0, sizeof(rp));
		memcpy(rp.rp_ifname, ifp->if_xname, sizeof(rp.rp_ifname));
		memcpy(rp.rp_portname, ifp0->if_xname, sizeof(rp.rp_portname));

		if (p->p_muxed)
			SET(rp.rp_flags, TRUNK_PORT_ACTIVE);
		if (p->p_collecting)
			SET(rp.rp_flags, TRUNK_PORT_COLLECTING);
		if (p->p_distributing)
			SET(rp.rp_flags, TRUNK_PORT_DISTRIBUTING);
		if (!aggr_port_enabled(p))
			SET(rp.rp_flags, TRUNK_PORT_DISABLED);

		opreq = &rp.rp_lacpreq;

		opreq->actor_prio = sc->sc_lacp_prio;
		memcpy(opreq->actor_mac, &sc->sc_ac.ac_enaddr,
		    sizeof(req->actor_mac));
		opreq->actor_key = ifp->if_index;
		opreq->actor_portprio = sc->sc_lacp_port_prio;
		opreq->actor_portno = ifp0->if_index;
		opreq->actor_state = state | p->p_actor_state;

		opreq->partner_prio =
		    ntohs(p->p_partner.lacp_sysid.lacp_sysid_priority);
		CTASSERT(sizeof(opreq->partner_mac) ==
		    sizeof(p->p_partner.lacp_sysid.lacp_sysid_mac));
		memcpy(opreq->partner_mac,
		    p->p_partner.lacp_sysid.lacp_sysid_mac,
		    sizeof(opreq->partner_mac));
		opreq->partner_key = ntohs(p->p_partner.lacp_key);
		opreq->partner_portprio =
		    ntohs(p->p_partner.lacp_portid.lacp_portid_priority);
		opreq->partner_portno =
		    ntohs(p->p_partner.lacp_portid.lacp_portid_number);
		opreq->partner_state = p->p_partner_state;

		error = copyout(&rp, ubuf, sizeof(rp));
		if (error != 0)
			break;

		ubuf += sizeof(rp);
		size -= sizeof(rp);
	}

	return (error);
}

static int
aggr_get_options(struct aggr_softc *sc, struct trunk_opts *tro)
{
	struct lacp_adminopts *opt = &tro->to_lacpopts;

	if (tro->to_proto != TRUNK_PROTO_LACP)
		return (EPROTONOSUPPORT);

	opt->lacp_mode = sc->sc_lacp_mode;
	opt->lacp_timeout = sc->sc_lacp_timeout;
	opt->lacp_prio = sc->sc_lacp_prio;
	opt->lacp_portprio = sc->sc_lacp_port_prio;
	opt->lacp_ifqprio = sc->sc_if.if_llprio;

	return (0);
}

static int
aggr_set_options(struct aggr_softc *sc, const struct trunk_opts *tro)
{
	const struct lacp_adminopts *opt = &tro->to_lacpopts;

	if (tro->to_proto != TRUNK_PROTO_LACP)
		return (EPROTONOSUPPORT);

	switch (tro->to_opts) {
	case TRUNK_OPT_LACP_MODE:
		switch (opt->lacp_mode) {
		case AGGR_LACP_MODE_PASSIVE:
		case AGGR_LACP_MODE_ACTIVE:
			break;
		default:
			return (EINVAL);
		}

		aggr_set_lacp_mode(sc, opt->lacp_mode);
		break;

	case TRUNK_OPT_LACP_TIMEOUT:
		if (opt->lacp_timeout >= nitems(aggr_periodic_times))
			return (EINVAL);

		aggr_set_lacp_timeout(sc, opt->lacp_timeout);
		break;

	case TRUNK_OPT_LACP_SYS_PRIO:
		if (opt->lacp_prio == 0)
			return (EINVAL);

		sc->sc_lacp_prio = opt->lacp_prio;
		break;

	case TRUNK_OPT_LACP_PORT_PRIO:
		if (opt->lacp_portprio == 0)
			return (EINVAL);

		sc->sc_lacp_port_prio = opt->lacp_portprio;
		break;

	default:
		return (ENODEV);
	}

	return (0);
}

static int
aggr_add_port(struct aggr_softc *sc, const struct trunk_reqport *rp)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	struct arpcom *ac0;
	struct aggr_port *p;
	struct aggr_multiaddr *ma;
	int past = ticks - (hz * LACP_TIMEOUT_FACTOR);
	int i;
	int error;

	NET_ASSERT_LOCKED();
	if (sc->sc_nports > AGGR_MAX_PORTS)
		return (ENOSPC);

	ifp0 = if_unit(rp->rp_portname);
	if (ifp0 == NULL)
		return (EINVAL);

	if (ifp0->if_index == ifp->if_index) {
		error = EINVAL;
		goto put;
	}

	if (ifp0->if_type != IFT_ETHER) {
		error = EPROTONOSUPPORT;
		goto put;
	}

	error = ether_brport_isset(ifp0);
	if (error != 0)
		goto put;

	if (ifp0->if_hardmtu < ifp->if_mtu) {
		error = ENOBUFS;
		goto put;
	}

	ac0 = (struct arpcom *)ifp0;
	if (ac0->ac_trunkport != NULL) {
		error = EBUSY;
		goto put;
	}

	/* let's try */

	p = malloc(sizeof(*p), M_DEVBUF, M_WAITOK|M_ZERO|M_CANFAIL);
	if (p == NULL) {
		error = ENOMEM;
		goto put;
	}

	for (i = 0; i < nitems(p->p_txm_log); i++)
		p->p_txm_log[i] = past;

	p->p_ifp0 = ifp0;
	p->p_aggr = sc;
	p->p_mtu = ifp0->if_mtu;
	mtx_init(&p->p_mtx, IPL_SOFTNET);

	CTASSERT(sizeof(p->p_lladdr) == sizeof(ac0->ac_enaddr));
	memcpy(p->p_lladdr, ac0->ac_enaddr, sizeof(p->p_lladdr));
	p->p_ioctl = ifp0->if_ioctl;
	p->p_input = ifp0->if_input;
	p->p_output = ifp0->if_output;

	error = aggr_group(sc, p, SIOCADDMULTI);
	if (error != 0)
		goto free;

	error = aggr_p_setlladdr(p, sc->sc_ac.ac_enaddr);
	if (error != 0)
		goto ungroup;

	error = aggr_p_set_mtu(p, ifp->if_mtu);
	if (error != 0)
		goto resetlladdr;

	if (sc->sc_promisc) {
		error = ifpromisc(ifp0, 1);
		if (error != 0)
			goto unmtu;
	}

	TAILQ_FOREACH(ma, &sc->sc_multiaddrs, m_entry) {
		if (aggr_multi(sc, p, ma, SIOCADDMULTI) != 0) {
			log(LOG_WARNING, "%s %s: "
			    "unable to add multicast address\n",
			    ifp->if_xname, ifp0->if_xname);
		}
	}

	task_set(&p->p_lhook, aggr_p_linkch, p);
	if_linkstatehook_add(ifp0, &p->p_lhook);

	task_set(&p->p_dhook, aggr_p_detach, p);
	if_detachhook_add(ifp0, &p->p_dhook);

	task_set(&p->p_rxm_task, aggr_rx, p);
	ml_init(&p->p_rxm_ml);

	timeout_set_proc(&p->p_ptm_tx, aggr_ptm_tx, p);
	timeout_set_proc(&p->p_txm_ntt, aggr_transmit_machine, p);
	timeout_set_proc(&p->p_current_while_timer,
	    aggr_current_while_timer, p);
	timeout_set_proc(&p->p_wait_while_timer, aggr_wait_while_timer, p);

	p->p_muxed = 0;
	p->p_collecting = 0;
	p->p_distributing = 0;
	p->p_selected = AGGR_PORT_UNSELECTED;
	p->p_actor_state = LACP_STATE_AGGREGATION;

	/* commit */
	DPRINTF(sc, "%s %s trunkport: creating port\n",
	    ifp->if_xname, ifp0->if_xname);

#if NKSTAT > 0
	aggr_port_kstat_attach(p); /* this prints warnings itself */
#endif

	TAILQ_INSERT_TAIL(&sc->sc_ports, p, p_entry);
	sc->sc_nports++;

	aggr_update_capabilities(sc);

	/*
	 * use (and modification) of ifp->if_input and ac->ac_trunkport
	 * is protected by NET_LOCK.
	 */

	ac0->ac_trunkport = p;

	/* make sure p is visible before handlers can run */
	membar_producer();
	ifp0->if_ioctl = aggr_p_ioctl;
	ifp0->if_input = aggr_input;
	ifp0->if_output = aggr_p_output;

	aggr_mux(sc, p, LACP_MUX_E_BEGIN);
	aggr_rxm(sc, p, LACP_RXM_E_BEGIN);
	aggr_p_linkch(p);

	return (0);

unmtu:
	if (aggr_p_set_mtu(p, p->p_mtu) != 0) {
		log(LOG_WARNING, "%s add %s: unable to reset mtu %u\n",
		    ifp->if_xname, ifp0->if_xname, p->p_mtu);
	}
resetlladdr:
	if (aggr_p_setlladdr(p, p->p_lladdr) != 0) {
		log(LOG_WARNING, "%s add %s: unable to reset lladdr\n",
		    ifp->if_xname, ifp0->if_xname);
	}
ungroup:
	if (aggr_group(sc, p, SIOCDELMULTI) != 0) {
		log(LOG_WARNING, "%s add %s: "
		    "unable to remove LACP group address\n",
		    ifp->if_xname, ifp0->if_xname);
	}
free:
	free(p, M_DEVBUF, sizeof(*p));
put:
	if_put(ifp0);
	return (error);
}

static struct aggr_port *
aggr_trunkport(struct aggr_softc *sc, const char *name)
{
	struct aggr_port *p;

	TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
		if (strcmp(p->p_ifp0->if_xname, name) == 0)
			return (p);
	}

	return (NULL);
}

static int
aggr_get_port(struct aggr_softc *sc, struct trunk_reqport *rp)
{
	struct aggr_port *p;

	NET_ASSERT_LOCKED();
	p = aggr_trunkport(sc, rp->rp_portname);
	if (p == NULL)
		return (EINVAL);

	/* XXX */

	return (0);
}

static int
aggr_del_port(struct aggr_softc *sc, const struct trunk_reqport *rp)
{
	struct aggr_port *p;

	NET_ASSERT_LOCKED();
	p = aggr_trunkport(sc, rp->rp_portname);
	if (p == NULL)
		return (EINVAL);

	aggr_p_dtor(sc, p, "del");

	return (0);
}

static int
aggr_p_setlladdr(struct aggr_port *p, const uint8_t *addr)
{
	struct ifnet *ifp0 = p->p_ifp0;
	struct ifreq ifr;
	struct sockaddr *sa;
	int error;

	memset(&ifr, 0, sizeof(ifr));

	CTASSERT(sizeof(ifr.ifr_name) == sizeof(ifp0->if_xname));
	memcpy(ifr.ifr_name, ifp0->if_xname, sizeof(ifr.ifr_name));

	sa = &ifr.ifr_addr;

	/* wtf is this? */
	sa->sa_len = ETHER_ADDR_LEN;
	sa->sa_family = AF_LINK;
	CTASSERT(sizeof(sa->sa_data) >= ETHER_ADDR_LEN);
	memcpy(sa->sa_data, addr, ETHER_ADDR_LEN);

	error = (*p->p_ioctl)(ifp0, SIOCSIFLLADDR, (caddr_t)&ifr);
	switch (error) {
	case ENOTTY:
	case 0:
		break;
	default:
		return (error);
	}

	error = if_setlladdr(ifp0, addr);
	if (error != 0)
		return (error);

	ifnewlladdr(ifp0);

	return (0);
}

static int
aggr_p_set_mtu(struct aggr_port *p, uint32_t mtu)
{
	struct ifnet *ifp0 = p->p_ifp0;
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));

	CTASSERT(sizeof(ifr.ifr_name) == sizeof(ifp0->if_xname));
	memcpy(ifr.ifr_name, ifp0->if_xname, sizeof(ifr.ifr_name));

	ifr.ifr_mtu = mtu;

	return ((*p->p_ioctl)(ifp0, SIOCSIFMTU, (caddr_t)&ifr));
}

static int
aggr_p_ioctl(struct ifnet *ifp0, u_long cmd, caddr_t data)
{
	struct arpcom *ac0 = (struct arpcom *)ifp0;
	struct aggr_port *p = ac0->ac_trunkport;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (cmd) {
	case SIOCGTRUNKPORT: {
		struct trunk_reqport *rp = (struct trunk_reqport *)data;
		struct aggr_softc *sc = p->p_aggr;
		struct ifnet *ifp = &sc->sc_if;

		if (strncmp(rp->rp_ifname, rp->rp_portname,
		    sizeof(rp->rp_ifname)) != 0)
			return (EINVAL);

		CTASSERT(sizeof(rp->rp_ifname) == sizeof(ifp->if_xname));
		memcpy(rp->rp_ifname, ifp->if_xname, sizeof(rp->rp_ifname));
		break;
	}

	case SIOCSIFMTU:
		if (ifr->ifr_mtu == ifp0->if_mtu)
			break; /* nop */

		/* FALLTHROUGH */
	case SIOCSIFLLADDR:
		error = EBUSY;
		break;

	case SIOCSIFFLAGS:
		if (!ISSET(ifp0->if_flags, IFF_UP) &&
		    ISSET(ifp0->if_flags, IFF_RUNNING)) {
			/* port is going down */
			if (p->p_selected == AGGR_PORT_SELECTED) {
				aggr_unselected(p);
				aggr_ntt_transmit(p); /* XXX */
			}
		}
		/* FALLTHROUGH */
	default:
		error = (*p->p_ioctl)(ifp0, cmd, data);
		break;
	}

	return (error);
}

static int
aggr_p_output(struct ifnet *ifp0, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	struct arpcom *ac0 = (struct arpcom *)ifp0;
	struct aggr_port *p = ac0->ac_trunkport;

	/* restrict transmission to bpf only */
	if (m_tag_find(m, PACKET_TAG_DLT, NULL) == NULL) {
		m_freem(m);
		return (EBUSY);
	}

	return ((*p->p_output)(ifp0, m, dst, rt));
}

static void
aggr_p_dtor(struct aggr_softc *sc, struct aggr_port *p, const char *op)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0 = p->p_ifp0;
	struct arpcom *ac0 = (struct arpcom *)ifp0;
	struct aggr_multiaddr *ma;
	enum aggr_port_selected selected;
	int error;

	DPRINTF(sc, "%s %s %s: destroying port\n",
	    ifp->if_xname, ifp0->if_xname, op);

	selected = p->p_selected;
	aggr_rxm(sc, p, LACP_RXM_E_NOT_PORT_ENABLED);
	aggr_unselected(p);
	if (aggr_port_enabled(p) && selected == AGGR_PORT_SELECTED)
		aggr_ntt_transmit(p);

	timeout_del(&p->p_ptm_tx);
	timeout_del_barrier(&p->p_txm_ntt); /* XXX */
	timeout_del(&p->p_current_while_timer);
	timeout_del(&p->p_wait_while_timer);

	/*
	 * use (and modification) of ifp->if_input and ac->ac_trunkport
	 * is protected by NET_LOCK.
	 */

	ac0->ac_trunkport = NULL;
	ifp0->if_input = p->p_input;
	ifp0->if_ioctl = p->p_ioctl;
	ifp0->if_output = p->p_output;

#if NKSTAT > 0
	aggr_port_kstat_detach(p);
#endif

	TAILQ_REMOVE(&sc->sc_ports, p, p_entry);
	sc->sc_nports--;

	TAILQ_FOREACH(ma, &sc->sc_multiaddrs, m_entry) {
		error = aggr_multi(sc, p, ma, SIOCDELMULTI);
		if (error != 0) {
			log(LOG_WARNING, "%s %s %s: "
			    "unable to remove multicast address (%d)\n",
			    ifp->if_xname, op, ifp0->if_xname, error);
		}
	}

	if (sc->sc_promisc) {
		error = ifpromisc(ifp0, 0);
		if (error != 0) {
			log(LOG_WARNING, "%s %s %s: "
			    "unable to disable promisc (%d)\n",
			    ifp->if_xname, op, ifp0->if_xname, error);
		}
	}

	error = aggr_p_set_mtu(p, p->p_mtu);
	if (error != 0) {
		log(LOG_WARNING, "%s %s %s: unable to restore mtu %u (%d)\n",
		    ifp->if_xname, op, ifp0->if_xname, p->p_mtu, error);
	}

	error = aggr_p_setlladdr(p, p->p_lladdr);
	if (error != 0) {
		log(LOG_WARNING, "%s %s %s: unable to restore lladdr (%d)\n",
		    ifp->if_xname, op, ifp0->if_xname, error);
	}

	error = aggr_group(sc, p, SIOCDELMULTI);
	if (error != 0) {
		log(LOG_WARNING, "%s %s %s: "
		    "unable to remove LACP group address (%d)\n",
		    ifp->if_xname, op, ifp0->if_xname, error);
	}

	if_detachhook_del(ifp0, &p->p_dhook);
	if_linkstatehook_del(ifp0, &p->p_lhook);
	if_put(ifp0);
	free(p, M_DEVBUF, sizeof(*p));

	/* XXX this is a pretty ugly place to update this */
	aggr_update_capabilities(sc);
}

static void
aggr_p_detach(void *arg)
{
	struct aggr_port *p = arg;
	struct aggr_softc *sc = p->p_aggr;

	aggr_p_dtor(sc, p, "detach");

	NET_ASSERT_LOCKED();
}

static void
aggr_p_linkch(void *arg)
{
	struct aggr_port *p = arg;
	struct aggr_softc *sc = p->p_aggr;

	NET_ASSERT_LOCKED();

	if (aggr_port_enabled(p)) {
		aggr_rxm(sc, p, LACP_RXM_E_PORT_ENABLED);

		if (aggr_lacp_enabled(sc)) {
			timeout_add_sec(&p->p_ptm_tx,
			    aggr_periodic_times[AGGR_LACP_TIMEOUT_FAST]);
		}
	} else {
		aggr_rxm(sc, p, LACP_RXM_E_NOT_PORT_ENABLED);
		aggr_unselected(p);
		aggr_record_default(sc, p);
		timeout_del(&p->p_ptm_tx);
	}
}

static void
aggr_map(struct aggr_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	struct aggr_map *map = NULL;
	struct aggr_port *p;
	unsigned int gen;
	unsigned int i;
	int link_state = LINK_STATE_DOWN;

	p = TAILQ_FIRST(&sc->sc_distributing);
	if (p != NULL) {
		gen = sc->sc_map_gen++;
		map = &sc->sc_maps[gen % nitems(sc->sc_maps)];

		for (i = 0; i < nitems(map->m_ifp0s); i++) {
			map->m_ifp0s[i] = p->p_ifp0;

			p = TAILQ_NEXT(p, p_entry_distributing);
			if (p == NULL)
				p = TAILQ_FIRST(&sc->sc_distributing);
		}

		link_state = LINK_STATE_FULL_DUPLEX;
	}

	SMR_PTR_SET_LOCKED(&sc->sc_map, map);
	smr_barrier();

	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}

static void
aggr_current_while_timer(void *arg)
{
	struct aggr_port *p = arg;
	struct aggr_softc *sc = p->p_aggr;

	aggr_rxm(sc, p, LACP_RXM_E_TIMER_EXPIRED);
}

static void
aggr_wait_while_timer(void *arg)
{
	struct aggr_port *p = arg;
	struct aggr_softc *sc = p->p_aggr;

	aggr_selection_logic(sc, p);
}

static void
aggr_start_current_while_timer(struct aggr_port *p, unsigned int t)
{
	timeout_add_sec(&p->p_current_while_timer,
		aggr_periodic_times[t] * LACP_TIMEOUT_FACTOR);
}

static void
aggr_input_lacpdu(struct aggr_port *p, struct mbuf *m)
{
	struct aggr_softc *sc = p->p_aggr;
	struct lacp_du *lacpdu;

	if (m->m_len < sizeof(*lacpdu)) {
		m = m_pullup(m, sizeof(*lacpdu));
		if (m == NULL)
			return;
	}

	/*
	 * In the process of executing the recordPDU function, a Receive
	 * machine compliant to this standard shall not validate the
	 * Version Number, TLV_type, or Reserved fields in received
	 * LACPDUs. The same actions are taken regardless of the values
	 * received in these fields. A Receive machine may validate
	 * the Actor_Information_Length, Partner_Information_Length,
	 * Collector_Information_Length, or Terminator_Length fields.
	 */

	lacpdu = mtod(m, struct lacp_du *);
	aggr_rxm_lacpdu(sc, p, lacpdu);

	m_freem(m);
}

static void
aggr_update_selected(struct aggr_softc *sc, struct aggr_port *p,
    const struct lacp_du *lacpdu)
{
	const struct lacp_port_info *rpi = &lacpdu->lacp_actor_info;
	const struct lacp_port_info *lpi = &p->p_partner;

	if ((rpi->lacp_portid.lacp_portid_number ==
	     lpi->lacp_portid.lacp_portid_number) &&
	    (rpi->lacp_portid.lacp_portid_priority ==
	     lpi->lacp_portid.lacp_portid_priority) &&
	    ETHER_IS_EQ(rpi->lacp_sysid.lacp_sysid_mac,
	     lpi->lacp_sysid.lacp_sysid_mac) &&
	    (rpi->lacp_sysid.lacp_sysid_priority ==
	     lpi->lacp_sysid.lacp_sysid_priority) &&
	    (rpi->lacp_key == lpi->lacp_key) &&
	    (ISSET(rpi->lacp_state, LACP_STATE_AGGREGATION) ==
	     ISSET(lpi->lacp_state, LACP_STATE_AGGREGATION)))
		return;

	aggr_unselected(p);
}

static void
aggr_record_default(struct aggr_softc *sc, struct aggr_port *p)
{
	struct lacp_port_info *pi = &p->p_partner;

	pi->lacp_sysid.lacp_sysid_priority = htons(0);
	memset(pi->lacp_sysid.lacp_sysid_mac, 0,
	    sizeof(pi->lacp_sysid.lacp_sysid_mac));

	pi->lacp_key = htons(0);

	pi->lacp_portid.lacp_portid_priority = htons(0);
	pi->lacp_portid.lacp_portid_number = htons(0);

	SET(p->p_actor_state, LACP_STATE_DEFAULTED);

	pi->lacp_state = LACP_STATE_AGGREGATION | LACP_STATE_SYNC;
	if (sc->sc_lacp_timeout == AGGR_LACP_TIMEOUT_FAST)
		SET(pi->lacp_state, LACP_STATE_TIMEOUT);
	if (sc->sc_lacp_mode == AGGR_LACP_MODE_ACTIVE)
		SET(pi->lacp_state, LACP_STATE_ACTIVITY);

	/* notify Mux */
	aggr_mux(sc, p, LACP_MUX_E_NOT_COLLECTING);
	aggr_mux(sc, p, LACP_MUX_E_SYNC);
}

static void
aggr_update_default_selected(struct aggr_softc *sc, struct aggr_port *p)
{
	const struct lacp_port_info *pi = &p->p_partner;

	if ((pi->lacp_portid.lacp_portid_number == htons(0)) &&
	    (pi->lacp_portid.lacp_portid_priority == htons(0)) &&
	    ETHER_IS_ANYADDR(pi->lacp_sysid.lacp_sysid_mac) &&
	    (pi->lacp_sysid.lacp_sysid_priority == htons(0)) &&
	    (pi->lacp_key == htons(0)) &&
	    ISSET(pi->lacp_state, LACP_STATE_AGGREGATION))
		return;

	aggr_unselected(p);
	aggr_selection_logic(sc, p); /* restart */
}

static int
aggr_update_ntt(struct aggr_port *p, const struct lacp_du *lacpdu)
{
	struct aggr_softc *sc = p->p_aggr;
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &ac->ac_if;
	struct ifnet *ifp0 = p->p_ifp0;
	const struct lacp_port_info *pi = &lacpdu->lacp_partner_info;
	uint8_t bits = LACP_STATE_ACTIVITY | LACP_STATE_TIMEOUT |
	    LACP_STATE_SYNC | LACP_STATE_AGGREGATION;
	uint8_t state = p->p_actor_state;
	int sync = 0;

	if (pi->lacp_portid.lacp_portid_number != htons(ifp0->if_index))
		goto ntt;
	if (pi->lacp_portid.lacp_portid_priority !=
	    htons(sc->sc_lacp_port_prio))
		goto ntt;
	if (!ETHER_IS_EQ(pi->lacp_sysid.lacp_sysid_mac, ac->ac_enaddr))
		goto ntt;
	if (pi->lacp_sysid.lacp_sysid_priority !=
	    htons(sc->sc_lacp_prio))
		goto ntt;
	if (pi->lacp_key != htons(ifp->if_index))
		goto ntt;
	if (ISSET(pi->lacp_state, LACP_STATE_SYNC) !=
	    ISSET(state, LACP_STATE_SYNC))
		goto ntt;
	sync = 1;

	if (sc->sc_lacp_timeout == AGGR_LACP_TIMEOUT_FAST)
		SET(state, LACP_STATE_TIMEOUT);
	if (sc->sc_lacp_mode == AGGR_LACP_MODE_ACTIVE)
		SET(state, LACP_STATE_ACTIVITY);

	if (ISSET(pi->lacp_state, bits) != ISSET(state, bits))
		goto ntt;

	return (1);

ntt:
	aggr_ntt(p);

	return (sync);
}

static void
aggr_recordpdu(struct aggr_port *p, const struct lacp_du *lacpdu, int sync)
{
	struct aggr_softc *sc = p->p_aggr;
	const struct lacp_port_info *rpi = &lacpdu->lacp_actor_info;
	struct lacp_port_info *lpi = &p->p_partner;
	int active = ISSET(rpi->lacp_state, LACP_STATE_ACTIVITY) ||
	    (ISSET(p->p_actor_state, LACP_STATE_ACTIVITY) &&
	     ISSET(lacpdu->lacp_partner_info.lacp_state, LACP_STATE_ACTIVITY));

	lpi->lacp_portid.lacp_portid_number =
	    rpi->lacp_portid.lacp_portid_number;
	lpi->lacp_portid.lacp_portid_priority =
	    rpi->lacp_portid.lacp_portid_priority;
	memcpy(lpi->lacp_sysid.lacp_sysid_mac,
	    rpi->lacp_sysid.lacp_sysid_mac,
	    sizeof(lpi->lacp_sysid.lacp_sysid_mac));
	lpi->lacp_sysid.lacp_sysid_priority =
	    rpi->lacp_sysid.lacp_sysid_priority;
	lpi->lacp_key = rpi->lacp_key;
	lpi->lacp_state = rpi->lacp_state & ~LACP_STATE_SYNC;

	CLR(p->p_actor_state, LACP_STATE_DEFAULTED);

	if (active && ISSET(rpi->lacp_state, LACP_STATE_SYNC) && sync) {
		SET(p->p_partner_state, LACP_STATE_SYNC);
		aggr_mux(sc, p, LACP_MUX_E_SYNC);
	} else {
		CLR(p->p_partner_state, LACP_STATE_SYNC);
		aggr_mux(sc, p, LACP_MUX_E_NOT_SYNC);
	}
}

static void
aggr_marker_response(struct aggr_port *p, struct mbuf *m)
{
	struct aggr_softc *sc = p->p_aggr;
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp0 = p->p_ifp0;
	struct marker_pdu *mpdu;
	struct ether_header *eh;

	mpdu = mtod(m, struct marker_pdu *);
	mpdu->marker_info_tlv.lacp_tlv_type = MARKER_T_RESPONSE;

	m = m_prepend(m, sizeof(*eh), M_DONTWAIT);
	if (m == NULL)
		return;

	eh = mtod(m, struct ether_header *);
	memcpy(eh->ether_dhost, lacp_address_slow, sizeof(eh->ether_dhost));
	memcpy(eh->ether_shost, ac->ac_enaddr, sizeof(eh->ether_shost));
	eh->ether_type = htons(ETHERTYPE_SLOW);

	mtx_enter(&p->p_mtx);
	p->p_proto_counts[AGGR_PROTO_TX_MARKER].c_pkts++;
	p->p_proto_counts[AGGR_PROTO_TX_MARKER].c_bytes += m->m_pkthdr.len;
	mtx_leave(&p->p_mtx);

	(void)if_enqueue(ifp0, m);
}

static void
aggr_input_marker(struct aggr_port *p, struct mbuf *m)
{
	struct marker_pdu *mpdu;

	if (m->m_len < sizeof(*mpdu)) {
		m = m_pullup(m, sizeof(*mpdu));
		if (m == NULL)
			return;
	}

	mpdu = mtod(m, struct marker_pdu *);
	switch (mpdu->marker_info_tlv.lacp_tlv_type) {
	case MARKER_T_INFORMATION:
		aggr_marker_response(p, m);
		break;
	default:
		m_freem(m);
		break;
	}
}

static void
aggr_rx(void *arg)
{
	struct aggr_port *p = arg;
	struct mbuf_list ml;
	struct mbuf *m;

	mtx_enter(&p->p_mtx);
	ml = p->p_rxm_ml;
	ml_init(&p->p_rxm_ml);
	mtx_leave(&p->p_mtx);

	while ((m = ml_dequeue(&ml)) != NULL) {
		struct ether_slowproto_hdr *sph;

		/* aggr_input has checked eh already */
		m_adj(m, sizeof(struct ether_header));

		sph = mtod(m, struct ether_slowproto_hdr *);
		switch (sph->sph_subtype) {
		case SLOWPROTOCOLS_SUBTYPE_LACP:
			aggr_input_lacpdu(p, m);
			break;
		case SLOWPROTOCOLS_SUBTYPE_LACP_MARKER:
			aggr_input_marker(p, m);
			break;
		default:
			panic("unexpected slow protocol subtype");
			/* NOTREACHED */
		}
	}
}

static void
aggr_set_selected(struct aggr_port *p, enum aggr_port_selected s,
    enum lacp_mux_event ev)
{
	struct aggr_softc *sc = p->p_aggr;

	if (p->p_selected != s) {
		DPRINTF(sc, "%s %s: Selected %s -> %s\n",
		    sc->sc_if.if_xname, p->p_ifp0->if_xname,
		    aggr_port_selected_names[p->p_selected],
		    aggr_port_selected_names[s]);

		/*
		 * setting p_selected doesn't need the mtx except to
		 * coordinate with a kstat read.
		 */

		mtx_enter(&p->p_mtx);
		p->p_selected = s;
		p->p_nselectch++;
		mtx_leave(&p->p_mtx);
	}
	aggr_mux(sc, p, ev);
}

static void
aggr_unselected(struct aggr_port *p)
{
	aggr_set_selected(p, AGGR_PORT_UNSELECTED, LACP_MUX_E_UNSELECTED);
}

static inline void
aggr_selected(struct aggr_port *p)
{
	aggr_set_selected(p, AGGR_PORT_SELECTED, LACP_MUX_E_SELECTED);
}

#ifdef notyet
static inline void
aggr_standby(struct aggr_port *p)
{
	aggr_set_selected(p, AGGR_PORT_STANDBY, LACP_MUX_E_STANDBY);
}
#endif

static void
aggr_selection_logic(struct aggr_softc *sc, struct aggr_port *p)
{
	const struct lacp_port_info *pi;
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &ac->ac_if;
	const uint8_t *mac;

	if (p->p_rxm_state != LACP_RXM_S_CURRENT) {
		DPRINTF(sc, "%s %s: selection logic: unselected (rxm !%s)\n",
		    ifp->if_xname, p->p_ifp0->if_xname,
		    lacp_rxm_state_names[LACP_RXM_S_CURRENT]);
		goto unselected;
	}

	pi = &p->p_partner;
	if (pi->lacp_key == htons(0)) {
		DPRINTF(sc, "%s %s: selection logic: unselected "
		    "(partner key == 0)\n",
		    ifp->if_xname, p->p_ifp0->if_xname);
		goto unselected;
	}

	/*
	 * aggr(4) does not support individual interfaces
	 */
	if (!ISSET(pi->lacp_state, LACP_STATE_AGGREGATION)) {
		DPRINTF(sc, "%s %s: selection logic: unselected "
		    "(partner state is Individual)\n",
		    ifp->if_xname, p->p_ifp0->if_xname);
		goto unselected;
	}

	/*
	 * Any pair of Aggregation Ports that are members of the same
	 * LAG, but are connected together by the same link, shall not
	 * select the same Aggregator
	 */

	mac = pi->lacp_sysid.lacp_sysid_mac;
	if (ETHER_IS_EQ(mac, ac->ac_enaddr) &&
	    pi->lacp_key == htons(ifp->if_index)) {
		DPRINTF(sc, "%s %s: selection logic: unselected "
		    "(partner sysid !eq)\n",
		    ifp->if_xname, p->p_ifp0->if_xname);
		goto unselected;
	}

	if (!TAILQ_EMPTY(&sc->sc_muxen)) {
		/* an aggregation has already been selected */
		if (!ETHER_IS_EQ(mac, sc->sc_partner_system.lacp_sysid_mac) ||
		    sc->sc_partner_key != pi->lacp_key) {
			DPRINTF(sc, "%s %s: selection logic: unselected "
			    "(partner sysid != selection)\n",
			    ifp->if_xname, p->p_ifp0->if_xname);
			goto unselected;
		}
	}

	aggr_selected(p);
	return;

unselected:
	aggr_unselected(p);
}

static void
aggr_mux(struct aggr_softc *sc, struct aggr_port *p, enum lacp_mux_event ev)
{
	int ntt = 0;

	/*
	 * the mux can move through multiple states based on a
	 * single event, so loop until the event is completely consumed.
	 * debounce NTT = TRUE through the multiple state transitions.
	 */

	while (aggr_mux_ev(sc, p, ev, &ntt) != 0)
		;

	if (ntt)
		aggr_ntt(p);
}

#ifdef notyet
static int
aggr_ready_n(struct aggr_port *p)
{
	return (p->p_mux_state == LACP_MUX_S_WAITING &&
	    !timeout_pending(&p->p_wait_while_timer));
}
#endif

static inline int
aggr_ready(struct aggr_softc *sc)
{
	return (1);
}

static void
aggr_disable_distributing(struct aggr_softc *sc, struct aggr_port *p)
{
	if (!p->p_distributing)
		return;

	sc->sc_ndistributing--;
	TAILQ_REMOVE(&sc->sc_distributing, p, p_entry_distributing);
	p->p_distributing = 0;

	aggr_map(sc);

	DPRINTF(sc, "%s %s: distributing disabled\n",
	    sc->sc_if.if_xname, p->p_ifp0->if_xname);
}

static void
aggr_enable_distributing(struct aggr_softc *sc, struct aggr_port *p)
{
	if (p->p_distributing)
		return;

	/* check the LAG ID? */

	p->p_distributing = 1;
	TAILQ_INSERT_TAIL(&sc->sc_distributing, p, p_entry_distributing);
	sc->sc_ndistributing++;

	aggr_map(sc);

	DPRINTF(sc, "%s %s: distributing enabled\n",
	    sc->sc_if.if_xname, p->p_ifp0->if_xname);
}

static void
aggr_disable_collecting(struct aggr_softc *sc, struct aggr_port *p)
{
	if (!p->p_collecting)
		return;

	p->p_collecting = 0;

	DPRINTF(sc, "%s %s: collecting disabled\n",
	    sc->sc_if.if_xname, p->p_ifp0->if_xname);
}

static void
aggr_enable_collecting(struct aggr_softc *sc, struct aggr_port *p)
{
	if (p->p_collecting)
		return;

	p->p_collecting = 1;

	DPRINTF(sc, "%s %s: collecting enabled\n",
	    sc->sc_if.if_xname, p->p_ifp0->if_xname);
}

static void
aggr_attach_mux(struct aggr_softc *sc, struct aggr_port *p)
{
	const struct lacp_port_info *pi = &p->p_partner;

	if (p->p_muxed)
		return;

	p->p_muxed = 1;
	if (TAILQ_EMPTY(&sc->sc_muxen)) {
		KASSERT(sc->sc_partner_key == htons(0));
		sc->sc_partner_system = pi->lacp_sysid;
		sc->sc_partner_key = pi->lacp_key;
	}

	TAILQ_INSERT_TAIL(&sc->sc_muxen, p, p_entry_muxen);

	DPRINTF(sc, "%s %s: mux attached\n",
	    sc->sc_if.if_xname, p->p_ifp0->if_xname);
}

static void
aggr_detach_mux(struct aggr_softc *sc, struct aggr_port *p)
{
	if (!p->p_muxed)
		return;

	p->p_muxed = 0;

	TAILQ_REMOVE(&sc->sc_muxen, p, p_entry_muxen);
	if (TAILQ_EMPTY(&sc->sc_muxen)) {
		memset(&sc->sc_partner_system.lacp_sysid_mac, 0,
		    sizeof(sc->sc_partner_system.lacp_sysid_mac));
		sc->sc_partner_system.lacp_sysid_priority = htons(0);
		sc->sc_partner_key = htons(0);
	}

	DPRINTF(sc, "%s %s: mux detached\n",
	    sc->sc_if.if_xname, p->p_ifp0->if_xname);
}

static int
aggr_mux_ev(struct aggr_softc *sc, struct aggr_port *p, enum lacp_mux_event ev,
    int *ntt)
{
	enum lacp_mux_state nstate = LACP_MUX_S_DETACHED;

	switch (p->p_mux_state) {
	case LACP_MUX_S_BEGIN:
		KASSERT(ev == LACP_MUX_E_BEGIN);
		nstate = LACP_MUX_S_DETACHED;
		break;
	case LACP_MUX_S_DETACHED:
		switch (ev) {
		case LACP_MUX_E_SELECTED:
		case LACP_MUX_E_STANDBY:
			nstate = LACP_MUX_S_WAITING;
			break;
		default:
			return (0);
		}
		break;
	case LACP_MUX_S_WAITING:
		switch (ev) {
		case LACP_MUX_E_UNSELECTED:
			nstate = LACP_MUX_S_DETACHED;
			break;
		case LACP_MUX_E_SELECTED:
		case LACP_MUX_E_READY:
			if (aggr_ready(sc) &&
			    p->p_selected == AGGR_PORT_SELECTED) {
				nstate = LACP_MUX_S_ATTACHED;
				break;
			}
			/* FALLTHROUGH */
		default:
			return (0);
		}
		break;
	case LACP_MUX_S_ATTACHED:
		switch (ev) {
		case LACP_MUX_E_UNSELECTED:
		case LACP_MUX_E_STANDBY:
			nstate = LACP_MUX_S_DETACHED;
			break;
		case LACP_MUX_E_SELECTED:
		case LACP_MUX_E_SYNC:
			if (p->p_selected == AGGR_PORT_SELECTED &&
			    ISSET(p->p_partner_state, LACP_STATE_SYNC)) {
				nstate = LACP_MUX_S_COLLECTING;
				break;
			}
			/* FALLTHROUGH */
		default:
			return (0);
		}
		break;
	case LACP_MUX_S_COLLECTING:
		switch (ev) {
		case LACP_MUX_E_UNSELECTED:
		case LACP_MUX_E_STANDBY:
		case LACP_MUX_E_NOT_SYNC:
			nstate = LACP_MUX_S_ATTACHED;
			break;
		case LACP_MUX_E_SELECTED:
		case LACP_MUX_E_SYNC:
		case LACP_MUX_E_COLLECTING:
			if (p->p_selected == AGGR_PORT_SELECTED &&
			    ISSET(p->p_partner_state, LACP_STATE_SYNC) &&
			    ISSET(p->p_partner_state, LACP_STATE_COLLECTING)) {
				nstate = LACP_MUX_S_DISTRIBUTING;
				break;
			}
			/* FALLTHROUGH */
		default:
			return (0);
		}
		break;
	case LACP_MUX_S_DISTRIBUTING:
		switch (ev) {
		case LACP_MUX_E_UNSELECTED:
		case LACP_MUX_E_STANDBY:
		case LACP_MUX_E_NOT_SYNC:
		case LACP_MUX_E_NOT_COLLECTING:
			nstate = LACP_MUX_S_COLLECTING;
			break;
		default:
			return (0);
		}
		break;
	}

	DPRINTF(sc, "%s %s mux: %s (%s) -> %s\n",
	    sc->sc_if.if_xname, p->p_ifp0->if_xname,
	    lacp_mux_state_names[p->p_mux_state], lacp_mux_event_names[ev],
	    lacp_mux_state_names[nstate]);

	/* act on the new state */
	switch (nstate) {
	case LACP_MUX_S_BEGIN:
		panic("unexpected mux nstate BEGIN");
		/* NOTREACHED */
	case LACP_MUX_S_DETACHED:
		/*
		 * Detach_Mux_From_Aggregator();
		 * Actor.Sync = FALSE;
		 * Disable_Distributing();
		 * Actor.Distributing = FALSE;
		 * Actor.Collecting = FALSE;
		 * Disable_Collecting();
		 * NTT = TRUE;
		 */
		aggr_detach_mux(sc, p);
		CLR(p->p_actor_state, LACP_STATE_SYNC);
		aggr_disable_distributing(sc, p);
		CLR(p->p_actor_state, LACP_STATE_DISTRIBUTING);
		CLR(p->p_actor_state, LACP_STATE_COLLECTING);
		aggr_disable_collecting(sc, p);
		*ntt = 1;
		break;
	case LACP_MUX_S_WAITING:
		/*
		 * Start wait_while_timer
		 */
		timeout_add_sec(&p->p_wait_while_timer,
		    LACP_AGGREGATION_WAIT_TIME);
		break;
	case LACP_MUX_S_ATTACHED:
		/*
		 * Attach_Mux_To_Aggregator();
		 * Actor.Sync = TRUE;
		 * Actor.Collecting = FALSE;
		 * Disable_Collecting();
		 * NTT = TRUE;
		 */
		aggr_attach_mux(sc, p);
		SET(p->p_actor_state, LACP_STATE_SYNC);
		CLR(p->p_actor_state, LACP_STATE_COLLECTING);
		aggr_disable_collecting(sc, p);
		*ntt = 1;
		break;

	case LACP_MUX_S_COLLECTING:
		/*
		 * Enable_Collecting();
		 * Actor.Collecting = TRUE;
		 * Disable_Distributing();
		 * Actor.Distributing = FALSE;
		 * NTT = TRUE;
		 */
		aggr_enable_collecting(sc, p);
		SET(p->p_actor_state, LACP_STATE_COLLECTING);
		aggr_disable_distributing(sc, p);
		CLR(p->p_actor_state, LACP_STATE_DISTRIBUTING);
		*ntt = 1;
		break;
	case LACP_MUX_S_DISTRIBUTING:
		/*
		 * Actor.Distributing = TRUE;
		 * Enable_Distributing();
		 */
		SET(p->p_actor_state, LACP_STATE_DISTRIBUTING);
		aggr_enable_distributing(sc, p);
		break;
	}

	p->p_mux_state = nstate;

	return (1);
}

static void
aggr_rxm_ev(struct aggr_softc *sc, struct aggr_port *p,
    enum lacp_rxm_event ev, const struct lacp_du *lacpdu)
{
	unsigned int port_disabled = 0;
	enum lacp_rxm_state nstate = LACP_RXM_S_BEGIN;

	KASSERT((ev == LACP_RXM_E_LACPDU) == (lacpdu != NULL));

	/* global transitions */

	switch (ev) {
	case LACP_RXM_E_NOT_PORT_ENABLED:
		port_disabled = !aggr_port_moved(sc, p);
		break;
	case LACP_RXM_E_NOT_PORT_MOVED:
		port_disabled = !aggr_port_enabled(p);
		break;
	default:
		break;
	}

	if (port_disabled)
		nstate = LACP_RXM_S_PORT_DISABLED;
	else switch (p->p_rxm_state) { /* local state transitions */
	case LACP_RXM_S_BEGIN:
		KASSERT(ev == LACP_RXM_E_BEGIN);
		nstate = LACP_RXM_S_INITIALIZE;
		break;
	case LACP_RXM_S_INITIALIZE:
		/* this should only be handled via UCT in nstate handling */
		panic("unexpected rxm state INITIALIZE");

	case LACP_RXM_S_PORT_DISABLED:
		switch (ev) {
		case LACP_RXM_E_PORT_MOVED:
			nstate = LACP_RXM_S_INITIALIZE;
			break;
		case LACP_RXM_E_PORT_ENABLED:
			nstate = aggr_lacp_enabled(sc) ?
			    LACP_RXM_S_EXPIRED : LACP_RXM_S_LACP_DISABLED;
			break;
		case LACP_RXM_E_LACP_ENABLED:
			if (!aggr_port_enabled(p))
				return;
			nstate = LACP_RXM_S_EXPIRED;
			break;
		case LACP_RXM_E_NOT_LACP_ENABLED:
			if (!aggr_port_enabled(p))
				return;
			nstate = LACP_RXM_S_LACP_DISABLED;
			break;
		default:
			return;
		}
		break;
	case LACP_RXM_S_EXPIRED:
		switch (ev) {
		case LACP_RXM_E_LACPDU:
			nstate = LACP_RXM_S_CURRENT;
			break;
		case LACP_RXM_E_TIMER_EXPIRED:
			nstate = LACP_RXM_S_DEFAULTED;
			break;
		default:
			return;
		}
		break;
	case LACP_RXM_S_LACP_DISABLED:
		switch (ev) {
		case LACP_RXM_E_LACP_ENABLED:
			nstate = LACP_RXM_S_PORT_DISABLED;
			break;
		default:
			return;
		}
		break;
	case LACP_RXM_S_DEFAULTED:
		switch (ev) {
		case LACP_RXM_E_LACPDU:
			nstate = LACP_RXM_S_CURRENT;
			break;
		default:
			return;
		}
		break;
	case LACP_RXM_S_CURRENT:
		switch (ev) {
		case LACP_RXM_E_TIMER_EXPIRED:
			nstate = LACP_RXM_S_EXPIRED;
			break;
		case LACP_RXM_E_LACPDU:
			nstate = LACP_RXM_S_CURRENT;
			break;
		default:
			return;
		}
		break;
	}

uct:
	if (p->p_rxm_state != nstate) {
		DPRINTF(sc, "%s %s rxm: %s (%s) -> %s\n",
		    sc->sc_if.if_xname, p->p_ifp0->if_xname,
		    lacp_rxm_state_names[p->p_rxm_state],
		    lacp_rxm_event_names[ev],
		    lacp_rxm_state_names[nstate]);
	}

	/* record the new state */
	p->p_rxm_state = nstate;

	/* act on the new state */
	switch (nstate) {
	case LACP_RXM_S_BEGIN:
		panic("unexpected rxm nstate BEGIN");
		/* NOTREACHED */
	case LACP_RXM_S_INITIALIZE:
		/*
		 * Selected = UNSELECTED;
		 * recordDefault();
		 * Actor_Oper_Port_State.Expired = FALSE;
		 * port_moved = FALSE;
		 */
		aggr_unselected(p);
		aggr_record_default(sc, p);
		CLR(p->p_actor_state, LACP_STATE_EXPIRED);

		ev = LACP_RXM_E_UCT;
		nstate = LACP_RXM_S_PORT_DISABLED;
		goto uct;
		/* NOTREACHED */
	case LACP_RXM_S_PORT_DISABLED:
		/*
		 * Partner_Oper_Port_State.Synchronization = FALSE;
		 */
		CLR(p->p_partner_state, LACP_STATE_SYNC);
		aggr_mux(sc, p, LACP_MUX_E_NOT_SYNC);
		break;
	case LACP_RXM_S_EXPIRED:
		/*
		 * Partner_Oper_Port_State.Synchronization = FALSE;
		 * Partner_Oper_Port_State.LACP_Timeout = Short Timeout;
		 * start current_while_timer(Short Timeout);
		 * Actor_Oper_Port_State.Expired = TRUE;
		 */

		CLR(p->p_partner_state, LACP_STATE_SYNC);
		aggr_mux(sc, p, LACP_MUX_E_NOT_SYNC);
		aggr_set_partner_timeout(p, AGGR_LACP_TIMEOUT_FAST);
		aggr_start_current_while_timer(p, AGGR_LACP_TIMEOUT_FAST);
		SET(p->p_actor_state, LACP_STATE_EXPIRED);

		break;
	case LACP_RXM_S_LACP_DISABLED:
		/*
		 * Selected = UNSELECTED;
		 * recordDefault();
		 * Partner_Oper_Port_State.Aggregation = FALSE;
		 * Actor_Oper_Port_State.Expired = FALSE;
		 */
		aggr_unselected(p);
		aggr_record_default(sc, p);
		CLR(p->p_partner_state, LACP_STATE_AGGREGATION);
		CLR(p->p_actor_state, LACP_STATE_EXPIRED);
		break;
	case LACP_RXM_S_DEFAULTED:
		/*
		 * update_Default_Selected();
		 * recordDefault();
		 * Actor_Oper_Port_State.Expired = FALSE;
		 */
		aggr_update_default_selected(sc, p);
		aggr_record_default(sc, p);
		CLR(p->p_actor_state, LACP_STATE_EXPIRED);
		break;
	case LACP_RXM_S_CURRENT: {
		/*
		 * update_Selected();
		 * update_NTT();
		 * if (Actor_System_LACP_Version >=2 ) recordVersionNumber();
		 * recordPDU();
		 * start current_while_timer(
		 *     Actor_Oper_Port_State.LACP_Timeout);
		 * Actor_Oper_Port_State.Expired = FALSE;
		 */
		int sync;

		aggr_update_selected(sc, p, lacpdu);
		sync = aggr_update_ntt(p, lacpdu);
		/* don't support v2 yet */
		aggr_recordpdu(p, lacpdu, sync);
		aggr_start_current_while_timer(p, sc->sc_lacp_timeout);
		CLR(p->p_actor_state, LACP_STATE_EXPIRED);

		if (p->p_selected == AGGR_PORT_UNSELECTED)
			aggr_selection_logic(sc, p); /* restart */

		}
		break;
	}
}

static int
aggr_up(struct aggr_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	struct aggr_port *p;

	NET_ASSERT_LOCKED();
	KASSERT(!ISSET(ifp->if_flags, IFF_RUNNING));

	SET(ifp->if_flags, IFF_RUNNING); /* LACP_Enabled = TRUE */

	TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
		aggr_rxm(sc, p, LACP_RXM_E_LACP_ENABLED);
		aggr_p_linkch(p);
	}

	/* start the Periodic Transmission machine */
	if (sc->sc_lacp_mode == AGGR_LACP_MODE_ACTIVE) {
		TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
			if (!aggr_port_enabled(p))
				continue;

			timeout_add_sec(&p->p_ptm_tx,
			    aggr_periodic_times[sc->sc_lacp_timeout]);
		}
	}

	return (ENETRESET);
}

static int
aggr_iff(struct aggr_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	unsigned int promisc = ISSET(ifp->if_flags, IFF_PROMISC);

	NET_ASSERT_LOCKED();

	if (promisc != sc->sc_promisc) {
		struct aggr_port *p;

		rw_enter_read(&sc->sc_lock);
		TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
			struct ifnet *ifp0 = p->p_ifp0;
			if (ifpromisc(ifp0, promisc) != 0) {
				log(LOG_WARNING, "%s iff %s: "
				    "unable to turn promisc %s\n",
				    ifp->if_xname, ifp0->if_xname,
				    promisc ? "on" : "off");
			}
		}
		rw_exit_read(&sc->sc_lock);

		sc->sc_promisc = promisc;
	}

	return (0);
}

static int
aggr_down(struct aggr_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	struct aggr_port *p;

	NET_ASSERT_LOCKED();
	CLR(ifp->if_flags, IFF_RUNNING); /* LACP_Enabled = FALSE */

	TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
		aggr_rxm(sc, p, LACP_RXM_E_NOT_LACP_ENABLED);

		/* stop the Periodic Transmission machine */
		timeout_del(&p->p_ptm_tx);

		/* stop the Mux machine */
		aggr_mux(sc, p, LACP_MUX_E_UNSELECTED);

		/* stop the Transmit machine */
		timeout_del(&p->p_txm_ntt);
	}

	KASSERT(TAILQ_EMPTY(&sc->sc_distributing));
	KASSERT(sc->sc_ndistributing == 0);
	KASSERT(SMR_PTR_GET_LOCKED(&sc->sc_map) == NULL);

	return (ENETRESET);
}

static int
aggr_set_lladdr(struct aggr_softc *sc, const struct ifreq *ifr)
{
	struct ifnet *ifp = &sc->sc_if;
	struct aggr_port *p;
	const uint8_t *lladdr = ifr->ifr_addr.sa_data;

	rw_enter_read(&sc->sc_lock);
	TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
		if (aggr_p_setlladdr(p, lladdr) != 0) {
			struct ifnet *ifp0 = p->p_ifp0;
			log(LOG_WARNING, "%s setlladdr %s: "
			    "unable to set lladdr\n",
			    ifp->if_xname, ifp0->if_xname);
		}
	}
	rw_exit_read(&sc->sc_lock);

	return (0);
}

static int
aggr_set_mtu(struct aggr_softc *sc, uint32_t mtu)
{
	struct ifnet *ifp = &sc->sc_if;
	struct aggr_port *p;

	if (mtu < ETHERMIN || mtu > ifp->if_hardmtu)
		return (EINVAL);

	ifp->if_mtu = mtu;

	TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
		if (aggr_p_set_mtu(p, mtu) != 0) {
			struct ifnet *ifp0 = p->p_ifp0;
			log(LOG_WARNING, "%s %s: unable to set mtu %u\n",
			    ifp->if_xname, ifp0->if_xname, mtu);
		}
	}

	return (0);
}

static int
aggr_group(struct aggr_softc *sc, struct aggr_port *p, u_long cmd)
{
	struct ifnet *ifp0 = p->p_ifp0;
	struct ifreq ifr;
	struct sockaddr *sa;

	memset(&ifr, 0, sizeof(ifr));

	/* make it convincing */
	CTASSERT(sizeof(ifr.ifr_name) == sizeof(ifp0->if_xname));
	memcpy(ifr.ifr_name, ifp0->if_xname, sizeof(ifr.ifr_name));

	sa = &ifr.ifr_addr;
	CTASSERT(sizeof(sa->sa_data) >= sizeof(lacp_address_slow));

	sa->sa_family = AF_UNSPEC;
	memcpy(sa->sa_data, lacp_address_slow, sizeof(lacp_address_slow));

	return ((*p->p_ioctl)(ifp0, cmd, (caddr_t)&ifr));
}

static int
aggr_multi(struct aggr_softc *sc, struct aggr_port *p,
    const struct aggr_multiaddr *ma, u_long cmd)
{
	struct ifnet *ifp0 = p->p_ifp0;
	struct {
		char			if_name[IFNAMSIZ];
		struct sockaddr_storage if_addr;
	} ifr;

	memset(&ifr, 0, sizeof(ifr));

	/* make it convincing */
	CTASSERT(sizeof(ifr.if_name) == sizeof(ifp0->if_xname));
	memcpy(ifr.if_name, ifp0->if_xname, sizeof(ifr.if_name));

	ifr.if_addr = ma->m_addr;

	return ((*p->p_ioctl)(ifp0, cmd, (caddr_t)&ifr));
}

static void
aggr_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct aggr_softc *sc = ifp->if_softc;

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_ETHER | IFM_AUTO;

	smr_read_enter(); /* there's no reason to block... */
	if (SMR_PTR_GET(&sc->sc_map) != NULL)
		imr->ifm_status |= IFM_ACTIVE;
	smr_read_leave();
}

static int
aggr_media_change(struct ifnet *ifp)
{
	return (EOPNOTSUPP);
}

static void
aggr_update_capabilities(struct aggr_softc *sc)
{
	struct aggr_port *p;
	uint32_t hardmtu = ETHER_MAX_HARDMTU_LEN;
	uint32_t capabilities = ~0;
	int set = 0;

	/* Do not inherit LRO capabilities. */
	CLR(capabilities, IFCAP_LRO);

	rw_enter_read(&sc->sc_lock);
	TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
		struct ifnet *ifp0 = p->p_ifp0;

		set = 1;
		capabilities &= ifp0->if_capabilities;
		if (ifp0->if_hardmtu < hardmtu)
			hardmtu = ifp0->if_hardmtu;
	}
	rw_exit_read(&sc->sc_lock);

	sc->sc_if.if_hardmtu = hardmtu;
	sc->sc_if.if_capabilities = (set ? capabilities : 0);
}

static void
aggr_ptm_tx(void *arg)
{
	struct aggr_port *p = arg;
	unsigned int timeout;

	aggr_ntt(p);

	timeout = ISSET(p->p_partner_state, LACP_STATE_TIMEOUT) ?
	    AGGR_LACP_TIMEOUT_FAST : AGGR_LACP_TIMEOUT_SLOW;
	timeout_add_sec(&p->p_ptm_tx, aggr_periodic_times[timeout]);
}

static inline void
aggr_lacp_tlv_set(struct lacp_tlv_hdr *tlv, uint8_t type, uint8_t len)
{
	tlv->lacp_tlv_type = type;
	tlv->lacp_tlv_length = sizeof(*tlv) + len;
}

static void
aggr_ntt_transmit(struct aggr_port *p)
{
	struct aggr_softc *sc = p->p_aggr;
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0 = p->p_ifp0;
	struct mbuf *m;
	struct lacp_du *lacpdu;
	struct lacp_port_info *pi;
	struct lacp_collector_info *ci;
	struct ether_header *eh;
	int linkhdr = max_linkhdr + ETHER_ALIGN;
	int len = linkhdr + sizeof(*eh) + sizeof(*lacpdu);

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return;

	if (len > MHLEN) {
		MCLGETL(m, M_DONTWAIT, len);
		if (!ISSET(m->m_flags, M_EXT)) {
			m_freem(m);
			return;
		}
	}

	m->m_pkthdr.pf.prio = sc->sc_if.if_llprio;
	m->m_pkthdr.len = m->m_len = len;
	memset(m->m_data, 0, m->m_len);
	m_adj(m, linkhdr);

	eh = mtod(m, struct ether_header *);

	CTASSERT(sizeof(eh->ether_dhost) == sizeof(lacp_address_slow));
	CTASSERT(sizeof(eh->ether_shost) == sizeof(ac->ac_enaddr));

	memcpy(eh->ether_dhost, lacp_address_slow, sizeof(eh->ether_dhost));
	memcpy(eh->ether_shost, ac->ac_enaddr, sizeof(eh->ether_shost));
	eh->ether_type = htons(ETHERTYPE_SLOW);

	lacpdu = (struct lacp_du *)(eh + 1);
	lacpdu->lacp_du_sph.sph_subtype = SLOWPROTOCOLS_SUBTYPE_LACP;
	lacpdu->lacp_du_sph.sph_version = LACP_VERSION;

	pi = &lacpdu->lacp_actor_info;
	aggr_lacp_tlv_set(&lacpdu->lacp_actor_info_tlv,
	    LACP_T_ACTOR, sizeof(*pi));

	pi->lacp_sysid.lacp_sysid_priority = htons(sc->sc_lacp_prio);
	CTASSERT(sizeof(pi->lacp_sysid.lacp_sysid_mac) ==
	    sizeof(ac->ac_enaddr));
	memcpy(pi->lacp_sysid.lacp_sysid_mac, ac->ac_enaddr,
	    sizeof(pi->lacp_sysid.lacp_sysid_mac));

	pi->lacp_key = htons(ifp->if_index);

	pi->lacp_portid.lacp_portid_priority = htons(sc->sc_lacp_port_prio);
	pi->lacp_portid.lacp_portid_number = htons(ifp0->if_index);

	pi->lacp_state = p->p_actor_state;
	if (sc->sc_lacp_mode)
		SET(pi->lacp_state, LACP_STATE_ACTIVITY);
	if (sc->sc_lacp_timeout)
		SET(pi->lacp_state, LACP_STATE_TIMEOUT);

	pi = &lacpdu->lacp_partner_info;
	aggr_lacp_tlv_set(&lacpdu->lacp_partner_info_tlv,
	    LACP_T_PARTNER, sizeof(*pi));

	*pi = p->p_partner;

	ci = &lacpdu->lacp_collector_info;
	aggr_lacp_tlv_set(&lacpdu->lacp_collector_info_tlv,
	    LACP_T_COLLECTOR, sizeof(*ci));
	ci->lacp_maxdelay = htons(0);

	lacpdu->lacp_terminator.lacp_tlv_type = LACP_T_TERMINATOR;
	lacpdu->lacp_terminator.lacp_tlv_length = 0;

	mtx_enter(&p->p_mtx);
	p->p_proto_counts[AGGR_PROTO_TX_LACP].c_pkts++;
	p->p_proto_counts[AGGR_PROTO_TX_LACP].c_bytes += m->m_pkthdr.len;
	mtx_leave(&p->p_mtx);

	(void)if_enqueue(ifp0, m);
}

static void
aggr_ntt(struct aggr_port *p)
{
	if (!timeout_pending(&p->p_txm_ntt))
		timeout_add(&p->p_txm_ntt, 0);
}

static void
aggr_transmit_machine(void *arg)
{
	struct aggr_port *p = arg;
	struct aggr_softc *sc = p->p_aggr;
	unsigned int slot;
	int *log;
	int period = hz * LACP_FAST_PERIODIC_TIME;
	int diff;

	if (!aggr_lacp_enabled(sc) || !aggr_port_enabled(p))
		return;

	slot = p->p_txm_slot;
	log = &p->p_txm_log[slot % nitems(p->p_txm_log)];

	diff = ticks - *log;
	if (diff < period) {
		timeout_add(&p->p_txm_ntt, period - diff);
		return;
	}

	*log = ticks;
	p->p_txm_slot = ++slot;

#if 0
	DPRINTF(sc, "%s %s ntt\n", sc->sc_if.if_xname, p->p_ifp0->if_xname);
#endif

	aggr_ntt_transmit(p);
}

static void
aggr_set_lacp_mode(struct aggr_softc *sc, int mode)
{
	sc->sc_lacp_mode = mode;

	if (mode == AGGR_LACP_MODE_PASSIVE) {
		struct aggr_port *p;

		TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
			if (!ISSET(p->p_partner_state, LACP_STATE_ACTIVITY))
				timeout_del(&p->p_ptm_tx);
		}
	}
}

static void
aggr_set_partner_timeout(struct aggr_port *p, int timeout)
{
	uint8_t ostate = ISSET(p->p_partner_state, LACP_STATE_TIMEOUT);
	uint8_t nstate = (timeout == AGGR_LACP_TIMEOUT_FAST) ?
	    LACP_STATE_TIMEOUT : 0;

	if (ostate == nstate)
		return;

	if (timeout == AGGR_LACP_TIMEOUT_FAST) {
		SET(p->p_partner_state, LACP_STATE_TIMEOUT);
		timeout_add_sec(&p->p_ptm_tx,
		    aggr_periodic_times[AGGR_LACP_TIMEOUT_FAST]);
	} else
		CLR(p->p_partner_state, LACP_STATE_TIMEOUT);
}

static void
aggr_set_lacp_timeout(struct aggr_softc *sc, int timeout)
{
	struct aggr_port *p;

	sc->sc_lacp_timeout = timeout;

	TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
		if (!ISSET(p->p_actor_state, LACP_STATE_DEFAULTED))
			continue;

		aggr_set_partner_timeout(p, timeout);
	}
}

static int
aggr_multi_eq(const struct aggr_multiaddr *ma,
    const uint8_t *addrlo, const uint8_t *addrhi)
{
	return (ETHER_IS_EQ(ma->m_addrlo, addrlo) &&
	    ETHER_IS_EQ(ma->m_addrhi, addrhi));
}

static int
aggr_multi_add(struct aggr_softc *sc, struct ifreq *ifr)
{
	struct ifnet *ifp = &sc->sc_if;
	struct aggr_port *p;
	struct aggr_multiaddr *ma;
	uint8_t addrlo[ETHER_ADDR_LEN];
	uint8_t addrhi[ETHER_ADDR_LEN];
	int error;

	error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
	if (error != 0)
		return (error);

	TAILQ_FOREACH(ma, &sc->sc_multiaddrs, m_entry) {
		if (aggr_multi_eq(ma, addrlo, addrhi)) {
			ma->m_refs++;
			return (0);
		}
	}

	ma = malloc(sizeof(*ma), M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (ma == NULL)
		return (ENOMEM);

	ma->m_refs = 1;
	memcpy(&ma->m_addr, &ifr->ifr_addr, ifr->ifr_addr.sa_len);
	memcpy(ma->m_addrlo, addrlo, sizeof(ma->m_addrlo));
	memcpy(ma->m_addrhi, addrhi, sizeof(ma->m_addrhi));
	TAILQ_INSERT_TAIL(&sc->sc_multiaddrs, ma, m_entry);

	TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
		struct ifnet *ifp0 = p->p_ifp0;

		if (aggr_multi(sc, p, ma, SIOCADDMULTI) != 0) {
			log(LOG_WARNING, "%s %s: "
			    "unable to add multicast address\n",
			    ifp->if_xname, ifp0->if_xname);
		}
	}

	return (0);
}

int
aggr_multi_del(struct aggr_softc *sc, struct ifreq *ifr)
{
	struct ifnet *ifp = &sc->sc_if;
	struct aggr_port *p;
	struct aggr_multiaddr *ma;
	uint8_t addrlo[ETHER_ADDR_LEN];
	uint8_t addrhi[ETHER_ADDR_LEN];
	int error;

	error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
	if (error != 0)
		return (error);

	TAILQ_FOREACH(ma, &sc->sc_multiaddrs, m_entry) {
		if (aggr_multi_eq(ma, addrlo, addrhi))
			break;
	}

	if (ma == NULL)
		return (EINVAL);

	if (--ma->m_refs > 0)
		return (0);

	TAILQ_REMOVE(&sc->sc_multiaddrs, ma, m_entry);

	TAILQ_FOREACH(p, &sc->sc_ports, p_entry) {
		struct ifnet *ifp0 = p->p_ifp0;

		if (aggr_multi(sc, p, ma, SIOCDELMULTI) != 0) {
			log(LOG_WARNING, "%s %s: "
			    "unable to delete multicast address\n",
			    ifp->if_xname, ifp0->if_xname);
		}
	}

	free(ma, M_DEVBUF, sizeof(*ma));

	return (0);
}

#if NKSTAT > 0
static const char *aggr_proto_names[AGGR_PROTO_COUNT] = {
	[AGGR_PROTO_TX_LACP] = "tx-lacp",
	[AGGR_PROTO_TX_MARKER] = "tx-marker",
	[AGGR_PROTO_RX_LACP] = "rx-lacp",
	[AGGR_PROTO_RX_MARKER] = "rx-marker",
};

struct aggr_port_kstat {
	struct kstat_kv		interface;

	struct {
		struct kstat_kv		pkts;
		struct kstat_kv		bytes;
	}			protos[AGGR_PROTO_COUNT];

	struct kstat_kv		rx_drops;

	struct kstat_kv		selected;
	struct kstat_kv		nselectch;
};

static int
aggr_port_kstat_read(struct kstat *ks)
{
	struct aggr_port *p = ks->ks_softc;
	struct aggr_port_kstat *pk = ks->ks_data;
	unsigned int proto;

	mtx_enter(&p->p_mtx);
	for (proto = 0; proto < AGGR_PROTO_COUNT; proto++) {
		kstat_kv_u64(&pk->protos[proto].pkts) =
		    p->p_proto_counts[proto].c_pkts;
		kstat_kv_u64(&pk->protos[proto].bytes) =
		    p->p_proto_counts[proto].c_bytes;
	}
	kstat_kv_u64(&pk->rx_drops) = p->p_rx_drops;

	kstat_kv_bool(&pk->selected) = p->p_selected == AGGR_PORT_SELECTED;
	kstat_kv_u32(&pk->nselectch) = p->p_nselectch;
	mtx_leave(&p->p_mtx);

	nanouptime(&ks->ks_updated);

	return (0);
}

static void
aggr_port_kstat_attach(struct aggr_port *p)
{
	struct aggr_softc *sc = p->p_aggr;
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0 = p->p_ifp0;
	struct kstat *ks;
	struct aggr_port_kstat *pk;
	unsigned int proto;

	pk = malloc(sizeof(*pk), M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (pk == NULL) {
		log(LOG_WARNING, "%s %s: unable to allocate aggr-port kstat\n",
		    ifp->if_xname, ifp0->if_xname);
		return;
	}

	ks = kstat_create(ifp->if_xname, 0, "aggr-port", ifp0->if_index,
	    KSTAT_T_KV, 0);
	if (ks == NULL) {
		log(LOG_WARNING, "%s %s: unable to create aggr-port kstat\n",
		    ifp->if_xname, ifp0->if_xname);
		free(pk, M_DEVBUF, sizeof(*pk));
		return;
	}

	kstat_kv_init(&pk->interface, "interface", KSTAT_KV_T_ISTR);
	strlcpy(kstat_kv_istr(&pk->interface), ifp0->if_xname,
	    sizeof(kstat_kv_istr(&pk->interface)));

	for (proto = 0; proto < AGGR_PROTO_COUNT; proto++) {
		char kvname[KSTAT_KV_NAMELEN];

		snprintf(kvname, sizeof(kvname),
		    "%s-pkts", aggr_proto_names[proto]);
		kstat_kv_unit_init(&pk->protos[proto].pkts,
		    kvname, KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS);

		snprintf(kvname, sizeof(kvname),
		    "%s-bytes", aggr_proto_names[proto]);
		kstat_kv_unit_init(&pk->protos[proto].bytes,
		    kvname, KSTAT_KV_T_COUNTER64, KSTAT_KV_U_BYTES);
	}

	kstat_kv_unit_init(&pk->rx_drops, "rx-drops",
	    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS);

	kstat_kv_init(&pk->selected, "selected", KSTAT_KV_T_BOOL);
	kstat_kv_init(&pk->nselectch, "select-changes", KSTAT_KV_T_COUNTER32);

	ks->ks_softc = p;
	ks->ks_data = pk;
	ks->ks_datalen = sizeof(*pk);
	ks->ks_read = aggr_port_kstat_read;

	kstat_install(ks);

	p->p_kstat = ks;
}

static void
aggr_port_kstat_detach(struct aggr_port *p)
{
	struct kstat *ks = p->p_kstat;
	struct aggr_port_kstat *pk;

	if (ks == NULL)
		return;

	p->p_kstat = NULL;

	kstat_remove(ks);
	pk = ks->ks_data;
	kstat_destroy(ks);

	free(pk, M_DEVBUF, sizeof(*pk));
}
#endif

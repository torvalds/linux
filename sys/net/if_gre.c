/*	$OpenBSD: if_gre.c,v 1.190 2025/07/07 02:28:50 jsg Exp $ */
/*	$NetBSD: if_gre.c,v 1.9 1999/10/25 19:18:11 drochner Exp $ */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Heiko W.Rupp <hwr@pilhuhn.de>
 *
 * IPv6-over-GRE contributed by Gert Doering <gert@greenie.muc.de>
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Encapsulate L3 protocols into IP, per RFC 1701 and 1702.
 * See gre(4) for more details.
 * Also supported: IP in IP encapsulation (proto 55) per RFC 2004.
 */

#include "bpfilter.h"
#include "pf.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/timeout.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/pool.h>

#include <crypto/siphash.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_ecn.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_var.h>
#endif

#ifdef PIPEX
#include <net/pipex.h>
#endif

#ifdef MPLS
#include <netmpls/mpls.h>
#endif /* MPLS */

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NPF > 0
#include <net/pfvar.h>
#endif

#include <net/if_gre.h>

#include <netinet/ip_gre.h>
#include <sys/sysctl.h>

/* for nvgre bridge shizz */
#include <net/if_bridge.h>
#include <net/if_etherbridge.h>

/*
 * Locks used to protect data:
 *	a	atomic
 */

/*
 * packet formats
 */
struct gre_header {
	uint16_t		gre_flags;
#define GRE_CP				0x8000  /* Checksum Present */
#define GRE_KP				0x2000  /* Key Present */
#define GRE_SP				0x1000  /* Sequence Present */

#define GRE_VERS_MASK			0x0007
#define GRE_VERS_0			0x0000
#define GRE_VERS_1			0x0001

	uint16_t		gre_proto;
} __packed __aligned(4);

struct gre_h_cksum {
	uint16_t		gre_cksum;
	uint16_t		gre_reserved1;
} __packed __aligned(4);

struct gre_h_key {
	uint32_t		gre_key;
} __packed __aligned(4);

#define GRE_EOIP		0x6400

struct gre_h_key_eoip {
	uint16_t		eoip_len;	/* network order */
	uint16_t		eoip_tunnel_id;	/* little endian */
} __packed __aligned(4);

#define NVGRE_VSID_RES_MIN	0x000000 /* reserved for future use */
#define NVGRE_VSID_RES_MAX	0x000fff
#define NVGRE_VSID_NVE2NVE	0xffffff /* vendor specific NVE-to-NVE comms */

struct gre_h_seq {
	uint32_t		gre_seq;
} __packed __aligned(4);

struct gre_h_wccp {
	uint8_t			wccp_flags;
	uint8_t			service_id;
	uint8_t			alt_bucket;
	uint8_t			pri_bucket;
} __packed __aligned(4);

#define GRE_WCCP		0x883e
#define GRE_ERSPAN		0x88be /* also ERSPAN Type II */
#define GRE_ERSPAN_III		0x22eb

#define GRE_HDRLEN (sizeof(struct ip) + sizeof(struct gre_header))

/*
 * GRE tunnel metadata
 */

#define GRE_KA_NONE		0
#define GRE_KA_DOWN		1
#define GRE_KA_HOLD		2
#define GRE_KA_UP		3

union gre_addr {
	struct in_addr		in4;
	struct in6_addr		in6;
};

static inline int
		gre_ip_cmp(int, const union gre_addr *,
		    const union gre_addr *);

#define GRE_KEY_MIN		0x00000000U
#define GRE_KEY_MAX		0xffffffffU
#define GRE_KEY_SHIFT		0

#define GRE_KEY_ENTROPY_MIN	0x00000000U
#define GRE_KEY_ENTROPY_MAX	0x00ffffffU
#define GRE_KEY_ENTROPY_SHIFT	8

struct gre_tunnel {
	uint32_t		t_key_mask;
#define GRE_KEY_NONE			htonl(0x00000000U)
#define GRE_KEY_ENTROPY			htonl(0xffffff00U)
#define GRE_KEY_MASK			htonl(0xffffffffU)
	uint32_t		t_key;

	u_int			t_rtableid;
	union gre_addr		t_src;
#define t_src4	t_src.in4
#define t_src6	t_src.in6
	union gre_addr		t_dst;
#define t_dst4	t_dst.in4
#define t_dst6	t_dst.in6
	int			t_ttl;
	int			t_txhprio;
	int			t_rxhprio;
	int			t_ecn;
	uint16_t		t_df;
	sa_family_t		t_af;
};

static int
		gre_cmp_src(const struct gre_tunnel *,
		    const struct gre_tunnel *);
static int
		gre_cmp(const struct gre_tunnel *, const struct gre_tunnel *);

static int	gre_set_tunnel(struct gre_tunnel *, struct if_laddrreq *, int);
static int	gre_get_tunnel(struct gre_tunnel *, struct if_laddrreq *);
static int	gre_del_tunnel(struct gre_tunnel *);

static int	gre_set_vnetid(struct gre_tunnel *, struct ifreq *);
static int	gre_get_vnetid(struct gre_tunnel *, struct ifreq *);
static int	gre_del_vnetid(struct gre_tunnel *);

static int	gre_set_vnetflowid(struct gre_tunnel *, struct ifreq *);
static int	gre_get_vnetflowid(struct gre_tunnel *, struct ifreq *);

static struct mbuf *
		gre_encap_dst(const struct gre_tunnel *, const union gre_addr *,
		    struct mbuf *, uint16_t, uint8_t, uint8_t);
#define gre_encap(_t, _m, _p, _ttl, _tos) \
		gre_encap_dst((_t), &(_t)->t_dst, (_m), (_p), (_ttl), (_tos))

static struct mbuf *
		gre_encap_dst_ip(const struct gre_tunnel *,
		    const union gre_addr *, struct mbuf *, uint8_t, uint8_t);
#define gre_encap_ip(_t, _m, _ttl, _tos) \
		gre_encap_dst_ip((_t), &(_t)->t_dst, (_m), (_ttl), (_tos))

static int
		gre_ip_output(const struct gre_tunnel *, struct mbuf *);

static int	gre_tunnel_ioctl(struct ifnet *, struct gre_tunnel *,
		    u_long, void *);

static uint8_t	gre_l2_tos(const struct gre_tunnel *, const struct mbuf *);
static uint8_t	gre_l3_tos(const struct gre_tunnel *,
		    const struct mbuf *, uint8_t);

/*
 * layer 3 GRE tunnels
 */

struct gre_softc {
	struct gre_tunnel	sc_tunnel; /* must be first */
	TAILQ_ENTRY(gre_softc)	sc_entry;

	struct ifnet		sc_if;

	struct timeout		sc_ka_send;
	struct timeout		sc_ka_hold;

	unsigned int		sc_ka_state;
	unsigned int		sc_ka_timeo;
	unsigned int		sc_ka_count;

	unsigned int		sc_ka_holdmax;
	unsigned int		sc_ka_holdcnt;

	SIPHASH_KEY		sc_ka_key;
	uint32_t		sc_ka_bias;
	int			sc_ka_recvtm;
};

TAILQ_HEAD(gre_list, gre_softc);

struct gre_keepalive {
	uint32_t		gk_uptime;
	uint32_t		gk_random;
	uint8_t			gk_digest[SIPHASH_DIGEST_LENGTH];
} __packed __aligned(4);

static int	gre_clone_create(struct if_clone *, int);
static int	gre_clone_destroy(struct ifnet *);

struct if_clone gre_cloner =
    IF_CLONE_INITIALIZER("gre", gre_clone_create, gre_clone_destroy);

/* protected by NET_LOCK */
struct gre_list gre_list = TAILQ_HEAD_INITIALIZER(gre_list);

static int	gre_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
static void	gre_start(struct ifnet *);
static int	gre_ioctl(struct ifnet *, u_long, caddr_t);

static int	gre_up(struct gre_softc *);
static int	gre_down(struct gre_softc *);
static void	gre_link_state(struct ifnet *, unsigned int);

static int	gre_input_key(struct mbuf **, int *, int, int, uint8_t,
		    struct gre_tunnel *, struct netstack *);

static struct mbuf *
		gre_ipv4_patch(const struct gre_tunnel *, struct mbuf *,
		    uint8_t *, uint8_t);
#ifdef INET6
static struct mbuf *
		gre_ipv6_patch(const struct gre_tunnel *, struct mbuf *,
		    uint8_t *, uint8_t);
#endif
#ifdef MPLS
static struct mbuf *
		gre_mpls_patch(const struct gre_tunnel *, struct mbuf *,
		    uint8_t *, uint8_t);
#endif
static void	gre_keepalive_send(void *);
static void	gre_keepalive_recv(struct ifnet *ifp, struct mbuf *);
static void	gre_keepalive_hold(void *);

static struct mbuf *
		gre_l3_encap_dst(const struct gre_tunnel *, const void *,
		    struct mbuf *m, sa_family_t);

#define gre_l3_encap(_t, _m, _af) \
		gre_l3_encap_dst((_t), &(_t)->t_dst, (_m), (_af))

struct mgre_softc {
	struct gre_tunnel	sc_tunnel; /* must be first */
	RBT_ENTRY(mgre_softc)	sc_entry;

	struct ifnet		sc_if;
};

RBT_HEAD(mgre_tree, mgre_softc);

static inline int
		mgre_cmp(const struct mgre_softc *, const struct mgre_softc *);

RBT_PROTOTYPE(mgre_tree, mgre_softc, sc_entry, mgre_cmp);

static int	mgre_clone_create(struct if_clone *, int);
static int	mgre_clone_destroy(struct ifnet *);

struct if_clone mgre_cloner =
    IF_CLONE_INITIALIZER("mgre", mgre_clone_create, mgre_clone_destroy);

static void	mgre_rtrequest(struct ifnet *, int, struct rtentry *);
static int	mgre_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
static void	mgre_start(struct ifnet *);
static int	mgre_ioctl(struct ifnet *, u_long, caddr_t);

static int	mgre_set_tunnel(struct mgre_softc *, struct if_laddrreq *);
static int	mgre_get_tunnel(struct mgre_softc *, struct if_laddrreq *);
static int	mgre_up(struct mgre_softc *);
static int	mgre_down(struct mgre_softc *);

/* protected by NET_LOCK */
struct mgre_tree mgre_tree = RBT_INITIALIZER();

/*
 * Ethernet GRE tunnels
 */

static struct mbuf *
		gre_ether_align(struct mbuf *, int);

struct egre_softc {
	struct gre_tunnel	sc_tunnel; /* must be first */
	RBT_ENTRY(egre_softc)	sc_entry;

	struct arpcom		sc_ac;
	struct ifmedia		sc_media;
};

RBT_HEAD(egre_tree, egre_softc);

static inline int
		egre_cmp(const struct egre_softc *, const struct egre_softc *);

RBT_PROTOTYPE(egre_tree, egre_softc, sc_entry, egre_cmp);

static int	egre_clone_create(struct if_clone *, int);
static int	egre_clone_destroy(struct ifnet *);

static void	egre_start(struct ifnet *);
static int	egre_ioctl(struct ifnet *, u_long, caddr_t);
static int	egre_media_change(struct ifnet *);
static void	egre_media_status(struct ifnet *, struct ifmediareq *);

static int	egre_up(struct egre_softc *);
static int	egre_down(struct egre_softc *);

static int	egre_input(const struct gre_tunnel *, struct mbuf *, int,
		    uint8_t, struct netstack *);
struct if_clone egre_cloner =
    IF_CLONE_INITIALIZER("egre", egre_clone_create, egre_clone_destroy);

/* protected by NET_LOCK */
struct egre_tree egre_tree = RBT_INITIALIZER();

/*
 * Network Virtualisation Using Generic Routing Encapsulation (NVGRE)
 */

struct nvgre_softc {
	struct gre_tunnel	 sc_tunnel; /* must be first */
	unsigned int		 sc_ifp0;
	RBT_ENTRY(nvgre_softc)	 sc_uentry;
	RBT_ENTRY(nvgre_softc)	 sc_mentry;

	struct arpcom		 sc_ac;
	struct ifmedia		 sc_media;

	struct mbuf_queue	 sc_send_list;
	struct task		 sc_send_task;

	void			*sc_inm;
	struct task		 sc_ltask;
	struct task		 sc_dtask;

	struct etherbridge	 sc_eb;
};

RBT_HEAD(nvgre_ucast_tree, nvgre_softc);
RBT_HEAD(nvgre_mcast_tree, nvgre_softc);

static inline int
		nvgre_cmp_ucast(const struct nvgre_softc *,
		    const struct nvgre_softc *);
static int
		nvgre_cmp_mcast(const struct gre_tunnel *,
		    const union gre_addr *, unsigned int,
		    const struct gre_tunnel *, const union gre_addr *,
		    unsigned int);
static inline int
		nvgre_cmp_mcast_sc(const struct nvgre_softc *,
		    const struct nvgre_softc *);

RBT_PROTOTYPE(nvgre_ucast_tree, nvgre_softc, sc_uentry, nvgre_cmp_ucast);
RBT_PROTOTYPE(nvgre_mcast_tree, nvgre_softc, sc_mentry, nvgre_cmp_mcast_sc);

static int	nvgre_clone_create(struct if_clone *, int);
static int	nvgre_clone_destroy(struct ifnet *);

static void	nvgre_start(struct ifnet *);
static int	nvgre_ioctl(struct ifnet *, u_long, caddr_t);

static int	nvgre_up(struct nvgre_softc *);
static int	nvgre_down(struct nvgre_softc *);
static int	nvgre_set_parent(struct nvgre_softc *, const char *);
static void	nvgre_link_change(void *);
static void	nvgre_detach(void *);

static int	nvgre_input(const struct gre_tunnel *, struct mbuf *, int,
		    uint8_t, struct netstack *);
static void	nvgre_send(void *);

static int	nvgre_add_addr(struct nvgre_softc *, const struct ifbareq *);
static int	nvgre_del_addr(struct nvgre_softc *, const struct ifbareq *);

static int	 nvgre_eb_port_eq(void *, void *, void *);
static void	*nvgre_eb_port_take(void *, void *);
static void	 nvgre_eb_port_rele(void *, void *);
static size_t	 nvgre_eb_port_ifname(void *, char *, size_t, void *);
static void	 nvgre_eb_port_sa(void *, struct sockaddr_storage *, void *);

static const struct etherbridge_ops nvgre_etherbridge_ops = {
	nvgre_eb_port_eq,
	nvgre_eb_port_take,
	nvgre_eb_port_rele,
	nvgre_eb_port_ifname,
	nvgre_eb_port_sa,
};

struct if_clone nvgre_cloner =
    IF_CLONE_INITIALIZER("nvgre", nvgre_clone_create, nvgre_clone_destroy);

struct pool nvgre_endpoint_pool;

/* protected by NET_LOCK */
struct nvgre_ucast_tree nvgre_ucast_tree = RBT_INITIALIZER();
struct nvgre_mcast_tree nvgre_mcast_tree = RBT_INITIALIZER();

/*
 * MikroTik Ethernet over IP protocol (eoip)
 */

struct eoip_softc {
	struct gre_tunnel	sc_tunnel; /* must be first */
	uint16_t		sc_tunnel_id;
	RBT_ENTRY(eoip_softc)	sc_entry;

	struct arpcom		sc_ac;
	struct ifmedia		sc_media;

	struct timeout		sc_ka_send;
	struct timeout		sc_ka_hold;

	unsigned int		sc_ka_state;
	unsigned int		sc_ka_timeo;
	unsigned int		sc_ka_count;

	unsigned int		sc_ka_holdmax;
	unsigned int		sc_ka_holdcnt;
};

RBT_HEAD(eoip_tree, eoip_softc);

static inline int
		eoip_cmp(const struct eoip_softc *, const struct eoip_softc *);

RBT_PROTOTYPE(eoip_tree, eoip_softc, sc_entry, eoip_cmp);

static int	eoip_clone_create(struct if_clone *, int);
static int	eoip_clone_destroy(struct ifnet *);

static void	eoip_start(struct ifnet *);
static int	eoip_ioctl(struct ifnet *, u_long, caddr_t);

static void	eoip_keepalive_send(void *);
static void	eoip_keepalive_recv(struct eoip_softc *);
static void	eoip_keepalive_hold(void *);

static int	eoip_up(struct eoip_softc *);
static int	eoip_down(struct eoip_softc *);

static struct mbuf *
		eoip_encap(struct eoip_softc *, struct mbuf *, uint8_t);

static struct mbuf *
		eoip_input(struct gre_tunnel *, struct mbuf *,
		    const struct gre_header *, uint8_t, int, struct netstack *);
struct if_clone eoip_cloner =
    IF_CLONE_INITIALIZER("eoip", eoip_clone_create, eoip_clone_destroy);

/* protected by NET_LOCK */
struct eoip_tree eoip_tree = RBT_INITIALIZER();

/*
 * ERSPAN support
 */

struct gre_h_erspan {
	uint32_t		hdr;
#define ERSPAN_II_VER_SHIFT		28
#define ERSPAN_II_VER_MASK		0xf
#define ERSPAN_II_VER			0x1
#define ERSPAN_II_VLAN_SHIFT		16
#define ERSPAN_II_VLAN_MASK		0xfff
#define ERSPAN_II_COS_SHIFT		13
#define ERSPAN_II_COS_MASK		0x7
#define ERSPAN_II_EN_SHIFT		11 /* Encapsulation type */
#define ERSPAN_II_EN_MASK		0x3
#define ERSPAN_II_EN_NONE		0x0
#define ERSPAN_II_EN_ISL		0x1
#define ERSPAN_II_EN_VLAN		0x2
#define ERSPAN_II_EN_PRESERVED		0x3
#define ERSPAN_II_T			(0x1 << 10)
#define ERSPAN_II_SESSION_ID_SHIFT	0
#define ERSPAN_II_SESSION_ID_MASK	0x3ff /* 10 bits */
	uint32_t		index;
#define ERSPAN_II_INDEX_SHIFT		0
#define ERSPAN_II_INDEX_MASK		0xfffff /* 20 bits */
};

struct erspan_softc {
	struct gre_tunnel	sc_tunnel; /* must be first */
	int			sc_session_id;
	RBT_ENTRY(erspan_softc)	sc_entry;

	struct arpcom		sc_ac;
	uint32_t		sc_seq;
	caddr_t			sc_bpf;
	caddr_t			sc_bpf_raw;
};

RBT_HEAD(erspan_tree, erspan_softc);

static inline int
		erspan_cmp(const struct erspan_softc *,
		    const struct erspan_softc *);

RBT_PROTOTYPE(erspan_tree, erspan_softc, sc_entry, erspan_cmp);

static int	erspan_clone_create(struct if_clone *, int);
static int	erspan_clone_destroy(struct ifnet *);

static void	erspan_start(struct ifnet *);
static int	erspan_ioctl(struct ifnet *, u_long, caddr_t);

static int	erspan_up(struct erspan_softc *);
static int	erspan_down(struct erspan_softc *);

static struct mbuf *
		erspan_encap(struct erspan_softc *, struct mbuf *, uint8_t,
		    uint32_t);

static struct mbuf *
		erspan_input(struct gre_tunnel *, struct mbuf *, int,
		    const struct gre_header *, uint8_t, struct netstack *);

struct if_clone erspan_cloner =
    IF_CLONE_INITIALIZER("erspan", erspan_clone_create, erspan_clone_destroy);

/* protected by NET_LOCK */
struct erspan_tree erspan_tree = RBT_INITIALIZER();

/*
 * It is not easy to calculate the right value for a GRE MTU.
 * We leave this task to the admin and use the same default that
 * other vendors use.
 */
#define GREMTU 1476

/*
 * We can control the acceptance of GRE and MobileIP packets by
 * altering the sysctl net.inet.gre.allow values
 * respectively. Zero means drop them, all else is acceptance.  We can also
 * control acceptance of WCCPv1-style GRE packets through the
 * net.inet.gre.wccp value, but be aware it depends upon normal GRE being
 * allowed as well.
 *
 */
int gre_allow = 0;		/* [a] */
int gre_wccp = 0;		/* [a] */

void
greattach(int n)
{
	if_clone_attach(&gre_cloner);
	if_clone_attach(&mgre_cloner);
	if_clone_attach(&egre_cloner);
	if_clone_attach(&nvgre_cloner);
	if_clone_attach(&eoip_cloner);
	if_clone_attach(&erspan_cloner);
}

static int
gre_clone_create(struct if_clone *ifc, int unit)
{
	struct gre_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	snprintf(sc->sc_if.if_xname, sizeof sc->sc_if.if_xname, "%s%d",
	    ifc->ifc_name, unit);

	ifp = &sc->sc_if;
	ifp->if_softc = sc;
	ifp->if_type = IFT_TUNNEL;
	ifp->if_hdrlen = GRE_HDRLEN;
	ifp->if_mtu = GREMTU;
	ifp->if_flags = IFF_POINTOPOINT|IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_bpf_mtap = p2p_bpf_mtap;
	ifp->if_input = p2p_input;
	ifp->if_output = gre_output;
	ifp->if_start = gre_start;
	ifp->if_ioctl = gre_ioctl;
	ifp->if_rtrequest = p2p_rtrequest;

	sc->sc_tunnel.t_ttl = atomic_load_int(&ip_defttl);
	sc->sc_tunnel.t_txhprio = IF_HDRPRIO_PAYLOAD;
	sc->sc_tunnel.t_rxhprio = IF_HDRPRIO_PACKET;
	sc->sc_tunnel.t_df = htons(0);
	sc->sc_tunnel.t_ecn = ECN_ALLOWED;

	timeout_set(&sc->sc_ka_send, gre_keepalive_send, sc);
	timeout_set_proc(&sc->sc_ka_hold, gre_keepalive_hold, sc);
	sc->sc_ka_state = GRE_KA_NONE;

	if_counters_alloc(ifp);
	if_attach(ifp);
	if_alloc_sadl(ifp);

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(uint32_t));
#endif

	ifp->if_llprio = IFQ_TOS2PRIO(IPTOS_PREC_INTERNETCONTROL);

	NET_LOCK();
	TAILQ_INSERT_TAIL(&gre_list, sc, sc_entry);
	NET_UNLOCK();

	return (0);
}

static int
gre_clone_destroy(struct ifnet *ifp)
{
	struct gre_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		gre_down(sc);

	TAILQ_REMOVE(&gre_list, sc, sc_entry);
	NET_UNLOCK();

	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
mgre_clone_create(struct if_clone *ifc, int unit)
{
	struct mgre_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = &sc->sc_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname),
	    "%s%d", ifc->ifc_name, unit);

	ifp->if_softc = sc;
	ifp->if_type = IFT_L3IPVLAN;
	ifp->if_hdrlen = GRE_HDRLEN;
	ifp->if_mtu = GREMTU;
	ifp->if_flags = IFF_MULTICAST|IFF_SIMPLEX;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_bpf_mtap = p2p_bpf_mtap;
	ifp->if_input = p2p_input;
	ifp->if_rtrequest = mgre_rtrequest;
	ifp->if_output = mgre_output;
	ifp->if_start = mgre_start;
	ifp->if_ioctl = mgre_ioctl;

	sc->sc_tunnel.t_ttl = atomic_load_int(&ip_defttl);
	sc->sc_tunnel.t_txhprio = IF_HDRPRIO_PAYLOAD;
	sc->sc_tunnel.t_rxhprio = IF_HDRPRIO_PACKET;
	sc->sc_tunnel.t_df = htons(0);
	sc->sc_tunnel.t_ecn = ECN_ALLOWED;

	if_counters_alloc(ifp);
	if_attach(ifp);
	if_alloc_sadl(ifp);

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(uint32_t));
#endif

	return (0);
}

static int
mgre_clone_destroy(struct ifnet *ifp)
{
	struct mgre_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		mgre_down(sc);
	NET_UNLOCK();

	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
egre_clone_create(struct if_clone *ifc, int unit)
{
	struct egre_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = &sc->sc_ac.ac_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	ifp->if_softc = sc;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_ioctl = egre_ioctl;
	ifp->if_start = egre_start;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ether_fakeaddr(ifp);

	sc->sc_tunnel.t_ttl = atomic_load_int(&ip_defttl);
	sc->sc_tunnel.t_txhprio = 0;
	sc->sc_tunnel.t_rxhprio = IF_HDRPRIO_PACKET;
	sc->sc_tunnel.t_df = htons(0);

	ifmedia_init(&sc->sc_media, 0, egre_media_change, egre_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

	return (0);
}

static int
egre_clone_destroy(struct ifnet *ifp)
{
	struct egre_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		egre_down(sc);
	NET_UNLOCK();

	ifmedia_delete_instance(&sc->sc_media, IFM_INST_ANY);
	ether_ifdetach(ifp);
	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
nvgre_clone_create(struct if_clone *ifc, int unit)
{
	struct nvgre_softc *sc;
	struct ifnet *ifp;
	struct gre_tunnel *tunnel;
	int error;

	if (nvgre_endpoint_pool.pr_size == 0) {
		pool_init(&nvgre_endpoint_pool, sizeof(union gre_addr),
		    0, IPL_SOFTNET, 0, "nvgreep", NULL);
	}

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = &sc->sc_ac.ac_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	error = etherbridge_init(&sc->sc_eb, ifp->if_xname,
	    &nvgre_etherbridge_ops, sc);
	if (error != 0) {
		free(sc, M_DEVBUF, sizeof(*sc));
		return (error);
	}

	ifp->if_softc = sc;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_ioctl = nvgre_ioctl;
	ifp->if_start = nvgre_start;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ether_fakeaddr(ifp);

	tunnel = &sc->sc_tunnel;
	tunnel->t_ttl = IP_DEFAULT_MULTICAST_TTL;
	tunnel->t_txhprio = 0;
	sc->sc_tunnel.t_rxhprio = IF_HDRPRIO_PACKET;
	tunnel->t_df = htons(IP_DF);
	tunnel->t_key_mask = GRE_KEY_ENTROPY;
	tunnel->t_key = htonl((NVGRE_VSID_RES_MAX + 1) <<
	    GRE_KEY_ENTROPY_SHIFT);

	mq_init(&sc->sc_send_list, IFQ_MAXLEN * 2, IPL_SOFTNET);
	task_set(&sc->sc_send_task, nvgre_send, sc);
	task_set(&sc->sc_ltask, nvgre_link_change, sc);
	task_set(&sc->sc_dtask, nvgre_detach, sc);

	ifmedia_init(&sc->sc_media, 0, egre_media_change, egre_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

	return (0);
}

static int
nvgre_clone_destroy(struct ifnet *ifp)
{
	struct nvgre_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		nvgre_down(sc);
	NET_UNLOCK();

	etherbridge_destroy(&sc->sc_eb);

	ifmedia_delete_instance(&sc->sc_media, IFM_INST_ANY);
	ether_ifdetach(ifp);
	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
eoip_clone_create(struct if_clone *ifc, int unit)
{
	struct eoip_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = &sc->sc_ac.ac_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	ifp->if_softc = sc;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_ioctl = eoip_ioctl;
	ifp->if_start = eoip_start;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ether_fakeaddr(ifp);

	sc->sc_tunnel.t_ttl = atomic_load_int(&ip_defttl);
	sc->sc_tunnel.t_txhprio = 0;
	sc->sc_tunnel.t_rxhprio = IF_HDRPRIO_PACKET;
	sc->sc_tunnel.t_df = htons(0);

	sc->sc_ka_timeo = 10;
	sc->sc_ka_count = 10;

	timeout_set(&sc->sc_ka_send, eoip_keepalive_send, sc);
	timeout_set_proc(&sc->sc_ka_hold, eoip_keepalive_hold, sc);
	sc->sc_ka_state = GRE_KA_DOWN;

	ifmedia_init(&sc->sc_media, 0, egre_media_change, egre_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

	return (0);
}

static int
eoip_clone_destroy(struct ifnet *ifp)
{
	struct eoip_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		eoip_down(sc);
	NET_UNLOCK();

	ifmedia_delete_instance(&sc->sc_media, IFM_INST_ANY);
	ether_ifdetach(ifp);
	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}


int
gre_input(struct mbuf **mp, int *offp, int type, int af, struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct gre_tunnel key;
	struct ip *ip;

	ip = mtod(m, struct ip *);

	/* XXX check if ip_src is sane for nvgre? */

	key.t_af = AF_INET;
	key.t_src4 = ip->ip_dst;
	key.t_dst4 = ip->ip_src;

	if (gre_input_key(mp, offp, type, af, ip->ip_tos, &key, ns) == -1)
		return (rip_input(mp, offp, type, af, ns));

	return (IPPROTO_DONE);
}

#ifdef INET6
int
gre_input6(struct mbuf **mp, int *offp, int type, int af, struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct gre_tunnel key;
	struct ip6_hdr *ip6;
	uint32_t flow;

	ip6 = mtod(m, struct ip6_hdr *);

	/* XXX check if ip6_src is sane for nvgre? */

	key.t_af = AF_INET6;
	key.t_src6 = ip6->ip6_dst;
	key.t_dst6 = ip6->ip6_src;

	flow = bemtoh32(&ip6->ip6_flow);

	if (gre_input_key(mp, offp, type, af, flow >> 20, &key, ns) == -1)
		return (rip6_input(mp, offp, type, af, ns));

	return (IPPROTO_DONE);
}
#endif /* INET6 */

static inline struct ifnet *
gre_find(const struct gre_tunnel *key)
{
	struct gre_softc *sc;

	TAILQ_FOREACH(sc, &gre_list, sc_entry) {
		if (gre_cmp(key, &sc->sc_tunnel) != 0)
			continue;

		if (!ISSET(sc->sc_if.if_flags, IFF_RUNNING))
			continue;

		return (&sc->sc_if);
	}

	return (NULL);
}

static inline struct ifnet *
mgre_find(const struct gre_tunnel *key)
{
	struct mgre_softc *sc;

	NET_ASSERT_LOCKED();
	sc = RBT_FIND(mgre_tree, &mgre_tree, (const struct mgre_softc *)key);
	if (sc != NULL)
		return (&sc->sc_if);

	return (NULL);
}

static struct mbuf *
gre_input_1(struct gre_tunnel *key, struct mbuf *m,
    const struct gre_header *gh, uint8_t otos, int iphlen, struct netstack *ns)
{
	switch (gh->gre_proto) {
	case htons(ETHERTYPE_PPP):
#ifdef PIPEX
		if (pipex_enable) {
			struct pipex_session *session;

			session = pipex_pptp_lookup_session(m);
			if (session != NULL) {
				struct mbuf *m0;

				m0 = pipex_pptp_input(m, session, ns);
				pipex_rele_session(session);

				if (m0 == NULL)
					return (NULL);
			}
		}
#endif
		break;
	case htons(GRE_EOIP):
		return (eoip_input(key, m, gh, otos, iphlen, ns));
		break;
	}

	return (m);
}

static int
gre_input_key(struct mbuf **mp, int *offp, int type, int af, uint8_t otos,
    struct gre_tunnel *key, struct netstack *ns)
{
	struct mbuf *m = *mp;
	int iphlen = *offp, hlen, rxprio;
	struct ifnet *ifp;
	const struct gre_tunnel *tunnel;
	caddr_t buf;
	struct gre_header *gh;
	struct gre_h_key *gkh;
	struct mbuf *(*patch)(const struct gre_tunnel *, struct mbuf *,
	    uint8_t *, uint8_t);
	int mcast = 0;
	uint8_t itos;

	if (!atomic_load_int(&gre_allow))
		goto decline;

	key->t_rtableid = m->m_pkthdr.ph_rtableid;

	hlen = iphlen + sizeof(*gh);
	if (m->m_pkthdr.len < hlen)
		goto decline;

	m = m_pullup(m, hlen);
	if (m == NULL)
		return (IPPROTO_DONE);

	buf = mtod(m, caddr_t);
	gh = (struct gre_header *)(buf + iphlen);

	/* check the version */
	switch (gh->gre_flags & htons(GRE_VERS_MASK)) {
	case htons(GRE_VERS_0):
		break;

	case htons(GRE_VERS_1):
		m = gre_input_1(key, m, gh, otos, iphlen, ns);
		if (m == NULL)
			return (IPPROTO_DONE);
		/* FALLTHROUGH */
	default:
		goto decline;
	}

	/*
	 * ERSPAN I uses no bits in the header, and II uses sequence numbers.
	 * handle them before limiting what flags we support. 
	 */
	if (gh->gre_proto == htons(GRE_ERSPAN)) {
		m = erspan_input(key, m, iphlen, gh, otos, ns);
		if (m == NULL)
			return (IPPROTO_DONE);

		goto decline;
	}

	/* the only optional bit in the header is K flag */
	if ((gh->gre_flags & htons(~(GRE_KP|GRE_VERS_MASK))) != htons(0))
		goto decline;

	if (gh->gre_flags & htons(GRE_KP)) {
		hlen += sizeof(*gkh);
		if (m->m_pkthdr.len < hlen)
			goto decline;

		m = m_pullup(m, hlen);
		if (m == NULL)
			return (IPPROTO_DONE);

		buf = mtod(m, caddr_t);
		gh = (struct gre_header *)(buf + iphlen);
		gkh = (struct gre_h_key *)(gh + 1);

		key->t_key_mask = GRE_KEY_MASK;
		key->t_key = gkh->gre_key;
	} else
		key->t_key_mask = GRE_KEY_NONE;

	if (gh->gre_proto == htons(ETHERTYPE_TRANSETHER)) {
		if (egre_input(key, m, hlen, otos, ns) == -1 &&
		    nvgre_input(key, m, hlen, otos, ns) == -1)
			goto decline;

		return (IPPROTO_DONE);
	}

	ifp = gre_find(key);
	if (ifp == NULL) {
		ifp = mgre_find(key);
		if (ifp == NULL)
			goto decline;
	}

	switch (gh->gre_proto) {
	case htons(GRE_WCCP): {
		struct mbuf *n;
		int off;

		/* WCCP/GRE:
		 *   So far as I can see (and test) it seems that Cisco's WCCP
		 *   GRE tunnel is precisely a IP-in-GRE tunnel that differs
		 *   only in its protocol number.  At least, it works for me.
		 *
		 *   The Internet Drafts can be found if you look for
		 *   the following:
		 *     draft-forster-wrec-wccp-v1-00.txt
		 *     draft-wilson-wrec-wccp-v2-01.txt
		 */

		if (!atomic_load_int(&gre_wccp) &&
		    !ISSET(ifp->if_flags, IFF_LINK0))
			goto decline;

		/*
		 * If the first nibble of the payload does not look like
		 * IPv4, assume it is WCCP v2.
		 */
		n = m_getptr(m, hlen, &off);
		if (n == NULL)
			goto decline;
		if (n->m_data[off] >> 4 != IPVERSION)
			hlen += 4;  /* four-octet Redirect header */

		/* FALLTHROUGH */
	}
	case htons(ETHERTYPE_IP):
		m->m_pkthdr.ph_family = AF_INET;
		patch = gre_ipv4_patch;
		break;
#ifdef INET6
	case htons(ETHERTYPE_IPV6):
		m->m_pkthdr.ph_family = AF_INET6;
		patch = gre_ipv6_patch;
		break;
#endif
#ifdef MPLS
	case htons(ETHERTYPE_MPLS_MCAST):
		mcast = M_MCAST|M_BCAST;
		/* fallthrough */
	case htons(ETHERTYPE_MPLS):
		m->m_pkthdr.ph_family = AF_MPLS;
		patch = gre_mpls_patch;
		break;
#endif
	case htons(0):
		if (ifp->if_type != IFT_TUNNEL) {
			/* keepalives dont make sense for mgre */
			goto decline;
		}

		m_adj(m, hlen);
		gre_keepalive_recv(ifp, m);
		return (IPPROTO_DONE);

	default:
		goto decline;
	}

	/* it's ours now */

	m_adj(m, hlen);

	tunnel = ifp->if_softc; /* gre and mgre tunnel info is at the front */

	m = (*patch)(tunnel, m, &itos, otos);
	if (m == NULL)
		return (IPPROTO_DONE);

	if (tunnel->t_key_mask == GRE_KEY_ENTROPY) {
		SET(m->m_pkthdr.csum_flags, M_FLOWID);
		m->m_pkthdr.ph_flowid =
		    bemtoh32(&key->t_key) & ~GRE_KEY_ENTROPY;
	}

	rxprio = tunnel->t_rxhprio;
	switch (rxprio) {
	case IF_HDRPRIO_PACKET:
		/* nop */
		break;
	case IF_HDRPRIO_OUTER:
		m->m_pkthdr.pf.prio = IFQ_TOS2PRIO(otos);
		break;
	case IF_HDRPRIO_PAYLOAD:
		m->m_pkthdr.pf.prio = IFQ_TOS2PRIO(itos);
		break;
	default:
		m->m_pkthdr.pf.prio = rxprio;
		break;
	}

	m->m_flags &= ~(M_MCAST|M_BCAST);
	m->m_flags |= mcast;

	if_vinput(ifp, m, ns);
	return (IPPROTO_DONE);
decline:
	*mp = m;
	return (-1);
}

static struct mbuf *
gre_ipv4_patch(const struct gre_tunnel *tunnel, struct mbuf *m,
    uint8_t *itosp, uint8_t otos)
{
	struct ip *ip;
	uint8_t itos;

	m = m_pullup(m, sizeof(*ip));
	if (m == NULL)
		return (NULL);

	ip = mtod(m, struct ip *);

	itos = ip->ip_tos;
	if (ip_ecn_egress(tunnel->t_ecn, &otos, &itos) == 0) {
		m_freem(m);
		return (NULL);
	}
	if (itos != ip->ip_tos)
		ip_tos_patch(ip, itos);

	*itosp = itos;

	return (m);
}

#ifdef INET6
static struct mbuf *
gre_ipv6_patch(const struct gre_tunnel *tunnel, struct mbuf *m,
    uint8_t *itosp, uint8_t otos)
{
	struct ip6_hdr *ip6;
	uint32_t flow;
	uint8_t itos;

	m = m_pullup(m, sizeof(*ip6));
	if (m == NULL)
		return (NULL);

	ip6 = mtod(m, struct ip6_hdr *);

	flow = bemtoh32(&ip6->ip6_flow);
	itos = flow >> 20;
	if (ip_ecn_egress(tunnel->t_ecn, &otos, &itos) == 0) {
		m_freem(m);
		return (NULL);
	}

	CLR(flow, 0xff << 20);
	SET(flow, itos << 20);
	htobem32(&ip6->ip6_flow, flow);

	*itosp = itos;

	return (m);
}
#endif

#ifdef MPLS
static struct mbuf *
gre_mpls_patch(const struct gre_tunnel *tunnel, struct mbuf *m,
    uint8_t *itosp, uint8_t otos)
{
	uint8_t itos;
	uint32_t shim;

	m = m_pullup(m, sizeof(shim));
	if (m == NULL)
		return (NULL);

	shim = *mtod(m, uint32_t *);
	itos = (ntohl(shim & MPLS_EXP_MASK) >> MPLS_EXP_OFFSET) << 5;

	if (ip_ecn_egress(tunnel->t_ecn, &otos, &itos) == 0) {
		m_freem(m);
		return (NULL);
	}

	*itosp = itos;

	return (m);
}
#endif

#define gre_l2_prio(_t, _m, _otos) do {					\
	int rxprio = (_t)->t_rxhprio;					\
	switch (rxprio) {						\
	case IF_HDRPRIO_PACKET:						\
		/* nop */						\
		break;							\
	case IF_HDRPRIO_OUTER:						\
		(_m)->m_pkthdr.pf.prio = IFQ_TOS2PRIO((_otos));		\
		break;							\
	default:							\
		(_m)->m_pkthdr.pf.prio = rxprio;			\
		break;							\
	}								\
} while (0)

static int
egre_input(const struct gre_tunnel *key, struct mbuf *m, int hlen,
    uint8_t otos, struct netstack *ns)
{
	struct egre_softc *sc;

	NET_ASSERT_LOCKED();
	sc = RBT_FIND(egre_tree, &egre_tree, (const struct egre_softc *)key);
	if (sc == NULL)
		return (-1);

	/* it's ours now */
	m = gre_ether_align(m, hlen);
	if (m == NULL)
		return (0);

	if (sc->sc_tunnel.t_key_mask == GRE_KEY_ENTROPY) {
		SET(m->m_pkthdr.csum_flags, M_FLOWID);
		m->m_pkthdr.ph_flowid =
		    bemtoh32(&key->t_key) & ~GRE_KEY_ENTROPY;
	}

	m->m_flags &= ~(M_MCAST|M_BCAST);

	gre_l2_prio(&sc->sc_tunnel, m, otos);

	if_vinput(&sc->sc_ac.ac_if, m, ns);

	return (0);
}

static inline struct nvgre_softc *
nvgre_mcast_find(const struct gre_tunnel *key, unsigned int if0idx)
{
	struct nvgre_softc *sc;
	int rv;

	/*
	 * building an nvgre_softc to use with RBT_FIND is expensive, and
	 * would need to swap the src and dst addresses in the key. so do the
	 * find by hand.
	 */

	NET_ASSERT_LOCKED();
	sc = RBT_ROOT(nvgre_mcast_tree, &nvgre_mcast_tree);
	while (sc != NULL) {
		rv = nvgre_cmp_mcast(key, &key->t_src, if0idx,
		    &sc->sc_tunnel, &sc->sc_tunnel.t_dst, sc->sc_ifp0);
		if (rv == 0)
			return (sc);
		if (rv < 0)
			sc = RBT_LEFT(nvgre_mcast_tree, sc);
		else
			sc = RBT_RIGHT(nvgre_mcast_tree, sc);
	}

	return (NULL);
}

static inline struct nvgre_softc *
nvgre_ucast_find(const struct gre_tunnel *key)
{
	NET_ASSERT_LOCKED();
	return (RBT_FIND(nvgre_ucast_tree, &nvgre_ucast_tree,
	    (struct nvgre_softc *)key));
}

static int
nvgre_input(const struct gre_tunnel *key, struct mbuf *m, int hlen,
    uint8_t otos, struct netstack *ns)
{
	struct nvgre_softc *sc;
	struct ether_header *eh;

	if (ISSET(m->m_flags, M_MCAST|M_BCAST))
		sc = nvgre_mcast_find(key, m->m_pkthdr.ph_ifidx);
	else
		sc = nvgre_ucast_find(key);

	if (sc == NULL)
		return (-1);

	/* it's ours now */
	m = gre_ether_align(m, hlen);
	if (m == NULL)
		return (0);

	eh = mtod(m, struct ether_header *);
	etherbridge_map_ea(&sc->sc_eb, (void *)&key->t_dst,
	    (struct ether_addr *)eh->ether_shost);

	SET(m->m_pkthdr.csum_flags, M_FLOWID);
	m->m_pkthdr.ph_flowid = bemtoh32(&key->t_key) & ~GRE_KEY_ENTROPY;

	m->m_flags &= ~(M_MCAST|M_BCAST);

	gre_l2_prio(&sc->sc_tunnel, m, otos);

	if_vinput(&sc->sc_ac.ac_if, m, ns);

	return (0);
}

static struct mbuf *
gre_ether_align(struct mbuf *m, int hlen)
{
	struct mbuf *n;
	int off;

	m_adj(m, hlen);

	if (m->m_pkthdr.len < sizeof(struct ether_header)) {
		m_freem(m);
		return (NULL);
	}

	m = m_pullup(m, sizeof(struct ether_header));
	if (m == NULL)
		return (NULL);

	n = m_getptr(m, sizeof(struct ether_header), &off);
	if (n == NULL) {
		m_freem(m);
		return (NULL);
	}

	if (!ALIGNED_POINTER(mtod(n, caddr_t) + off, uint32_t)) {
		n = m_dup_pkt(m, ETHER_ALIGN, M_NOWAIT);
		m_freem(m);
		if (n == NULL)
			return (NULL);
		m = n;
	}

	return (m);
}

static void
gre_keepalive_recv(struct ifnet *ifp, struct mbuf *m)
{
	struct gre_softc *sc = ifp->if_softc;
	struct gre_keepalive *gk;
	SIPHASH_CTX ctx;
	uint8_t digest[SIPHASH_DIGEST_LENGTH];
	int uptime, delta;
	int tick = ticks;

	if (sc->sc_ka_state == GRE_KA_NONE ||
	    sc->sc_tunnel.t_rtableid != sc->sc_if.if_rdomain)
		goto drop;

	if (m->m_pkthdr.len < sizeof(*gk))
		goto drop;
	m = m_pullup(m, sizeof(*gk));
	if (m == NULL)
		return;

	gk = mtod(m, struct gre_keepalive *);
	uptime = bemtoh32(&gk->gk_uptime) - sc->sc_ka_bias;
	delta = tick - uptime;
	if (delta < 0)
		goto drop;
	if (delta > hz * 10) /* magic */
		goto drop;

	/* avoid too much siphash work */
	delta = tick - sc->sc_ka_recvtm;
	if (delta > 0 && delta < (hz / 10))
		goto drop;

	SipHash24_Init(&ctx, &sc->sc_ka_key);
	SipHash24_Update(&ctx, &gk->gk_uptime, sizeof(gk->gk_uptime));
	SipHash24_Update(&ctx, &gk->gk_random, sizeof(gk->gk_random));
	SipHash24_Final(digest, &ctx);

	if (memcmp(digest, gk->gk_digest, sizeof(digest)) != 0)
		goto drop;

	sc->sc_ka_recvtm = tick;

	switch (sc->sc_ka_state) {
	case GRE_KA_DOWN:
		sc->sc_ka_state = GRE_KA_HOLD;
		sc->sc_ka_holdcnt = sc->sc_ka_holdmax;
		sc->sc_ka_holdmax = MIN(sc->sc_ka_holdmax * 2,
		    16 * sc->sc_ka_count);
		break;
	case GRE_KA_HOLD:
		if (--sc->sc_ka_holdcnt > 0)
			break;

		sc->sc_ka_state = GRE_KA_UP;
		gre_link_state(&sc->sc_if, sc->sc_ka_state);
		break;

	case GRE_KA_UP:
		sc->sc_ka_holdmax--;
		sc->sc_ka_holdmax = MAX(sc->sc_ka_holdmax, sc->sc_ka_count);
		break;
	}

	timeout_add_sec(&sc->sc_ka_hold, sc->sc_ka_timeo * sc->sc_ka_count);

drop:
	m_freem(m);
}

static int
gre_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	struct m_tag *mtag;
	int error = 0;

	if (!atomic_load_int(&gre_allow)) {
		error = EACCES;
		goto drop;
	}

	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		error = ENETDOWN;
		goto drop;
	}

	switch (dst->sa_family) {
	case AF_INET:
#ifdef INET6
	case AF_INET6:
#endif
#ifdef MPLS
	case AF_MPLS:
#endif
		break;
	default:
		error = EAFNOSUPPORT;
		goto drop;
	}

	/* Try to limit infinite recursion through misconfiguration. */
	for (mtag = m_tag_find(m, PACKET_TAG_GRE, NULL); mtag;
	     mtag = m_tag_find(m, PACKET_TAG_GRE, mtag)) {
		if (memcmp((caddr_t)(mtag + 1), &ifp->if_index,
		    sizeof(ifp->if_index)) == 0) {
			m_freem(m);
			error = EIO;
			goto end;
		}
	}

	mtag = m_tag_get(PACKET_TAG_GRE, sizeof(ifp->if_index), M_NOWAIT);
	if (mtag == NULL) {
		m_freem(m);
		error = ENOBUFS;
		goto end;
	}
	memcpy((caddr_t)(mtag + 1), &ifp->if_index, sizeof(ifp->if_index));
	m_tag_prepend(m, mtag);

	m->m_pkthdr.ph_family = dst->sa_family;

	error = if_enqueue(ifp, m);
end:
	if (error)
		ifp->if_oerrors++;
	return (error);

drop:
	m_freem(m);
	return (error);
}

void
gre_start(struct ifnet *ifp)
{
	struct gre_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int af;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	while ((m = ifq_dequeue(&ifp->if_snd)) != NULL) {
		af = m->m_pkthdr.ph_family;

#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_af(if_bpf, af, m, BPF_DIRECTION_OUT);
#endif

		m = gre_l3_encap(&sc->sc_tunnel, m, af);
		if (m == NULL || gre_ip_output(&sc->sc_tunnel, m) != 0) {
			ifp->if_oerrors++;
			continue;
		}
	}
}

void
mgre_rtrequest(struct ifnet *ifp, int req, struct rtentry *rt)
{
	struct ifnet *lo0ifp;
	struct ifaddr *ifa, *lo0ifa;

	switch (req) {
	case RTM_ADD:
		if (!ISSET(rt->rt_flags, RTF_LOCAL))
			break;

		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
			if (memcmp(rt_key(rt), ifa->ifa_addr,
			    rt_key(rt)->sa_len) == 0)
				break;
		}

		if (ifa == NULL)
			break;

		KASSERT(ifa == rt->rt_ifa);

		lo0ifp = if_get(rtable_loindex(ifp->if_rdomain));
		KASSERT(lo0ifp != NULL);
		TAILQ_FOREACH(lo0ifa, &lo0ifp->if_addrlist, ifa_list) {
			if (lo0ifa->ifa_addr->sa_family ==
			    ifa->ifa_addr->sa_family)
				break;
		}
		if_put(lo0ifp);

		if (lo0ifa == NULL)
			break;

		rt->rt_flags &= ~RTF_LLINFO;
		break;
	case RTM_DELETE:
	case RTM_RESOLVE:
	default:
		break;
	}
}

static int
mgre_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dest,
    struct rtentry *rt0)
{
	struct mgre_softc *sc = ifp->if_softc;
	struct sockaddr *gate;
	struct rtentry *rt;
	struct m_tag *mtag;
	int error = 0;
	sa_family_t af;
	const void *addr;

	if (!atomic_load_int(&gre_allow)) {
		error = EACCES;
		goto drop;
	}

	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		error = ENETDOWN;
		goto drop;
	}

	switch (dest->sa_family) {
	case AF_INET:
#ifdef INET6
	case AF_INET6:
#endif
#ifdef MPLS
	case AF_MPLS:
#endif
		break;
	default:
		error = EAFNOSUPPORT;
		goto drop;
	}

	if (ISSET(m->m_flags, M_MCAST|M_BCAST)) {
		error = ENETUNREACH;
		goto drop;
	}

	rt = rt_getll(rt0);

	/* check rt_expire? */
	if (rt == NULL || ISSET(rt->rt_flags, RTF_REJECT)) {
		error = (rt == rt0) ? EHOSTDOWN : EHOSTUNREACH;
		goto drop;
	}
	if (!ISSET(rt->rt_flags, RTF_HOST)) {
		error = EHOSTUNREACH;
		goto drop;
	}
	if (ISSET(rt->rt_flags, RTF_GATEWAY)) {
		error = EINVAL;
		goto drop;
	}

	gate = rt->rt_gateway;
	af = gate->sa_family;
	if (af != sc->sc_tunnel.t_af) {
		error = EAGAIN;
		goto drop;
	}

	/* Try to limit infinite recursion through misconfiguration. */
	for (mtag = m_tag_find(m, PACKET_TAG_GRE, NULL); mtag;
	     mtag = m_tag_find(m, PACKET_TAG_GRE, mtag)) {
		if (memcmp((caddr_t)(mtag + 1), &ifp->if_index,
		    sizeof(ifp->if_index)) == 0) {
			error = EIO;
			goto drop;
		}
	}

	mtag = m_tag_get(PACKET_TAG_GRE, sizeof(ifp->if_index), M_NOWAIT);
	if (mtag == NULL) {
		error = ENOBUFS;
		goto drop;
	}
	memcpy((caddr_t)(mtag + 1), &ifp->if_index, sizeof(ifp->if_index));
	m_tag_prepend(m, mtag);

	switch (af) {
	case AF_INET: {
		struct sockaddr_in *sin = (struct sockaddr_in *)gate;
		addr = &sin->sin_addr;
		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)gate;
		addr = &sin6->sin6_addr;
		break;
	}
 #endif
	default:
		unhandled_af(af);
		/* NOTREACHED */
	}

	m = gre_l3_encap_dst(&sc->sc_tunnel, addr, m, dest->sa_family);
	if (m == NULL) {
		ifp->if_oerrors++;
		return (ENOBUFS);
	}

	m->m_pkthdr.ph_family = dest->sa_family;

	error = if_enqueue(ifp, m);
	if (error)
		ifp->if_oerrors++;
	return (error);

drop:
	m_freem(m);
	return (error);
}

static void
mgre_start(struct ifnet *ifp)
{
	struct mgre_softc *sc = ifp->if_softc;
	struct mbuf *m;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	while ((m = ifq_dequeue(&ifp->if_snd)) != NULL) {
#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf) {
			struct m_hdr mh;
			struct mbuf *n;
			int off;

			n = m_getptr(m, ifp->if_hdrlen, &off);
			KASSERT(n != NULL);

			mh.mh_flags = 0;
			mh.mh_next = n->m_next;
			mh.mh_len = n->m_len - off;
			mh.mh_data = n->m_data + off;

			bpf_mtap_af(if_bpf, m->m_pkthdr.ph_family,
			    (struct mbuf *)&mh, BPF_DIRECTION_OUT);
		}
#endif

		if (gre_ip_output(&sc->sc_tunnel, m) != 0) {
			ifp->if_oerrors++;
			continue;
		}
	}
}

static void
egre_start(struct ifnet *ifp)
{
	struct egre_softc *sc = ifp->if_softc;
	struct mbuf *m0, *m;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	if (!atomic_load_int(&gre_allow)) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	while ((m0 = ifq_dequeue(&ifp->if_snd)) != NULL) {
#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_ether(if_bpf, m0, BPF_DIRECTION_OUT);
#endif

		/* force prepend mbuf because of alignment problems */
		m = m_get(M_DONTWAIT, m0->m_type);
		if (m == NULL) {
			m_freem(m0);
			continue;
		}

		M_MOVE_PKTHDR(m, m0);
		m->m_next = m0;

		m_align(m, 0);
		m->m_len = 0;

		m = gre_encap(&sc->sc_tunnel, m, htons(ETHERTYPE_TRANSETHER),
		    sc->sc_tunnel.t_ttl, gre_l2_tos(&sc->sc_tunnel, m));
		if (m == NULL || gre_ip_output(&sc->sc_tunnel, m) != 0) {
			ifp->if_oerrors++;
			continue;
		}
	}
}

static struct mbuf *
gre_l3_encap_dst(const struct gre_tunnel *tunnel, const void *dst,
    struct mbuf *m, sa_family_t af)
{
	uint16_t proto;
	uint8_t ttl, itos, otos;
	int tttl = tunnel->t_ttl;
	int ttloff;

	switch (af) {
	case AF_INET: {
		struct ip *ip;

		m = m_pullup(m, sizeof(*ip));
		if (m == NULL)
			return (NULL);

		ip = mtod(m, struct ip *);
		itos = ip->ip_tos;

		ttloff = offsetof(struct ip, ip_ttl);
		proto = htons(ETHERTYPE_IP);
		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct ip6_hdr *ip6;

		m = m_pullup(m, sizeof(*ip6));
		if (m == NULL)
			return (NULL);

		ip6 = mtod(m, struct ip6_hdr *);
		itos = (ntohl(ip6->ip6_flow) & 0x0ff00000) >> 20;

		ttloff = offsetof(struct ip6_hdr, ip6_hlim);
		proto = htons(ETHERTYPE_IPV6);
		break;
	}
 #endif
#ifdef MPLS
	case AF_MPLS: {
		uint32_t shim;

		m = m_pullup(m, sizeof(shim));
		if (m == NULL)
			return (NULL);

		shim = bemtoh32(mtod(m, uint32_t *)) & MPLS_EXP_MASK;
		itos = (shim >> MPLS_EXP_OFFSET) << 5;

		ttloff = 3;

		if (m->m_flags & (M_BCAST | M_MCAST))
			proto = htons(ETHERTYPE_MPLS_MCAST);
		else
			proto = htons(ETHERTYPE_MPLS);
		break;
	}
#endif
	default:
		unhandled_af(af);
	}

	if (tttl == -1) {
		KASSERT(m->m_len > ttloff); /* m_pullup has happened */

		ttl = *(m->m_data + ttloff);
	} else
		ttl = tttl;

	itos = gre_l3_tos(tunnel, m, itos);
	ip_ecn_ingress(tunnel->t_ecn, &otos, &itos);

	return (gre_encap_dst(tunnel, dst, m, proto, ttl, otos));
}

static struct mbuf *
gre_encap_dst(const struct gre_tunnel *tunnel, const union gre_addr *dst,
    struct mbuf *m, uint16_t proto, uint8_t ttl, uint8_t tos)
{
	struct gre_header *gh;
	struct gre_h_key *gkh;
	int hlen;

	hlen = sizeof(*gh);
	if (tunnel->t_key_mask != GRE_KEY_NONE)
		hlen += sizeof(*gkh);

	m = m_prepend(m, hlen, M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	gh = mtod(m, struct gre_header *);
	gh->gre_flags = GRE_VERS_0;
	gh->gre_proto = proto;
	if (tunnel->t_key_mask != GRE_KEY_NONE) {
		gh->gre_flags |= htons(GRE_KP);

		gkh = (struct gre_h_key *)(gh + 1);
		gkh->gre_key = tunnel->t_key;

		if (tunnel->t_key_mask == GRE_KEY_ENTROPY &&
		    ISSET(m->m_pkthdr.csum_flags, M_FLOWID)) {
			gkh->gre_key |= htonl(~GRE_KEY_ENTROPY &
			    m->m_pkthdr.ph_flowid);
		}
	}

	return (gre_encap_dst_ip(tunnel, dst, m, ttl, tos));
}

static struct mbuf *
gre_encap_dst_ip(const struct gre_tunnel *tunnel, const union gre_addr *dst,
    struct mbuf *m, uint8_t ttl, uint8_t tos)
{
	switch (tunnel->t_af) {
	case AF_UNSPEC:
		/* packets may arrive before tunnel is set up */
		m_freem(m);
		return (NULL);
	case AF_INET: {
		struct ip *ip;

		m = m_prepend(m, sizeof(*ip), M_DONTWAIT);
		if (m == NULL)
			return (NULL);

		ip = mtod(m, struct ip *);
		ip->ip_v = IPVERSION;
		ip->ip_hl = sizeof(*ip) >> 2;
		ip->ip_off = tunnel->t_df;
		ip->ip_tos = tos;
		ip->ip_len = htons(m->m_pkthdr.len);
		ip->ip_ttl = ttl;
		ip->ip_p = IPPROTO_GRE;
		ip->ip_src = tunnel->t_src4;
		ip->ip_dst = dst->in4;
		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct ip6_hdr *ip6;
		int len = m->m_pkthdr.len;

		m = m_prepend(m, sizeof(*ip6), M_DONTWAIT);
		if (m == NULL)
			return (NULL);

		ip6 = mtod(m, struct ip6_hdr *);
		ip6->ip6_flow = ISSET(m->m_pkthdr.csum_flags, M_FLOWID) ?
		    htonl(m->m_pkthdr.ph_flowid) : 0;
		ip6->ip6_vfc |= IPV6_VERSION;
		ip6->ip6_flow |= htonl((uint32_t)tos << 20);
		ip6->ip6_plen = htons(len);
		ip6->ip6_nxt = IPPROTO_GRE;
		ip6->ip6_hlim = ttl;
		ip6->ip6_src = tunnel->t_src6;
		ip6->ip6_dst = dst->in6;

		if (tunnel->t_df)
			SET(m->m_pkthdr.csum_flags, M_IPV6_DF_OUT);

		break;
	}
#endif /* INET6 */
	default:
		unhandled_af(tunnel->t_af);
	}

	return (m);
}

static int
gre_ip_output(const struct gre_tunnel *tunnel, struct mbuf *m)
{
	m->m_flags &= ~(M_BCAST|M_MCAST);
	m->m_pkthdr.ph_rtableid = tunnel->t_rtableid;

#if NPF > 0
	pf_pkt_addr_changed(m);
#endif

	switch (tunnel->t_af) {
	case AF_INET:
		ip_send(m);
		break;
#ifdef INET6
	case AF_INET6:
		ip6_send(m);
		break;
#endif
	default:
		unhandled_af(tunnel->t_af);
	}

	return (0);
}

static int
gre_tunnel_ioctl(struct ifnet *ifp, struct gre_tunnel *tunnel,
    u_long cmd, void *data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch(cmd) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < 576) {
			error = EINVAL;
			break;
		}
		ifp->if_mtu = ifr->ifr_mtu;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCSVNETID:
		error = gre_set_vnetid(tunnel, ifr);
		break;

	case SIOCGVNETID:
		error = gre_get_vnetid(tunnel, ifr);
		break;
	case SIOCDVNETID:
		error = gre_del_vnetid(tunnel);
		break;

	case SIOCSVNETFLOWID:
		error = gre_set_vnetflowid(tunnel, ifr);
		break;

	case SIOCGVNETFLOWID:
		error = gre_get_vnetflowid(tunnel, ifr);
		break;

	case SIOCSLIFPHYADDR:
		error = gre_set_tunnel(tunnel, (struct if_laddrreq *)data, 1);
		break;
	case SIOCGLIFPHYADDR:
		error = gre_get_tunnel(tunnel, (struct if_laddrreq *)data);
		break;
	case SIOCDIFPHYADDR:
		error = gre_del_tunnel(tunnel);
		break;

	case SIOCSLIFPHYRTABLE:
		if (ifr->ifr_rdomainid < 0 ||
		    ifr->ifr_rdomainid > RT_TABLEID_MAX ||
		    !rtable_exists(ifr->ifr_rdomainid)) {
			error = EINVAL;
			break;
		}
		tunnel->t_rtableid = ifr->ifr_rdomainid;
		break;
	case SIOCGLIFPHYRTABLE:
		ifr->ifr_rdomainid = tunnel->t_rtableid;
		break;

	case SIOCSLIFPHYDF:
		/* commit */
		tunnel->t_df = ifr->ifr_df ? htons(IP_DF) : htons(0);
		break;
	case SIOCGLIFPHYDF:
		ifr->ifr_df = tunnel->t_df ? 1 : 0;
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

static uint8_t
gre_l2_tos(const struct gre_tunnel *t, const struct mbuf *m)
{
	uint8_t prio;

	switch (t->t_txhprio) {
	case IF_HDRPRIO_PACKET:
		prio = m->m_pkthdr.pf.prio;
		break;
	default:
		prio = t->t_txhprio;
		break;
	}

	return (IFQ_PRIO2TOS(prio));
}

static uint8_t
gre_l3_tos(const struct gre_tunnel *t, const struct mbuf *m, uint8_t tos)
{
	uint8_t prio;

	switch (t->t_txhprio) {
	case IF_HDRPRIO_PAYLOAD:
		return (tos);
	case IF_HDRPRIO_PACKET:
		prio = m->m_pkthdr.pf.prio;
		break;
	default:
		prio = t->t_txhprio;
		break;
	}

	return (IFQ_PRIO2TOS(prio) | (tos & IPTOS_ECN_MASK));
}

static int
gre_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct gre_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct ifkalivereq *ikar = (struct ifkalivereq *)data;
	int error = 0;

	switch(cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = gre_up(sc);
			else
				error = 0;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = gre_down(sc);
		}
		break;
	case SIOCSIFRDOMAIN:
		/* let if_rdomain do its thing */
		error = ENOTTY;
		break;

	case SIOCSETKALIVE:
		if (ikar->ikar_timeo < 0 || ikar->ikar_timeo > 86400 ||
		    ikar->ikar_cnt < 0 || ikar->ikar_cnt > 256 ||
		    (ikar->ikar_timeo == 0) != (ikar->ikar_cnt == 0))
			return (EINVAL);

		if (ikar->ikar_timeo == 0 || ikar->ikar_cnt == 0) {
			sc->sc_ka_count = 0;
			sc->sc_ka_timeo = 0;
			sc->sc_ka_state = GRE_KA_NONE;
		} else {
			sc->sc_ka_count = ikar->ikar_cnt;
			sc->sc_ka_timeo = ikar->ikar_timeo;
			sc->sc_ka_state = GRE_KA_DOWN;

			arc4random_buf(&sc->sc_ka_key, sizeof(sc->sc_ka_key));
			sc->sc_ka_bias = arc4random();
			sc->sc_ka_holdmax = sc->sc_ka_count;

			sc->sc_ka_recvtm = ticks - hz;
			timeout_add(&sc->sc_ka_send, 1);
			timeout_add_sec(&sc->sc_ka_hold,
			    sc->sc_ka_timeo * sc->sc_ka_count);
		}
		break;

	case SIOCGETKALIVE:
		ikar->ikar_cnt = sc->sc_ka_count;
		ikar->ikar_timeo = sc->sc_ka_timeo;
		break;

	case SIOCSLIFPHYTTL:
		if (ifr->ifr_ttl != -1 &&
		    (ifr->ifr_ttl < 1 || ifr->ifr_ttl > 0xff)) {
			error = EINVAL;
			break;
		}

		/* commit */
		sc->sc_tunnel.t_ttl = ifr->ifr_ttl;
		break;

	case SIOCGLIFPHYTTL:
		ifr->ifr_ttl = sc->sc_tunnel.t_ttl;
		break;

	case SIOCSLIFPHYECN:
		sc->sc_tunnel.t_ecn =
		    ifr->ifr_metric ? ECN_ALLOWED : ECN_FORBIDDEN;
		break;
	case SIOCGLIFPHYECN:
		ifr->ifr_metric = (sc->sc_tunnel.t_ecn == ECN_ALLOWED);
		break;

	case SIOCSTXHPRIO:
		error = if_txhprio_l3_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_txhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGTXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_txhprio;
		break;

	case SIOCSRXHPRIO:
		error = if_rxhprio_l3_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_rxhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGRXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_rxhprio;
		break;

	default:
		error = gre_tunnel_ioctl(ifp, &sc->sc_tunnel, cmd, data);
		break;
	}

	return (error);
}

static int
mgre_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct mgre_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch(cmd) {
	case SIOCSIFADDR:
		break;
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = mgre_up(sc);
			else
				error = 0;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = mgre_down(sc);
		}
		break;

	case SIOCSLIFPHYTTL:
		if (ifr->ifr_ttl != -1 &&
		    (ifr->ifr_ttl < 1 || ifr->ifr_ttl > 0xff)) {
			error = EINVAL;
			break;
		}

		/* commit */
		sc->sc_tunnel.t_ttl = ifr->ifr_ttl;
		break;

	case SIOCGLIFPHYTTL:
		ifr->ifr_ttl = sc->sc_tunnel.t_ttl;
		break;

	case SIOCSLIFPHYECN:
		sc->sc_tunnel.t_ecn =
		    ifr->ifr_metric ? ECN_ALLOWED : ECN_FORBIDDEN;
		break;
	case SIOCGLIFPHYECN:
		ifr->ifr_metric = (sc->sc_tunnel.t_ecn == ECN_ALLOWED);
		break;

	case SIOCSLIFPHYADDR:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}
		error = mgre_set_tunnel(sc, (struct if_laddrreq *)data);
		break;
	case SIOCGLIFPHYADDR:
		error = mgre_get_tunnel(sc, (struct if_laddrreq *)data);
		break;

	case SIOCSTXHPRIO:
		error = if_txhprio_l3_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_txhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGTXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_txhprio;
		break;

	case SIOCSRXHPRIO:
		error = if_rxhprio_l3_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_rxhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGRXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_rxhprio;
		break;

	case SIOCSVNETID:
	case SIOCDVNETID:
	case SIOCDIFPHYADDR:
	case SIOCSLIFPHYRTABLE:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}

		/* FALLTHROUGH */
	default:
		error = gre_tunnel_ioctl(ifp, &sc->sc_tunnel, cmd, data);
		break;
	}

	return (error);
}

static int
mgre_set_tunnel(struct mgre_softc *sc, struct if_laddrreq *req)
{
	struct gre_tunnel *tunnel = &sc->sc_tunnel;
	struct sockaddr *addr = (struct sockaddr *)&req->addr;
	struct sockaddr *dstaddr = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *addr4;
#ifdef INET6
	struct sockaddr_in6 *addr6;
	int error;
#endif

	if (dstaddr->sa_family != AF_UNSPEC)
		return (EINVAL);

	/* validate */
	switch (addr->sa_family) {
	case AF_INET:
		if (addr->sa_len != sizeof(*addr4))
			return (EINVAL);

		addr4 = (struct sockaddr_in *)addr;
		if (in_nullhost(addr4->sin_addr) ||
		    IN_MULTICAST(addr4->sin_addr.s_addr))
			return (EINVAL);

		tunnel->t_src4 = addr4->sin_addr;
		tunnel->t_dst4.s_addr = INADDR_ANY;

		break;
#ifdef INET6
	case AF_INET6:
		if (addr->sa_len != sizeof(*addr6))
			return (EINVAL);

		addr6 = (struct sockaddr_in6 *)addr;
		if (IN6_IS_ADDR_UNSPECIFIED(&addr6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&addr6->sin6_addr))
			return (EINVAL);

		error = in6_embedscope(&tunnel->t_src6, addr6, NULL, NULL);
		if (error != 0)
			return (error);

		memset(&tunnel->t_dst6, 0, sizeof(tunnel->t_dst6));

		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}

	/* commit */
	tunnel->t_af = addr->sa_family;

	return (0);
}

static int
mgre_get_tunnel(struct mgre_softc *sc, struct if_laddrreq *req)
{
	struct gre_tunnel *tunnel = &sc->sc_tunnel;
	struct sockaddr *dstaddr = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif

	switch (tunnel->t_af) {
	case AF_UNSPEC:
		return (EADDRNOTAVAIL);
	case AF_INET:
		sin = (struct sockaddr_in *)&req->addr;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = tunnel->t_src4;
		break;

#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&req->addr;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(*sin6);
		in6_recoverscope(sin6, &tunnel->t_src6);
		break;
#endif
	default:
		unhandled_af(tunnel->t_af);
	}

	dstaddr->sa_len = 2;
	dstaddr->sa_family = AF_UNSPEC;

	return (0);
}

static int
egre_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct egre_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch(cmd) {
	case SIOCSIFADDR:
		break;
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = egre_up(sc);
			else
				error = 0;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = egre_down(sc);
		}
		break;

	case SIOCSLIFPHYTTL:
		if (ifr->ifr_ttl < 1 || ifr->ifr_ttl > 0xff) {
			error = EINVAL;
			break;
		}

		/* commit */
		sc->sc_tunnel.t_ttl = (uint8_t)ifr->ifr_ttl;
		break;

	case SIOCGLIFPHYTTL:
		ifr->ifr_ttl = (int)sc->sc_tunnel.t_ttl;
		break;

	case SIOCSTXHPRIO:
		error = if_txhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_txhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGTXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_txhprio;
		break;

	case SIOCSRXHPRIO:
		error = if_rxhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_rxhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGRXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_rxhprio;
		break;

	case SIOCSVNETID:
	case SIOCDVNETID:
	case SIOCSVNETFLOWID:
	case SIOCSLIFPHYADDR:
	case SIOCDIFPHYADDR:
	case SIOCSLIFPHYRTABLE:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}

		/* FALLTHROUGH */
	default:
		error = gre_tunnel_ioctl(ifp, &sc->sc_tunnel, cmd, data);
		if (error == ENOTTY)
			error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		/* no hardware to program */
		error = 0;
	}

	return (error);
}

static int
nvgre_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct nvgre_softc *sc = ifp->if_softc;
	struct gre_tunnel *tunnel = &sc->sc_tunnel;

	struct ifreq *ifr = (struct ifreq *)data;
	struct if_parent *parent = (struct if_parent *)data;
	struct ifbrparam *bparam = (struct ifbrparam *)data;
	struct ifnet *ifp0;

	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		break;
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = nvgre_up(sc);
			else
				error = ENETRESET;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = nvgre_down(sc);
		}
		break;

	case SIOCSLIFPHYADDR:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}
		error = gre_set_tunnel(tunnel, (struct if_laddrreq *)data, 0);
		if (error == 0)
			etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);
		break;
	case SIOCGLIFPHYADDR:
		error = gre_get_tunnel(tunnel, (struct if_laddrreq *)data);
		break;
	case SIOCDIFPHYADDR:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}
		error = gre_del_tunnel(tunnel);
		if (error == 0)
			etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);
		break;

	case SIOCSIFPARENT:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}
		error = nvgre_set_parent(sc, parent->ifp_parent);
		if (error == 0)
			etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);
		break;
	case SIOCGIFPARENT:
		ifp0 = if_get(sc->sc_ifp0);
		if (ifp0 == NULL)
			error = EADDRNOTAVAIL;
		else {
			memcpy(parent->ifp_parent, ifp0->if_xname,
			    sizeof(parent->ifp_parent));
		}
		if_put(ifp0);
		break;
	case SIOCDIFPARENT:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}
		/* commit */
		sc->sc_ifp0 = 0;
		etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);
		break;

	case SIOCSVNETID:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}
		if (ifr->ifr_vnetid < GRE_KEY_ENTROPY_MIN ||
		    ifr->ifr_vnetid > GRE_KEY_ENTROPY_MAX) {
			error = EINVAL;
			break;
		}

		/* commit */
		tunnel->t_key = htonl(ifr->ifr_vnetid << GRE_KEY_ENTROPY_SHIFT);
		etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);
		break;
	case SIOCGVNETID:
		error = gre_get_vnetid(tunnel, ifr);
		break;

	case SIOCSLIFPHYRTABLE:
		if (ifr->ifr_rdomainid < 0 ||
		    ifr->ifr_rdomainid > RT_TABLEID_MAX ||
		    !rtable_exists(ifr->ifr_rdomainid)) {
			error = EINVAL;
			break;
		}
		tunnel->t_rtableid = ifr->ifr_rdomainid;
		etherbridge_flush(&sc->sc_eb, IFBF_FLUSHALL);
		break;
	case SIOCGLIFPHYRTABLE:
		ifr->ifr_rdomainid = tunnel->t_rtableid;
		break;

	case SIOCSLIFPHYDF:
		/* commit */
		tunnel->t_df = ifr->ifr_df ? htons(IP_DF) : htons(0);
		break;
	case SIOCGLIFPHYDF:
		ifr->ifr_df = tunnel->t_df ? 1 : 0;
		break;

	case SIOCSLIFPHYTTL:
		if (ifr->ifr_ttl < 1 || ifr->ifr_ttl > 0xff) {
			error = EINVAL;
			break;
		}

		/* commit */
		tunnel->t_ttl = ifr->ifr_ttl;
		break;

	case SIOCGLIFPHYTTL:
		ifr->ifr_ttl = tunnel->t_ttl;
		break;

	case SIOCSTXHPRIO:
		error = if_txhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_txhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGTXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_txhprio;
		break;

	case SIOCSRXHPRIO:
		error = if_rxhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_rxhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGRXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_rxhprio;
		break;

	case SIOCBRDGSCACHE:
		error = etherbridge_set_max(&sc->sc_eb, bparam);
		break;
	case SIOCBRDGGCACHE:
		error = etherbridge_get_max(&sc->sc_eb, bparam);
		break;

	case SIOCBRDGSTO:
		error = etherbridge_set_tmo(&sc->sc_eb, bparam);
		break;
	case SIOCBRDGGTO:
		error = etherbridge_get_tmo(&sc->sc_eb, bparam);
		break;

	case SIOCBRDGRTS:
		error = etherbridge_rtfind(&sc->sc_eb,
		    (struct ifbaconf *)data);
		break;
	case SIOCBRDGFLUSH:
		etherbridge_flush(&sc->sc_eb,
		    ((struct ifbreq *)data)->ifbr_ifsflags);
		break;
	case SIOCBRDGSADDR:
		error = nvgre_add_addr(sc, (struct ifbareq *)data);
		break;
	case SIOCBRDGDADDR:
		error = nvgre_del_addr(sc, (struct ifbareq *)data);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		/* no hardware to program */
		error = 0;
	}

	return (error);
}

static int
eoip_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct eoip_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct ifkalivereq *ikar = (struct ifkalivereq *)data;
	int error = 0;

	switch(cmd) {
	case SIOCSIFADDR:
		break;
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = eoip_up(sc);
			else
				error = 0;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = eoip_down(sc);
		}
		break;

	case SIOCSETKALIVE:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}

		if (ikar->ikar_timeo < 0 || ikar->ikar_timeo > 86400 ||
		    ikar->ikar_cnt < 0 || ikar->ikar_cnt > 256)
			return (EINVAL);

		if (ikar->ikar_timeo == 0 || ikar->ikar_cnt == 0) {
			sc->sc_ka_count = 0;
			sc->sc_ka_timeo = 0;
			sc->sc_ka_state = GRE_KA_NONE;
		} else {
			sc->sc_ka_count = ikar->ikar_cnt;
			sc->sc_ka_timeo = ikar->ikar_timeo;
			sc->sc_ka_state = GRE_KA_DOWN;
		}
		break;

	case SIOCGETKALIVE:
		ikar->ikar_cnt = sc->sc_ka_count;
		ikar->ikar_timeo = sc->sc_ka_timeo;
		break;

	case SIOCSVNETID:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}
		if (ifr->ifr_vnetid < 0 || ifr->ifr_vnetid > 0xffff)
			return (EINVAL);

		sc->sc_tunnel.t_key = htole16(ifr->ifr_vnetid); /* for cmp */
		sc->sc_tunnel_id = htole16(ifr->ifr_vnetid);
		break;

	case SIOCGVNETID:
		ifr->ifr_vnetid = letoh16(sc->sc_tunnel_id);
		break;

	case SIOCSLIFPHYADDR:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}

		error = gre_set_tunnel(&sc->sc_tunnel,
		    (struct if_laddrreq *)data, 1);
		break;
	case SIOCGLIFPHYADDR:
		error = gre_get_tunnel(&sc->sc_tunnel,
		    (struct if_laddrreq *)data);
		break;
	case SIOCDIFPHYADDR:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}

		error = gre_del_tunnel(&sc->sc_tunnel);
		break;

	case SIOCSLIFPHYRTABLE:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}

		if (ifr->ifr_rdomainid < 0 ||
		    ifr->ifr_rdomainid > RT_TABLEID_MAX ||
		    !rtable_exists(ifr->ifr_rdomainid)) {
			error = EINVAL;
			break;
		}
		sc->sc_tunnel.t_rtableid = ifr->ifr_rdomainid;
		break;
	case SIOCGLIFPHYRTABLE:
		ifr->ifr_rdomainid = sc->sc_tunnel.t_rtableid;
		break;

	case SIOCSLIFPHYTTL:
		if (ifr->ifr_ttl < 1 || ifr->ifr_ttl > 0xff) {
			error = EINVAL;
			break;
		}

		/* commit */
		sc->sc_tunnel.t_ttl = (uint8_t)ifr->ifr_ttl;
		break;
	case SIOCGLIFPHYTTL:
		ifr->ifr_ttl = (int)sc->sc_tunnel.t_ttl;
		break;

	case SIOCSLIFPHYDF:
		/* commit */
		sc->sc_tunnel.t_df = ifr->ifr_df ? htons(IP_DF) : htons(0);
		break;
	case SIOCGLIFPHYDF:
		ifr->ifr_df = sc->sc_tunnel.t_df ? 1 : 0;
		break;

	case SIOCSTXHPRIO:
		error = if_txhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_txhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGTXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_txhprio;
		break;

	case SIOCSRXHPRIO:
		error = if_rxhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_rxhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGRXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_rxhprio;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		/* no hardware to program */
		error = 0;
	}

	return (error);
}

static int
gre_up(struct gre_softc *sc)
{
	NET_ASSERT_LOCKED();
	SET(sc->sc_if.if_flags, IFF_RUNNING);

	if (sc->sc_ka_state != GRE_KA_NONE)
		gre_keepalive_send(sc);

	return (0);
}

static int
gre_down(struct gre_softc *sc)
{
	NET_ASSERT_LOCKED();
	CLR(sc->sc_if.if_flags, IFF_RUNNING);

	if (sc->sc_ka_state != GRE_KA_NONE) {
		timeout_del_barrier(&sc->sc_ka_hold);
		timeout_del_barrier(&sc->sc_ka_send);

		sc->sc_ka_state = GRE_KA_DOWN;
		gre_link_state(&sc->sc_if, sc->sc_ka_state);
	}

	return (0);
}

static void
gre_link_state(struct ifnet *ifp, unsigned int state)
{
	int link_state = LINK_STATE_UNKNOWN;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		switch (state) {
		case GRE_KA_NONE:
			/* maybe up? or down? it's unknown, really */
			break;
		case GRE_KA_UP:
			link_state = LINK_STATE_UP;
			break;
		default:
			link_state = LINK_STATE_KALIVE_DOWN;
			break;
		}
	}

	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}

static void
gre_keepalive_send(void *arg)
{
	struct gre_tunnel t;
	struct gre_softc *sc = arg;
	struct mbuf *m;
	struct gre_keepalive *gk;
	SIPHASH_CTX ctx;
	int linkhdr, len;
	uint16_t proto;
	uint8_t ttl;
	uint8_t tos;

	/*
	 * re-schedule immediately, so we deal with incomplete configuration
	 * or temporary errors.
	 */
	if (sc->sc_ka_timeo)
		timeout_add_sec(&sc->sc_ka_send, sc->sc_ka_timeo);

	if (!ISSET(sc->sc_if.if_flags, IFF_RUNNING) ||
	    sc->sc_ka_state == GRE_KA_NONE ||
	    sc->sc_tunnel.t_af == AF_UNSPEC ||
	    sc->sc_tunnel.t_rtableid != sc->sc_if.if_rdomain)
		return;

	/* this is really conservative */
#ifdef INET6
	linkhdr = max_linkhdr + MAX(sizeof(struct ip), sizeof(struct ip6_hdr)) +
	    sizeof(struct gre_header) + sizeof(struct gre_h_key);
#else
	linkhdr = max_linkhdr + sizeof(struct ip) +
	    sizeof(struct gre_header) + sizeof(struct gre_h_key);
#endif
	len = linkhdr + sizeof(*gk);

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return;

	if (len > MHLEN) {
		MCLGETL(m, M_DONTWAIT, len);
		if (!ISSET(m->m_flags, M_EXT)) {
			m_freem(m);
			return;
		}
	}

	m->m_pkthdr.len = m->m_len = len;
	m_adj(m, linkhdr);

	/*
	 * build the inside packet
	 */
	gk = mtod(m, struct gre_keepalive *);
	htobem32(&gk->gk_uptime, sc->sc_ka_bias + ticks);
	htobem32(&gk->gk_random, arc4random());

	SipHash24_Init(&ctx, &sc->sc_ka_key);
	SipHash24_Update(&ctx, &gk->gk_uptime, sizeof(gk->gk_uptime));
	SipHash24_Update(&ctx, &gk->gk_random, sizeof(gk->gk_random));
	SipHash24_Final(gk->gk_digest, &ctx);

	ttl = sc->sc_tunnel.t_ttl == -1 ?
	    atomic_load_int(&ip_defttl) : sc->sc_tunnel.t_ttl;

	m->m_pkthdr.pf.prio = sc->sc_if.if_llprio;
	tos = gre_l3_tos(&sc->sc_tunnel, m, IFQ_PRIO2TOS(m->m_pkthdr.pf.prio));

	t.t_af = sc->sc_tunnel.t_af;
	t.t_df = sc->sc_tunnel.t_df;
	t.t_src = sc->sc_tunnel.t_dst;
	t.t_dst = sc->sc_tunnel.t_src;
	t.t_key = sc->sc_tunnel.t_key;
	t.t_key_mask = sc->sc_tunnel.t_key_mask;

	m = gre_encap(&t, m, htons(0), ttl, tos);
	if (m == NULL)
		return;

	switch (sc->sc_tunnel.t_af) {
	case AF_INET: {
		struct ip *ip;

		ip = mtod(m, struct ip *);
		ip->ip_id = htons(ip_randomid());
		in_hdr_cksum_out(m, NULL);

		proto = htons(ETHERTYPE_IP);
		break;
	}
#ifdef INET6
	case AF_INET6:
		proto = htons(ETHERTYPE_IPV6);
		break;
#endif
	default:
		m_freem(m);
		return;
	}

	/*
	 * put it in the tunnel
	 */
	m = gre_encap(&sc->sc_tunnel, m, proto, ttl, tos);
	if (m == NULL)
		return;

	gre_ip_output(&sc->sc_tunnel, m);
}

static void
gre_keepalive_hold(void *arg)
{
	struct gre_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_if;

	if (!ISSET(ifp->if_flags, IFF_RUNNING) ||
	    sc->sc_ka_state == GRE_KA_NONE)
		return;

	NET_LOCK();
	sc->sc_ka_state = GRE_KA_DOWN;
	gre_link_state(ifp, sc->sc_ka_state);
	NET_UNLOCK();
}

static int
gre_set_tunnel(struct gre_tunnel *tunnel, struct if_laddrreq *req, int ucast)
{
	struct sockaddr *src = (struct sockaddr *)&req->addr;
	struct sockaddr *dst = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *src4, *dst4;
#ifdef INET6
	struct sockaddr_in6 *src6, *dst6;
	int error;
#endif

	/* sa_family and sa_len must be equal */
	if (src->sa_family != dst->sa_family || src->sa_len != dst->sa_len)
		return (EINVAL);

	/* validate */
	switch (dst->sa_family) {
	case AF_INET:
		if (dst->sa_len != sizeof(*dst4))
			return (EINVAL);

		src4 = (struct sockaddr_in *)src;
		if (in_nullhost(src4->sin_addr) ||
		    IN_MULTICAST(src4->sin_addr.s_addr))
			return (EINVAL);

		dst4 = (struct sockaddr_in *)dst;
		if (in_nullhost(dst4->sin_addr) ||
		    (IN_MULTICAST(dst4->sin_addr.s_addr) != !ucast))
			return (EINVAL);

		tunnel->t_src4 = src4->sin_addr;
		tunnel->t_dst4 = dst4->sin_addr;

		break;
#ifdef INET6
	case AF_INET6:
		if (dst->sa_len != sizeof(*dst6))
			return (EINVAL);

		src6 = (struct sockaddr_in6 *)src;
		if (IN6_IS_ADDR_UNSPECIFIED(&src6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&src6->sin6_addr))
			return (EINVAL);

		dst6 = (struct sockaddr_in6 *)dst;
		if (IN6_IS_ADDR_UNSPECIFIED(&dst6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&dst6->sin6_addr) != !ucast)
			return (EINVAL);

		if (src6->sin6_scope_id != dst6->sin6_scope_id)
			return (EINVAL);

		error = in6_embedscope(&tunnel->t_src6, src6, NULL, NULL);
		if (error != 0)
			return (error);

		error = in6_embedscope(&tunnel->t_dst6, dst6, NULL, NULL);
		if (error != 0)
			return (error);

		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}

	/* commit */
	tunnel->t_af = dst->sa_family;

	return (0);
}

static int
gre_get_tunnel(struct gre_tunnel *tunnel, struct if_laddrreq *req)
{
	struct sockaddr *src = (struct sockaddr *)&req->addr;
	struct sockaddr *dst = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *sin;
#ifdef INET6 /* ifconfig already embeds the scopeid */
	struct sockaddr_in6 *sin6;
#endif

	switch (tunnel->t_af) {
	case AF_UNSPEC:
		return (EADDRNOTAVAIL);
	case AF_INET:
		sin = (struct sockaddr_in *)src;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = tunnel->t_src4;

		sin = (struct sockaddr_in *)dst;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = tunnel->t_dst4;

		break;

#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)src;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(*sin6);
		in6_recoverscope(sin6, &tunnel->t_src6);

		sin6 = (struct sockaddr_in6 *)dst;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(*sin6);
		in6_recoverscope(sin6, &tunnel->t_dst6);

		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}

	return (0);
}

static int
gre_del_tunnel(struct gre_tunnel *tunnel)
{
	/* commit */
	tunnel->t_af = AF_UNSPEC;

	return (0);
}

static int
gre_set_vnetid(struct gre_tunnel *tunnel, struct ifreq *ifr)
{
	uint32_t key;
	uint32_t min = GRE_KEY_MIN;
	uint32_t max = GRE_KEY_MAX;
	unsigned int shift = GRE_KEY_SHIFT;
	uint32_t mask = GRE_KEY_MASK;

	if (tunnel->t_key_mask == GRE_KEY_ENTROPY) {
		min = GRE_KEY_ENTROPY_MIN;
		max = GRE_KEY_ENTROPY_MAX;
		shift = GRE_KEY_ENTROPY_SHIFT;
		mask = GRE_KEY_ENTROPY;
	}

	if (ifr->ifr_vnetid < min || ifr->ifr_vnetid > max)
		return (EINVAL);

	key = htonl(ifr->ifr_vnetid << shift);

	/* commit */
	tunnel->t_key_mask = mask;
	tunnel->t_key = key;

	return (0);
}

static int
gre_get_vnetid(struct gre_tunnel *tunnel, struct ifreq *ifr)
{
	int shift;

	switch (tunnel->t_key_mask) {
	case GRE_KEY_NONE:
		return (EADDRNOTAVAIL);
	case GRE_KEY_ENTROPY:
		shift = GRE_KEY_ENTROPY_SHIFT;
		break;
	case GRE_KEY_MASK:
		shift = GRE_KEY_SHIFT;
		break;
	}

	ifr->ifr_vnetid = ntohl(tunnel->t_key) >> shift;

	return (0);
}

static int
gre_del_vnetid(struct gre_tunnel *tunnel)
{
	tunnel->t_key_mask = GRE_KEY_NONE;

	return (0);
}

static int
gre_set_vnetflowid(struct gre_tunnel *tunnel, struct ifreq *ifr)
{
	uint32_t mask, key;

	if (tunnel->t_key_mask == GRE_KEY_NONE)
		return (EADDRNOTAVAIL);

	mask = ifr->ifr_vnetid ? GRE_KEY_ENTROPY : GRE_KEY_MASK;
	if (tunnel->t_key_mask == mask) {
		/* nop */
		return (0);
	}

	key = ntohl(tunnel->t_key);
	if (mask == GRE_KEY_ENTROPY) {
		if (key > GRE_KEY_ENTROPY_MAX)
			return (ERANGE);

		key = htonl(key << GRE_KEY_ENTROPY_SHIFT);
	} else
		key = htonl(key >> GRE_KEY_ENTROPY_SHIFT);

	/* commit */
	tunnel->t_key_mask = mask;
	tunnel->t_key = key;

	return (0);
}

static int
gre_get_vnetflowid(struct gre_tunnel *tunnel, struct ifreq *ifr)
{
	if (tunnel->t_key_mask == GRE_KEY_NONE)
		return (EADDRNOTAVAIL);

	ifr->ifr_vnetid = tunnel->t_key_mask == GRE_KEY_ENTROPY;

	return (0);
}

static int
mgre_up(struct mgre_softc *sc)
{
	unsigned int hlen;

	switch (sc->sc_tunnel.t_af) {
	case AF_UNSPEC:
		return (EDESTADDRREQ);
	case AF_INET:
		hlen = sizeof(struct ip);
		break;
#ifdef INET6
	case AF_INET6:
		hlen = sizeof(struct ip6_hdr);
		break;
#endif /* INET6 */
	default:
		unhandled_af(sc->sc_tunnel.t_af);
	}

	hlen += sizeof(struct gre_header);
	if (sc->sc_tunnel.t_key_mask != GRE_KEY_NONE)
		hlen += sizeof(struct gre_h_key);

	NET_ASSERT_LOCKED();

	if (RBT_INSERT(mgre_tree, &mgre_tree, sc) != NULL)
		return (EADDRINUSE);

	sc->sc_if.if_hdrlen = hlen;
	SET(sc->sc_if.if_flags, IFF_RUNNING);

	return (0);
}

static int
mgre_down(struct mgre_softc *sc)
{
	NET_ASSERT_LOCKED();

	CLR(sc->sc_if.if_flags, IFF_RUNNING);
	sc->sc_if.if_hdrlen = GRE_HDRLEN; /* symmetry */

	RBT_REMOVE(mgre_tree, &mgre_tree, sc);

	/* barrier? */

	return (0);
}

static int
egre_up(struct egre_softc *sc)
{
	if (sc->sc_tunnel.t_af == AF_UNSPEC)
		return (EDESTADDRREQ);

	NET_ASSERT_LOCKED();

	if (RBT_INSERT(egre_tree, &egre_tree, sc) != NULL)
		return (EADDRINUSE);

	SET(sc->sc_ac.ac_if.if_flags, IFF_RUNNING);

	return (0);
}

static int
egre_down(struct egre_softc *sc)
{
	NET_ASSERT_LOCKED();

	CLR(sc->sc_ac.ac_if.if_flags, IFF_RUNNING);

	RBT_REMOVE(egre_tree, &egre_tree, sc);

	/* barrier? */

	return (0);
}

static int
egre_media_change(struct ifnet *ifp)
{
	return (ENOTTY);
}

static void
egre_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	imr->ifm_active = IFM_ETHER | IFM_AUTO;
	imr->ifm_status = IFM_AVALID | IFM_ACTIVE;
}

static int
nvgre_up(struct nvgre_softc *sc)
{
	struct gre_tunnel *tunnel = &sc->sc_tunnel;
	struct ifnet *ifp0;
	void *inm;
	int error;

	if (tunnel->t_af == AF_UNSPEC)
		return (EDESTADDRREQ);

	ifp0 = if_get(sc->sc_ifp0);
	if (ifp0 == NULL)
		return (ENXIO);
	if (!ISSET(ifp0->if_flags, IFF_MULTICAST)) {
		error = ENODEV;
		goto put;
	}

	NET_ASSERT_LOCKED();

	if (RBT_INSERT(nvgre_mcast_tree, &nvgre_mcast_tree, sc) != NULL) {
		error = EADDRINUSE;
		goto put;
	}
	if (RBT_INSERT(nvgre_ucast_tree, &nvgre_ucast_tree, sc) != NULL) {
		error = EADDRINUSE;
		goto remove_mcast;
	}

	switch (tunnel->t_af) {
	case AF_INET:
		inm = in_addmulti(&tunnel->t_dst4, ifp0);
		if (inm == NULL) {
			error = ECONNABORTED;
			goto remove_ucast;
		}
		break;
#ifdef INET6
	case AF_INET6:
		inm = in6_addmulti(&tunnel->t_dst6, ifp0, &error);
		if (inm == NULL) {
			/* error is already set */
			goto remove_ucast;
		}
		break;
#endif /* INET6 */
	default:
		unhandled_af(tunnel->t_af);
	}

	if_linkstatehook_add(ifp0, &sc->sc_ltask);
	if_detachhook_add(ifp0, &sc->sc_dtask);

	if_put(ifp0);

	sc->sc_inm = inm;
	SET(sc->sc_ac.ac_if.if_flags, IFF_RUNNING);

	return (0);

remove_ucast:
	RBT_REMOVE(nvgre_ucast_tree, &nvgre_ucast_tree, sc);
remove_mcast:
	RBT_REMOVE(nvgre_mcast_tree, &nvgre_mcast_tree, sc);
put:
	if_put(ifp0);
	return (error);
}

static int
nvgre_down(struct nvgre_softc *sc)
{
	struct gre_tunnel *tunnel = &sc->sc_tunnel;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct taskq *softnet = net_tq(ifp->if_index);
	struct ifnet *ifp0;

	NET_ASSERT_LOCKED();

	CLR(ifp->if_flags, IFF_RUNNING);

	NET_UNLOCK();
	ifq_barrier(&ifp->if_snd);
	taskq_del_barrier(softnet, &sc->sc_send_task);
	NET_LOCK();

	mq_purge(&sc->sc_send_list);

	ifp0 = if_get(sc->sc_ifp0);
	if (ifp0 != NULL) {
		if_detachhook_del(ifp0, &sc->sc_dtask);
		if_linkstatehook_del(ifp0, &sc->sc_ltask);
	}
	if_put(ifp0);

	switch (tunnel->t_af) {
	case AF_INET:
		in_delmulti(sc->sc_inm);
		break;

#ifdef INET6
	case AF_INET6:
		in6_delmulti(sc->sc_inm);
		break;
#endif
	default:
		unhandled_af(tunnel->t_af);
	}

	RBT_REMOVE(nvgre_ucast_tree, &nvgre_ucast_tree, sc);
	RBT_REMOVE(nvgre_mcast_tree, &nvgre_mcast_tree, sc);

	return (0);
}

static void
nvgre_link_change(void *arg)
{
	/* nop */
}

static void
nvgre_detach(void *arg)
{
	struct nvgre_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		nvgre_down(sc);
		if_down(ifp);
	}

	sc->sc_ifp0 = 0;
}

static int
nvgre_set_parent(struct nvgre_softc *sc, const char *parent)
{
	struct ifnet *ifp0;

	ifp0 = if_unit(parent);
	if (ifp0 == NULL)
		return (EINVAL);

	if (!ISSET(ifp0->if_flags, IFF_MULTICAST)) {
		if_put(ifp0);
		return (EPROTONOSUPPORT);
	}

	ifsetlro(ifp0, 0);

	/* commit */
	sc->sc_ifp0 = ifp0->if_index;
	if_put(ifp0);

	return (0);
}

static int
nvgre_add_addr(struct nvgre_softc *sc, const struct ifbareq *ifba)
{
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct sockaddr_in6 src6 = {
		.sin6_len = sizeof(src6),
		.sin6_family = AF_UNSPEC,
	};
	int error;
#endif
	union gre_addr endpoint;
	unsigned int type;

	/* ignore ifba_ifsname */

	if (ISSET(ifba->ifba_flags, ~IFBAF_TYPEMASK))
		return (EINVAL);
	switch (ifba->ifba_flags & IFBAF_TYPEMASK) {
	case IFBAF_DYNAMIC:
		type = EBE_DYNAMIC;
		break;
	case IFBAF_STATIC:
		type = EBE_STATIC;
		break;
	default:
		return (EINVAL);
	}

	memset(&endpoint, 0, sizeof(endpoint));

	if (ifba->ifba_dstsa.ss_family != sc->sc_tunnel.t_af)
		return (EAFNOSUPPORT);
	switch (ifba->ifba_dstsa.ss_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)&ifba->ifba_dstsa;
		if (in_nullhost(sin->sin_addr) ||
		    IN_MULTICAST(sin->sin_addr.s_addr))
			return (EADDRNOTAVAIL);

		endpoint.in4 = sin->sin_addr;
		break;

#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&ifba->ifba_dstsa;
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			return (EADDRNOTAVAIL);

		in6_recoverscope(&src6, &sc->sc_tunnel.t_src6);

		if (src6.sin6_scope_id != sin6->sin6_scope_id)
			return (EADDRNOTAVAIL);

		error = in6_embedscope(&endpoint.in6, sin6, NULL, NULL);
		if (error != 0)
			return (error);

		break;
#endif
	default: /* AF_UNSPEC */
		return (EADDRNOTAVAIL);
	}

	return (etherbridge_add_addr(&sc->sc_eb, &endpoint,
	    &ifba->ifba_dst, type));
}

static int
nvgre_del_addr(struct nvgre_softc *sc, const struct ifbareq *ifba)
{
	return (etherbridge_del_addr(&sc->sc_eb, &ifba->ifba_dst));
}

static void
nvgre_start(struct ifnet *ifp)
{
	struct nvgre_softc *sc = ifp->if_softc;
	const struct gre_tunnel *tunnel = &sc->sc_tunnel;
	union gre_addr gateway;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ether_header *eh;
	struct mbuf *m, *m0;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	if (!atomic_load_int(&gre_allow)) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	while ((m0 = ifq_dequeue(&ifp->if_snd)) != NULL) {
#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_ether(if_bpf, m0, BPF_DIRECTION_OUT);
#endif

		eh = mtod(m0, struct ether_header *);
		if (ETHER_IS_BROADCAST(eh->ether_dhost))
			gateway = tunnel->t_dst;
		else {
			const union gre_addr *endpoint;

			smr_read_enter();
			endpoint = etherbridge_resolve_ea(&sc->sc_eb,
			    (struct ether_addr *)eh->ether_dhost);
			if (endpoint == NULL) {
				/* "flood" to unknown hosts */
				endpoint = &tunnel->t_dst;
			}
			gateway = *endpoint;
			smr_read_leave();
		}

		/* force prepend mbuf because of alignment problems */
		m = m_get(M_DONTWAIT, m0->m_type);
		if (m == NULL) {
			m_freem(m0);
			continue;
		}

		M_MOVE_PKTHDR(m, m0);
		m->m_next = m0;

		m_align(m, 0);
		m->m_len = 0;

		m = gre_encap_dst(tunnel, &gateway, m,
		    htons(ETHERTYPE_TRANSETHER),
		    tunnel->t_ttl, gre_l2_tos(tunnel, m));
		if (m == NULL)
			continue;

		m->m_flags &= ~(M_BCAST|M_MCAST);
		m->m_pkthdr.ph_rtableid = tunnel->t_rtableid;

#if NPF > 0
		pf_pkt_addr_changed(m);
#endif

		ml_enqueue(&ml, m);
	}

	if (!ml_empty(&ml)) {
		if (mq_enlist(&sc->sc_send_list, &ml) == 0)
			task_add(net_tq(ifp->if_index), &sc->sc_send_task);
		/* else set OACTIVE? */
	}
}

static uint64_t
nvgre_send4(struct nvgre_softc *sc, struct mbuf_list *ml)
{
	struct ip_moptions imo;
	struct mbuf *m;
	uint64_t oerrors = 0;

	imo.imo_ifidx = sc->sc_ifp0;
	imo.imo_ttl = sc->sc_tunnel.t_ttl;
	imo.imo_loop = 0;

	NET_LOCK();
	while ((m = ml_dequeue(ml)) != NULL) {
		if (ip_output(m, NULL, NULL, IP_RAWOUTPUT, &imo, NULL, 0) != 0)
			oerrors++;
	}
	NET_UNLOCK();

	return (oerrors);
}

#ifdef INET6
static uint64_t
nvgre_send6(struct nvgre_softc *sc, struct mbuf_list *ml)
{
	struct ip6_moptions im6o;
	struct mbuf *m;
	uint64_t oerrors = 0;

	im6o.im6o_ifidx = sc->sc_ifp0;
	im6o.im6o_hlim = sc->sc_tunnel.t_ttl;
	im6o.im6o_loop = 0;

	NET_LOCK();
	while ((m = ml_dequeue(ml)) != NULL) {
		if (ip6_output(m, NULL, NULL, 0, &im6o, NULL) != 0)
			oerrors++;
	}
	NET_UNLOCK();

	return (oerrors);
}
#endif /* INET6 */

static void
nvgre_send(void *arg)
{
	struct nvgre_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	sa_family_t af = sc->sc_tunnel.t_af;
	struct mbuf_list ml;
	uint64_t oerrors;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return;

	mq_delist(&sc->sc_send_list, &ml);
	if (ml_empty(&ml))
		return;

	switch (af) {
	case AF_INET:
		oerrors = nvgre_send4(sc, &ml);
		break;
#ifdef INET6
	case AF_INET6:
		oerrors = nvgre_send6(sc, &ml);
		break;
#endif
	default:
		unhandled_af(af);
		/* NOTREACHED */
	}

	ifp->if_oerrors += oerrors; /* XXX should be ifq_oerrors */
}

static int
eoip_up(struct eoip_softc *sc)
{
	if (sc->sc_tunnel.t_af == AF_UNSPEC)
		return (EDESTADDRREQ);

	NET_ASSERT_LOCKED();

	if (RBT_INSERT(eoip_tree, &eoip_tree, sc) != NULL)
		return (EADDRINUSE);

	SET(sc->sc_ac.ac_if.if_flags, IFF_RUNNING);

	if (sc->sc_ka_state != GRE_KA_NONE) {
		sc->sc_ka_holdmax = sc->sc_ka_count;
		eoip_keepalive_send(sc);
	}

	return (0);
}

static int
eoip_down(struct eoip_softc *sc)
{
	NET_ASSERT_LOCKED();
	CLR(sc->sc_ac.ac_if.if_flags, IFF_RUNNING);

	if (sc->sc_ka_state != GRE_KA_NONE) {
		timeout_del_barrier(&sc->sc_ka_hold);
		timeout_del_barrier(&sc->sc_ka_send);

		sc->sc_ka_state = GRE_KA_DOWN;
		gre_link_state(&sc->sc_ac.ac_if, sc->sc_ka_state);
	}

	RBT_REMOVE(eoip_tree, &eoip_tree, sc);

	return (0);
}

static void
eoip_start(struct ifnet *ifp)
{
	struct eoip_softc *sc = ifp->if_softc;
	struct mbuf *m0, *m;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	if (!atomic_load_int(&gre_allow)) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	while ((m0 = ifq_dequeue(&ifp->if_snd)) != NULL) {
#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_ether(if_bpf, m0, BPF_DIRECTION_OUT);
#endif

		/* force prepend mbuf because of alignment problems */
		m = m_get(M_DONTWAIT, m0->m_type);
		if (m == NULL) {
			m_freem(m0);
			continue;
		}

		M_MOVE_PKTHDR(m, m0);
		m->m_next = m0;

		m_align(m, 0);
		m->m_len = 0;

		m = eoip_encap(sc, m, gre_l2_tos(&sc->sc_tunnel, m));
		if (m == NULL || gre_ip_output(&sc->sc_tunnel, m) != 0) {
			ifp->if_oerrors++;
			continue;
		}
	}
}

static struct mbuf *
eoip_encap(struct eoip_softc *sc, struct mbuf *m, uint8_t tos)
{
	struct gre_header *gh;
	struct gre_h_key_eoip *eoiph;
	int len = m->m_pkthdr.len;

	m = m_prepend(m, sizeof(*gh) + sizeof(*eoiph), M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	gh = mtod(m, struct gre_header *);
	gh->gre_flags = htons(GRE_VERS_1 | GRE_KP);
	gh->gre_proto = htons(GRE_EOIP);

	eoiph = (struct gre_h_key_eoip *)(gh + 1);
	htobem16(&eoiph->eoip_len, len);
	eoiph->eoip_tunnel_id = sc->sc_tunnel_id;

	return (gre_encap_ip(&sc->sc_tunnel, m, sc->sc_tunnel.t_ttl, tos));
}

static void
eoip_keepalive_send(void *arg)
{
	struct eoip_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mbuf *m;
	int linkhdr;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return;

	/* this is really conservative */
#ifdef INET6
	linkhdr = max_linkhdr + MAX(sizeof(struct ip), sizeof(struct ip6_hdr)) +
	    sizeof(struct gre_header) + sizeof(struct gre_h_key_eoip);
#else
	linkhdr = max_linkhdr + sizeof(struct ip) +
	    sizeof(struct gre_header) + sizeof(struct gre_h_key_eoip);
#endif
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return;

	if (linkhdr > MHLEN) {
		MCLGETL(m, M_DONTWAIT, linkhdr);
		if (!ISSET(m->m_flags, M_EXT)) {
			m_freem(m);
			return;
		}
	}

	m->m_pkthdr.pf.prio = ifp->if_llprio;
	m->m_pkthdr.len = m->m_len = linkhdr;
	m_adj(m, linkhdr);

	m = eoip_encap(sc, m, gre_l2_tos(&sc->sc_tunnel, m));
	if (m == NULL)
		return;

	gre_ip_output(&sc->sc_tunnel, m);

	timeout_add_sec(&sc->sc_ka_send, sc->sc_ka_timeo);
}

static void
eoip_keepalive_hold(void *arg)
{
	struct eoip_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return;

	NET_LOCK();
	sc->sc_ka_state = GRE_KA_DOWN;
	gre_link_state(ifp, sc->sc_ka_state);
	NET_UNLOCK();
}

static void
eoip_keepalive_recv(struct eoip_softc *sc)
{
	switch (sc->sc_ka_state) {
	case GRE_KA_NONE:
		return;
	case GRE_KA_DOWN:
		sc->sc_ka_state = GRE_KA_HOLD;
		sc->sc_ka_holdcnt = sc->sc_ka_holdmax;
		sc->sc_ka_holdmax = MIN(sc->sc_ka_holdmax * 2,
		    16 * sc->sc_ka_count);
		break;
	case GRE_KA_HOLD:
		if (--sc->sc_ka_holdcnt > 0)
			break;

		sc->sc_ka_state = GRE_KA_UP;
		gre_link_state(&sc->sc_ac.ac_if, sc->sc_ka_state);
		break;

	case GRE_KA_UP:
		sc->sc_ka_holdmax--;
		sc->sc_ka_holdmax = MAX(sc->sc_ka_holdmax, sc->sc_ka_count);
		break;
	}

	timeout_add_sec(&sc->sc_ka_hold, sc->sc_ka_timeo * sc->sc_ka_count);
}

static struct mbuf *
eoip_input(struct gre_tunnel *key, struct mbuf *m,
    const struct gre_header *gh, uint8_t otos, int iphlen, struct netstack *ns)
{
	struct eoip_softc *sc;
	struct gre_h_key_eoip *eoiph;
	int hlen, len;
	caddr_t buf;

	if (gh->gre_flags != htons(GRE_KP | GRE_VERS_1))
		goto decline;

	hlen = iphlen + sizeof(*gh) + sizeof(*eoiph);
	if (m->m_pkthdr.len < hlen)
		goto decline;

	m = m_pullup(m, hlen);
	if (m == NULL)
		return (NULL);

	buf = mtod(m, caddr_t);
	gh = (struct gre_header *)(buf + iphlen);
	eoiph = (struct gre_h_key_eoip *)(gh + 1);

	key->t_key = eoiph->eoip_tunnel_id;

	NET_ASSERT_LOCKED();
	sc = RBT_FIND(eoip_tree, &eoip_tree, (const struct eoip_softc *)key);
	if (sc == NULL)
		goto decline;

	/* it's ours now */
	len = bemtoh16(&eoiph->eoip_len);
	if (len == 0) {
		eoip_keepalive_recv(sc);
		goto drop;
	}

	m = gre_ether_align(m, hlen);
	if (m == NULL)
		return (NULL);

	if (m->m_pkthdr.len < len)
		goto drop;
	if (m->m_pkthdr.len != len)
		m_adj(m, len - m->m_pkthdr.len);

	m->m_flags &= ~(M_MCAST|M_BCAST);

	gre_l2_prio(&sc->sc_tunnel, m, otos);

	if_vinput(&sc->sc_ac.ac_if, m, ns);

	return (NULL);

decline:
	return (m);
drop:
	m_freem(m);
	return (NULL);
}

const struct sysctl_bounded_args gre_vars[] = {
	{ GRECTL_ALLOW, &gre_allow, 0, 1 },
	{ GRECTL_WCCP, &gre_wccp, 0, 1 },
};

int
gre_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	return sysctl_bounded_arr(gre_vars, nitems(gre_vars), name,
	    namelen, oldp, oldlenp, newp, newlen);
}

static inline int
gre_ip_cmp(int af, const union gre_addr *a, const union gre_addr *b)
{
	switch (af) {
#ifdef INET6
	case AF_INET6:
		return (memcmp(&a->in6, &b->in6, sizeof(a->in6)));
#endif /* INET6 */
	case AF_INET:
		return (memcmp(&a->in4, &b->in4, sizeof(a->in4)));
	default:
		unhandled_af(af);
	}

	return (0);
}

static int
gre_cmp_src(const struct gre_tunnel *a, const struct gre_tunnel *b)
{
	uint32_t ka, kb;
	uint32_t mask;
	int rv;

	/* is K set at all? */
	ka = a->t_key_mask & GRE_KEY_ENTROPY;
	kb = b->t_key_mask & GRE_KEY_ENTROPY;

	/* sort by whether K is set */
	if (ka > kb)
		return (1);
	if (ka < kb)
		return (-1);

	/* is K set on both? */
	if (ka != GRE_KEY_NONE) {
		/* get common prefix */
		mask = a->t_key_mask & b->t_key_mask;

		ka = a->t_key & mask;
		kb = b->t_key & mask;

		/* sort by common prefix */
		if (ka > kb)
			return (1);
		if (ka < kb)
			return (-1);
	}

	/* sort by routing table */
	if (a->t_rtableid > b->t_rtableid)
		return (1);
	if (a->t_rtableid < b->t_rtableid)
		return (-1);

	/* sort by address */
	if (a->t_af > b->t_af)
		return (1);
	if (a->t_af < b->t_af)
		return (-1);

	rv = gre_ip_cmp(a->t_af, &a->t_src, &b->t_src);
	if (rv != 0)
		return (rv);

	return (0);
}

static int
gre_cmp(const struct gre_tunnel *a, const struct gre_tunnel *b)
{
	int rv;

	rv = gre_cmp_src(a, b);
	if (rv != 0)
		return (rv);

	return (gre_ip_cmp(a->t_af, &a->t_dst, &b->t_dst));
}

static inline int
mgre_cmp(const struct mgre_softc *a, const struct mgre_softc *b)
{
	return (gre_cmp_src(&a->sc_tunnel, &b->sc_tunnel));
}

RBT_GENERATE(mgre_tree, mgre_softc, sc_entry, mgre_cmp);

static inline int
egre_cmp(const struct egre_softc *a, const struct egre_softc *b)
{
	return (gre_cmp(&a->sc_tunnel, &b->sc_tunnel));
}

RBT_GENERATE(egre_tree, egre_softc, sc_entry, egre_cmp);

static int
nvgre_cmp_tunnel(const struct gre_tunnel *a, const struct gre_tunnel *b)
{
	uint32_t ka, kb;

	ka = a->t_key & GRE_KEY_ENTROPY;
	kb = b->t_key & GRE_KEY_ENTROPY;

	/* sort by common prefix */
	if (ka > kb)
		return (1);
	if (ka < kb)
		return (-1);

	/* sort by routing table */
	if (a->t_rtableid > b->t_rtableid)
		return (1);
	if (a->t_rtableid < b->t_rtableid)
		return (-1);

	/* sort by address */
	if (a->t_af > b->t_af)
		return (1);
	if (a->t_af < b->t_af)
		return (-1);

	return (0);
}

static inline int
nvgre_cmp_ucast(const struct nvgre_softc *na, const struct nvgre_softc *nb)
{
	const struct gre_tunnel *a = &na->sc_tunnel;
	const struct gre_tunnel *b = &nb->sc_tunnel;
	int rv;

	rv = nvgre_cmp_tunnel(a, b);
	if (rv != 0)
		return (rv);

	rv = gre_ip_cmp(a->t_af, &a->t_src, &b->t_src);
	if (rv != 0)
		return (rv);

	return (0);
}

static int
nvgre_cmp_mcast(const struct gre_tunnel *a, const union gre_addr *aa,
    unsigned int if0idxa, const struct gre_tunnel *b,
    const union gre_addr *ab,unsigned int if0idxb)
{
	int rv;

	rv = nvgre_cmp_tunnel(a, b);
	if (rv != 0)
		return (rv);

	rv = gre_ip_cmp(a->t_af, aa, ab);
	if (rv != 0)
		return (rv);

	if (if0idxa > if0idxb)
		return (1);
	if (if0idxa < if0idxb)
		return (-1);

	return (0);
}

static inline int
nvgre_cmp_mcast_sc(const struct nvgre_softc *na, const struct nvgre_softc *nb)
{
	const struct gre_tunnel *a = &na->sc_tunnel;
	const struct gre_tunnel *b = &nb->sc_tunnel;

	return (nvgre_cmp_mcast(a, &a->t_dst, na->sc_ifp0,
	    b, &b->t_dst, nb->sc_ifp0));
}

RBT_GENERATE(nvgre_ucast_tree, nvgre_softc, sc_uentry, nvgre_cmp_ucast);
RBT_GENERATE(nvgre_mcast_tree, nvgre_softc, sc_mentry, nvgre_cmp_mcast_sc);

static inline int
gre_tunnel_key_cmp(const struct gre_tunnel *a, const struct gre_tunnel *b)
{
	int rv;

	if (a->t_key > b->t_key)
		return (1);
	if (a->t_key < b->t_key)
		return (-1);

	/* sort by routing table */
	if (a->t_rtableid > b->t_rtableid)
		return (1);
	if (a->t_rtableid < b->t_rtableid)
		return (-1);

	/* sort by address */
	if (a->t_af > b->t_af)
		return (1);
	if (a->t_af < b->t_af)
		return (-1);

	rv = gre_ip_cmp(a->t_af, &a->t_src, &b->t_src);
	if (rv != 0)
		return (rv);

	rv = gre_ip_cmp(a->t_af, &a->t_dst, &b->t_dst);
	if (rv != 0)
		return (rv);

	return (0);

}

static inline int
eoip_cmp(const struct eoip_softc *ea, const struct eoip_softc *eb)
{
	return (gre_tunnel_key_cmp(&ea->sc_tunnel, &eb->sc_tunnel));
}

RBT_GENERATE(eoip_tree, eoip_softc, sc_entry, eoip_cmp);

static int
nvgre_eb_port_eq(void *arg, void *a, void *b)
{
	struct nvgre_softc *sc = arg;

	return (gre_ip_cmp(sc->sc_tunnel.t_af, a, b) == 0);
}

static void *
nvgre_eb_port_take(void *arg, void *port)
{
	union gre_addr *ea = port;
	union gre_addr *endpoint;

	endpoint = pool_get(&nvgre_endpoint_pool, PR_NOWAIT);
	if (endpoint == NULL)
		return (NULL);

	*endpoint = *ea;

	return (endpoint);
}

static void
nvgre_eb_port_rele(void *arg, void *port)
{
	union gre_addr *endpoint = port;

	pool_put(&nvgre_endpoint_pool, endpoint);
}

static size_t
nvgre_eb_port_ifname(void *arg, char *dst, size_t len, void *port)
{
	struct nvgre_softc *sc = arg;

	return (strlcpy(dst, sc->sc_ac.ac_if.if_xname, len));
}

static void
nvgre_eb_port_sa(void *arg, struct sockaddr_storage *ss, void *port)
{
	struct nvgre_softc *sc = arg;
	union gre_addr *endpoint = port;

	switch (sc->sc_tunnel.t_af) {
	case AF_INET: {
		struct sockaddr_in *sin = (struct sockaddr_in *)ss;

		sin->sin_len = sizeof(*sin);
		sin->sin_family = AF_INET;
		sin->sin_addr = endpoint->in4;
		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;

		sin6->sin6_len = sizeof(*sin6);
		sin6->sin6_family = AF_INET6;
		in6_recoverscope(sin6, &endpoint->in6);

		break;
	}
#endif /* INET6 */
	default:
		unhandled_af(sc->sc_tunnel.t_af);
	}
}

/*
 * ERSPAN
 */

static int
erspan_clone_create(struct if_clone *ifc, int unit)
{
	struct erspan_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = &sc->sc_ac.ac_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	ifp->if_softc = sc;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_ioctl = erspan_ioctl;
	ifp->if_start = erspan_start;
	ifp->if_xflags = IFXF_CLONED | IFXF_MONITOR;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
#if 0 && NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif
	ether_fakeaddr(ifp);

	sc->sc_tunnel.t_key = ~0;
	sc->sc_tunnel.t_ttl = atomic_load_int(&ip_defttl);
	sc->sc_tunnel.t_txhprio = IF_HDRPRIO_PACKET; /* XXX */
	sc->sc_tunnel.t_rxhprio = IF_HDRPRIO_PAYLOAD;
	sc->sc_tunnel.t_df = htons(0);

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

#if NBPFILTER > 0
	/* attach after Ethernet */
	bpfattach(&sc->sc_bpf, ifp, DLT_LOOP, sizeof(uint32_t));
	bpfattach(&sc->sc_bpf_raw, ifp, DLT_RAW, 0);
#endif

	return (0);
}

static int
erspan_clone_destroy(struct ifnet *ifp)
{
	struct erspan_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		erspan_down(sc);
	NET_UNLOCK();

	ether_ifdetach(ifp);
	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
erspan_set_tunnel(struct erspan_softc *sc, struct if_laddrreq *req)
{
	struct gre_tunnel *tunnel = &sc->sc_tunnel;
	struct sockaddr *addr = (struct sockaddr *)&req->addr;
	struct sockaddr *dstaddr = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *src4;
#ifdef INET6
	struct sockaddr_in6 *src6;
	int error;
#endif
	uint32_t mask = 0;

	/* validate */
	switch (addr->sa_family) {
	case AF_INET:
		if (addr->sa_len != sizeof(*src4))
			return (EINVAL);

		src4 = (struct sockaddr_in *)addr;
		if (in_nullhost(src4->sin_addr) ||
		    IN_MULTICAST(src4->sin_addr.s_addr))
			return (EINVAL);

		if (dstaddr->sa_family == AF_UNSPEC)
			tunnel->t_dst4.s_addr = INADDR_ANY;
		else if (dstaddr->sa_family != AF_INET)
			return (EINVAL);
		else {
			struct sockaddr_in *daddr4 = satosin(dstaddr);
			if (in_nullhost(daddr4->sin_addr) ||
			    IN_MULTICAST(daddr4->sin_addr.s_addr))
				return (EINVAL);
			
			tunnel->t_dst4 = daddr4->sin_addr;
			mask = 1;
		}
		tunnel->t_src4 = src4->sin_addr;

		break;
#ifdef INET6
	case AF_INET6:
		if (addr->sa_len != sizeof(*src6))
			return (EINVAL);

		src6 = (struct sockaddr_in6 *)addr;
		if (IN6_IS_ADDR_UNSPECIFIED(&src6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&src6->sin6_addr))
			return (EINVAL);

		error = in6_embedscope(&tunnel->t_src6, src6, NULL, NULL);
		if (error != 0)
			return (error);

		if (dstaddr->sa_family == AF_UNSPEC)
			memset(&tunnel->t_dst6, 0, sizeof(tunnel->t_dst6));
		else if (dstaddr->sa_family != AF_INET6)
			return (EINVAL);
		else {
			struct sockaddr_in6 *dst6 = satosin6(dstaddr);
			if (IN6_IS_ADDR_UNSPECIFIED(&dst6->sin6_addr) ||
			    IN6_IS_ADDR_MULTICAST(&dst6->sin6_addr))
				return (EINVAL);

			if (src6->sin6_scope_id != dst6->sin6_scope_id)
				return (EINVAL);

			error = in6_embedscope(&tunnel->t_dst6, dst6,
			    NULL, NULL);
			if (error != 0)
				return (error);
			mask = 1;
		}

		error = in6_embedscope(&tunnel->t_src6, src6, NULL, NULL);
		if (error != 0)
			return (error);

		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}

	/* commit */
	tunnel->t_af = addr->sa_family;
	tunnel->t_key_mask = mask; /* set if dstaddr set */

	return (0);
}

static int
erspan_get_tunnel(struct erspan_softc *sc, struct if_laddrreq *req)
{
	struct gre_tunnel *tunnel = &sc->sc_tunnel;
	struct sockaddr *dstaddr = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif

	switch (tunnel->t_af) {
	case AF_UNSPEC:
		return (EADDRNOTAVAIL);
	case AF_INET:
		sin = (struct sockaddr_in *)&req->addr;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = tunnel->t_src4;

		if (!tunnel->t_key_mask)
			goto unspec;

		sin = (struct sockaddr_in *)dstaddr;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = tunnel->t_dst4;
		break;

#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&req->addr;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(*sin6);
		in6_recoverscope(sin6, &tunnel->t_src6);

		if (!tunnel->t_key_mask)
			goto unspec;

		sin6 = (struct sockaddr_in6 *)dstaddr;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(*sin6);
		in6_recoverscope(sin6, &tunnel->t_dst6);
		break;
#endif
	default:
		unhandled_af(tunnel->t_af);
	}

	return (0);

unspec:
	dstaddr->sa_len = 2;
	dstaddr->sa_family = AF_UNSPEC;

	return (0);
}

static int
erspan_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct erspan_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch(cmd) {
	case SIOCSIFADDR:
		break;
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = erspan_up(sc);
			else
				error = 0;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = erspan_down(sc);
		}
		break;

	case SIOCSVNETID:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}
		if (ifr->ifr_vnetid < 0 ||
		    ifr->ifr_vnetid > ERSPAN_II_SESSION_ID_MASK)
			return (EINVAL);

		sc->sc_tunnel.t_key = ifr->ifr_vnetid; /* for cmp */
		break;
	case SIOCGVNETID:
		if (sc->sc_tunnel.t_key == ~0)
			return (EADDRNOTAVAIL);
		ifr->ifr_vnetid = sc->sc_tunnel.t_key;
		break;
	case SIOCDVNETID:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}
		sc->sc_tunnel.t_key = ~0;
		break;

	case SIOCSLIFPHYADDR:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}

		error = erspan_set_tunnel(sc, (struct if_laddrreq *)data);
		break;
	case SIOCGLIFPHYADDR:
		error = erspan_get_tunnel(sc, (struct if_laddrreq *)data);
		break;
	case SIOCDIFPHYADDR:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}

		/* commit */
		sc->sc_tunnel.t_af = AF_UNSPEC;
		sc->sc_tunnel.t_key_mask = 0; /* dstaddr is not set */
		break;

	case SIOCSLIFPHYRTABLE:
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			error = EBUSY;
			break;
		}

		if (ifr->ifr_rdomainid < 0 ||
		    ifr->ifr_rdomainid > RT_TABLEID_MAX ||
		    !rtable_exists(ifr->ifr_rdomainid)) {
			error = EINVAL;
			break;
		}
		sc->sc_tunnel.t_rtableid = ifr->ifr_rdomainid;
		break;
	case SIOCGLIFPHYRTABLE:
		ifr->ifr_rdomainid = sc->sc_tunnel.t_rtableid;
		break;

	case SIOCSLIFPHYTTL:
		if (ifr->ifr_ttl < 1 || ifr->ifr_ttl > 0xff) {
			error = EINVAL;
			break;
		}

		/* commit */
		sc->sc_tunnel.t_ttl = (uint8_t)ifr->ifr_ttl;
		break;
	case SIOCGLIFPHYTTL:
		ifr->ifr_ttl = (int)sc->sc_tunnel.t_ttl;
		break;

	case SIOCSLIFPHYDF:
		/* commit */
		sc->sc_tunnel.t_df = ifr->ifr_df ? htons(IP_DF) : htons(0);
		break;
	case SIOCGLIFPHYDF:
		ifr->ifr_df = sc->sc_tunnel.t_df ? 1 : 0;
		break;

	case SIOCSTXHPRIO:
		error = if_txhprio_l3_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_txhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGTXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_txhprio;
		break;

	case SIOCSRXHPRIO:
		error = if_rxhprio_l3_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_tunnel.t_rxhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGRXHPRIO:
		ifr->ifr_hdrprio = sc->sc_tunnel.t_rxhprio;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		/* no hardware to program */
		error = 0;
	}

	return (error);
}

static int
erspan_up(struct erspan_softc *sc)
{
	struct gre_tunnel *tunnel = &sc->sc_tunnel;

	if (tunnel->t_af == AF_UNSPEC)
		return (EDESTADDRREQ);
	if (tunnel->t_key == ~0 && tunnel->t_key_mask) {
		/* wildcard session id and t_dst is not set */
		return (EDESTADDRREQ);
	}

	NET_ASSERT_LOCKED();

	if (RBT_INSERT(erspan_tree, &erspan_tree, sc) != NULL)
		return (EADDRINUSE);

	SET(sc->sc_ac.ac_if.if_flags, IFF_RUNNING);

	return (0);
}

static int
erspan_down(struct erspan_softc *sc)
{
	NET_ASSERT_LOCKED();
	CLR(sc->sc_ac.ac_if.if_flags, IFF_RUNNING);

	RBT_REMOVE(erspan_tree, &erspan_tree, sc);

	return (0);
}

static void
erspan_start(struct ifnet *ifp)
{
	struct erspan_softc *sc = ifp->if_softc;
	struct mbuf *m0, *m;
	uint32_t session_id = sc->sc_tunnel.t_key;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	if (!atomic_load_int(&gre_allow) ||
	    !sc->sc_tunnel.t_key_mask || /* dstaddr is not set */
	    session_id == ~0) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	while ((m0 = ifq_dequeue(&ifp->if_snd)) != NULL) {
#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_ether(if_bpf, m0, BPF_DIRECTION_OUT);
#endif

		/* force prepend mbuf because of alignment problems */
		m = m_get(M_DONTWAIT, m0->m_type);
		if (m == NULL) {
			m_freem(m0);
			continue;
		}

		M_MOVE_PKTHDR(m, m0);
		m->m_next = m0;

		m_align(m, 0);
		m->m_len = 0;

		m = erspan_encap(sc, m, gre_l2_tos(&sc->sc_tunnel, m),
		    session_id);
		if (m == NULL) {
			ifp->if_oerrors++;
			continue;
		}
#if NBPFILTER > 0
		if_bpf = sc->sc_bpf;
		if (if_bpf) {
			bpf_mtap_af(if_bpf, sc->sc_tunnel.t_af, m,
			    BPF_DIRECTION_OUT);
		}
		if_bpf = sc->sc_bpf_raw;
		if (if_bpf)
			bpf_mtap(if_bpf, m, BPF_DIRECTION_OUT);
#endif
		if (gre_ip_output(&sc->sc_tunnel, m) != 0) {
			ifp->if_oerrors++;
			continue;
		}
	}
}

static struct mbuf *
erspan_encap(struct erspan_softc *sc, struct mbuf *m, uint8_t tos,
    uint32_t session_id)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct gre_header *gh;
	struct gre_h_seq *seqh;
	struct gre_h_erspan *erspanh;
	uint32_t hdr;

	m = m_prepend(m, sizeof(*gh) + sizeof(*seqh) + sizeof(*erspanh),
	    M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	gh = mtod(m, struct gre_header *);
	gh->gre_flags = htons(GRE_VERS_0 | GRE_SP);
	gh->gre_proto = htons(GRE_ERSPAN);

	seqh = (struct gre_h_seq *)(gh + 1);
	htobem32(&seqh->gre_seq, sc->sc_seq++);

	hdr = session_id << ERSPAN_II_SESSION_ID_SHIFT;
	hdr |= m->m_pkthdr.pf.prio << ERSPAN_II_COS_SHIFT;
#if 0 && NVLAN > 0
	if (ISSET(m->m_flags, M_VLANTAG)) {
		hdr |= ERSPAN_II_EN_VLAN << ERSPAN_II_EN_SHIFT;
		hdr |= (m->m_pkthdr.ether_vlan & ERSPAN_II_VLAN_MASK) <<
		    ERSPAN_II_VLAN_SHIFT;
		CLR(m->m_flags, M_VLANTAG);
	} /* else?? */
#endif
	hdr |= ERSPAN_II_VER << ERSPAN_II_VER_SHIFT;

	erspanh = (struct gre_h_erspan *)(seqh + 1);
	htobem32(&erspanh->hdr, hdr);
	htobem32(&erspanh->index, ISSET(ifp->if_flags, IFF_LINK0) ?
	    m->m_pkthdr.ph_ifidx : 0);

	return (gre_encap_ip(&sc->sc_tunnel, m, sc->sc_tunnel.t_ttl, tos));
}

static struct mbuf *
erspan_input(struct gre_tunnel *key, struct mbuf *m, int iphlen,
    const struct gre_header *gh, uint8_t otos, struct netstack *ns)
{
	struct erspan_softc *sc;
	struct ifnet *ifp;
	struct gre_h_seq *seqh;
	struct gre_h_erspan *erspanh;
	uint32_t hdr;
	int hlen;
	caddr_t buf;
	int input = 1;
	int rxprio;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	/* ERSPAN Type II */
	if (gh->gre_flags != htons(GRE_SP | GRE_VERS_0))
		goto decline;

	hlen = iphlen + sizeof(*gh) + sizeof(*seqh) + sizeof(*erspanh);
	if (m->m_pkthdr.len < hlen)
		goto decline;

	m = m_pullup(m, hlen);
	if (m == NULL)
		return (NULL);

	buf = mtod(m, caddr_t);
	gh = (struct gre_header *)(buf + iphlen);
	seqh = (struct gre_h_seq *)(gh + 1);
	erspanh = (struct gre_h_erspan *)(seqh + 1);

	hdr = bemtoh32(&erspanh->hdr);

	key->t_key = (hdr >> ERSPAN_II_SESSION_ID_SHIFT) &
	    ERSPAN_II_SESSION_ID_MASK;

	NET_ASSERT_LOCKED();
	sc = RBT_FIND(erspan_tree, &erspan_tree,
	    (const struct erspan_softc *)key);
	if (sc == NULL) {
		/* try for a wildcard listener */
		struct gre_tunnel wkey = {
			.t_af = key->t_af,
			.t_rtableid = key->t_rtableid,
			.t_src = key->t_src,
			.t_key = key->t_key,
		};

		input = 0;
		sc = RBT_FIND(erspan_tree, &erspan_tree,
		    (const struct erspan_softc *)&wkey);
		if (sc == NULL) {
			/* last resort is a wildcard listener without a key */
			wkey.t_key = ~0;
			sc = RBT_FIND(erspan_tree, &erspan_tree,
			    (const struct erspan_softc *)&wkey);
			if (sc == NULL) {
				goto decline;
			}
		}
	}

	/* it's ours now */
	ifp = &sc->sc_ac.ac_if;

#if NBPFILTER > 0
	if_bpf = sc->sc_bpf;
	if (if_bpf) {
		if (bpf_mtap_af(if_bpf, key->t_af, m, BPF_DIRECTION_IN))
			input = 0;
	}
	if_bpf = sc->sc_bpf_raw;
	if (if_bpf) {
		if (bpf_mtap(if_bpf, m, BPF_DIRECTION_IN))
			input = 0;
	}
#endif

#if 0
	/*
	 * this appears to be metadata from the switch rather than
	 * an offload for the payload.
	 */
	switch ((hdr >> ERSPAN_II_EN_SHIFT) & ERSPAN_II_EN_MASK) {
	case ERSPAN_II_EN_ISL: /* this is cheeky */
	case ERSPAN_II_EN_VLAN:
#if NVLAN > 0
		m->m_pkthdr.ether_vtag = (hdr >> ERSPAN_II_VLAN_SHIFT) &
		    ERSPAN_II_VLAN_MASK;
		m->m_pkthdr.ether_vtag |= ((hdr >> ERSPAN_II_COS_SHIFT) &
		    ERSPAN_II_COS_MASK) << 13;
                m->m_flags |= M_VLANTAG;
#else
		input = 0;
#endif
		break;
	default:
		break;
	}
#endif

	rxprio = sc->sc_tunnel.t_rxhprio;
	switch (rxprio) {
	case IF_HDRPRIO_PACKET:
		/* nop */
		break;
	case IF_HDRPRIO_OUTER:
		m->m_pkthdr.pf.prio = IFQ_TOS2PRIO(otos);
		break;
	case IF_HDRPRIO_PAYLOAD:
		m->m_pkthdr.pf.prio = (hdr >> ERSPAN_II_COS_SHIFT) &
		    ERSPAN_II_COS_MASK;
		break;
	default:
		m->m_pkthdr.pf.prio = rxprio;
		break;
	}

	if (hdr & ERSPAN_II_T)
		input = 0;

	if (input) {
		m = gre_ether_align(m, hlen);
		if (m == NULL)
			return (NULL);

		CLR(m->m_flags, M_MCAST|M_BCAST);

		if_vinput(&sc->sc_ac.ac_if, m, ns);
	} else {
		counters_pkt(ifp->if_counters, ifc_ipackets, ifc_ibytes,
		    m->m_pkthdr.len - hlen);
#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf) {
			m_adj(m, hlen);
			bpf_mtap_ether(if_bpf, m, BPF_DIRECTION_IN);
		}
#endif

		goto drop;
	}

	return (NULL);

decline:
	return (m);
drop:
	m_freem(m);
	return (NULL);
}

static inline int
erspan_cmp(const struct erspan_softc *ea, const struct erspan_softc *eb)
{
	return (gre_tunnel_key_cmp(&ea->sc_tunnel, &eb->sc_tunnel));
}

RBT_GENERATE(erspan_tree, erspan_softc, sc_entry, erspan_cmp);

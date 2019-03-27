/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Michael Shalayeff.
 * Copyright (c) 2003 Ryan McBride.
 * Copyright (c) 2011 Gleb Smirnoff <glebius@FreeBSD.org>
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bpf.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/taskqueue.h>
#include <sys/counter.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_llatbl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/vnet.h>

#if defined(INET) || defined(INET6)
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip_carp.h>
#include <netinet/ip.h>
#include <machine/in_cksum.h>
#endif
#ifdef INET
#include <netinet/ip_var.h>
#include <netinet/if_ether.h>
#endif

#ifdef INET6
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>
#include <netinet6/nd6.h>
#endif

#include <crypto/sha1.h>

static MALLOC_DEFINE(M_CARP, "CARP", "CARP addresses");

struct carp_softc {
	struct ifnet		*sc_carpdev;	/* Pointer to parent ifnet. */
	struct ifaddr		**sc_ifas;	/* Our ifaddrs. */
	struct sockaddr_dl	sc_addr;	/* Our link level address. */
	struct callout		sc_ad_tmo;	/* Advertising timeout. */
#ifdef INET
	struct callout		sc_md_tmo;	/* Master down timeout. */
#endif
#ifdef INET6
	struct callout 		sc_md6_tmo;	/* XXX: Master down timeout. */
#endif
	struct mtx		sc_mtx;

	int			sc_vhid;
	int			sc_advskew;
	int			sc_advbase;

	int			sc_naddrs;
	int			sc_naddrs6;
	int			sc_ifasiz;
	enum { INIT = 0, BACKUP, MASTER }	sc_state;
	int			sc_suppress;
	int			sc_sendad_errors;
#define	CARP_SENDAD_MAX_ERRORS	3
	int			sc_sendad_success;
#define	CARP_SENDAD_MIN_SUCCESS 3

	int			sc_init_counter;
	uint64_t		sc_counter;

	/* authentication */
#define	CARP_HMAC_PAD	64
	unsigned char sc_key[CARP_KEY_LEN];
	unsigned char sc_pad[CARP_HMAC_PAD];
	SHA1_CTX sc_sha1;

	TAILQ_ENTRY(carp_softc)	sc_list;	/* On the carp_if list. */
	LIST_ENTRY(carp_softc)	sc_next;	/* On the global list. */
};

struct carp_if {
#ifdef INET
	int	cif_naddrs;
#endif
#ifdef INET6
	int	cif_naddrs6;
#endif
	TAILQ_HEAD(, carp_softc) cif_vrs;
#ifdef INET
	struct ip_moptions 	 cif_imo;
#endif
#ifdef INET6
	struct ip6_moptions 	 cif_im6o;
#endif
	struct ifnet	*cif_ifp;
	struct mtx	cif_mtx;
	uint32_t	cif_flags;
#define	CIF_PROMISC	0x00000001
};

#define	CARP_INET	0
#define	CARP_INET6	1
static int proto_reg[] = {-1, -1};

/*
 * Brief design of carp(4).
 *
 * Any carp-capable ifnet may have a list of carp softcs hanging off
 * its ifp->if_carp pointer. Each softc represents one unique virtual
 * host id, or vhid. The softc has a back pointer to the ifnet. All
 * softcs are joined in a global list, which has quite limited use.
 *
 * Any interface address that takes part in CARP negotiation has a
 * pointer to the softc of its vhid, ifa->ifa_carp. That could be either
 * AF_INET or AF_INET6 address.
 *
 * Although, one can get the softc's backpointer to ifnet and traverse
 * through its ifp->if_addrhead queue to find all interface addresses
 * involved in CARP, we keep a growable array of ifaddr pointers. This
 * allows us to avoid grabbing the IF_ADDR_LOCK() in many traversals that
 * do calls into the network stack, thus avoiding LORs.
 *
 * Locking:
 *
 * Each softc has a lock sc_mtx. It is used to synchronise carp_input_c(),
 * callout-driven events and ioctl()s.
 *
 * To traverse the list of softcs on an ifnet we use CIF_LOCK() or carp_sx.
 * To traverse the global list we use the mutex carp_mtx.
 *
 * Known issues with locking:
 *
 * - Sending ad, we put the pointer to the softc in an mtag, and no reference
 *   counting is done on the softc.
 * - On module unload we may race (?) with packet processing thread
 *   dereferencing our function pointers.
 */

/* Accept incoming CARP packets. */
VNET_DEFINE_STATIC(int, carp_allow) = 1;
#define	V_carp_allow	VNET(carp_allow)

/* Set DSCP in outgoing CARP packets. */
VNET_DEFINE_STATIC(int, carp_dscp) = 56;
#define	V_carp_dscp	VNET(carp_dscp)

/* Preempt slower nodes. */
VNET_DEFINE_STATIC(int, carp_preempt) = 0;
#define	V_carp_preempt	VNET(carp_preempt)

/* Log level. */
VNET_DEFINE_STATIC(int, carp_log) = 1;
#define	V_carp_log	VNET(carp_log)

/* Global advskew demotion. */
VNET_DEFINE_STATIC(int, carp_demotion) = 0;
#define	V_carp_demotion	VNET(carp_demotion)

/* Send error demotion factor. */
VNET_DEFINE_STATIC(int, carp_senderr_adj) = CARP_MAXSKEW;
#define	V_carp_senderr_adj	VNET(carp_senderr_adj)

/* Iface down demotion factor. */
VNET_DEFINE_STATIC(int, carp_ifdown_adj) = CARP_MAXSKEW;
#define	V_carp_ifdown_adj	VNET(carp_ifdown_adj)

static int carp_allow_sysctl(SYSCTL_HANDLER_ARGS);
static int carp_dscp_sysctl(SYSCTL_HANDLER_ARGS);
static int carp_demote_adj_sysctl(SYSCTL_HANDLER_ARGS);

SYSCTL_NODE(_net_inet, IPPROTO_CARP,	carp,	CTLFLAG_RW, 0,	"CARP");
SYSCTL_PROC(_net_inet_carp, OID_AUTO, allow,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW, 0, 0, carp_allow_sysctl, "I",
    "Accept incoming CARP packets");
SYSCTL_PROC(_net_inet_carp, OID_AUTO, dscp,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW, 0, 0, carp_dscp_sysctl, "I",
    "DSCP value for carp packets");
SYSCTL_INT(_net_inet_carp, OID_AUTO, preempt, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(carp_preempt), 0, "High-priority backup preemption mode");
SYSCTL_INT(_net_inet_carp, OID_AUTO, log, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(carp_log), 0, "CARP log level");
SYSCTL_PROC(_net_inet_carp, OID_AUTO, demotion,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW,
    0, 0, carp_demote_adj_sysctl, "I",
    "Adjust demotion factor (skew of advskew)");
SYSCTL_INT(_net_inet_carp, OID_AUTO, senderr_demotion_factor,
    CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(carp_senderr_adj), 0, "Send error demotion factor adjustment");
SYSCTL_INT(_net_inet_carp, OID_AUTO, ifdown_demotion_factor,
    CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(carp_ifdown_adj), 0,
    "Interface down demotion factor adjustment");

VNET_PCPUSTAT_DEFINE(struct carpstats, carpstats);
VNET_PCPUSTAT_SYSINIT(carpstats);
VNET_PCPUSTAT_SYSUNINIT(carpstats);

#define	CARPSTATS_ADD(name, val)	\
    counter_u64_add(VNET(carpstats)[offsetof(struct carpstats, name) / \
	sizeof(uint64_t)], (val))
#define	CARPSTATS_INC(name)		CARPSTATS_ADD(name, 1)

SYSCTL_VNET_PCPUSTAT(_net_inet_carp, OID_AUTO, stats, struct carpstats,
    carpstats, "CARP statistics (struct carpstats, netinet/ip_carp.h)");

#define	CARP_LOCK_INIT(sc)	mtx_init(&(sc)->sc_mtx, "carp_softc",   \
	NULL, MTX_DEF)
#define	CARP_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->sc_mtx)
#define	CARP_LOCK_ASSERT(sc)	mtx_assert(&(sc)->sc_mtx, MA_OWNED)
#define	CARP_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define	CARP_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define	CIF_LOCK_INIT(cif)	mtx_init(&(cif)->cif_mtx, "carp_if",   \
	NULL, MTX_DEF)
#define	CIF_LOCK_DESTROY(cif)	mtx_destroy(&(cif)->cif_mtx)
#define	CIF_LOCK_ASSERT(cif)	mtx_assert(&(cif)->cif_mtx, MA_OWNED)
#define	CIF_LOCK(cif)		mtx_lock(&(cif)->cif_mtx)
#define	CIF_UNLOCK(cif)		mtx_unlock(&(cif)->cif_mtx)
#define	CIF_FREE(cif)	do {				\
		CIF_LOCK(cif);				\
		if (TAILQ_EMPTY(&(cif)->cif_vrs))	\
			carp_free_if(cif);		\
		else					\
			CIF_UNLOCK(cif);		\
} while (0)

#define	CARP_LOG(...)	do {				\
	if (V_carp_log > 0)				\
		log(LOG_INFO, "carp: " __VA_ARGS__);	\
} while (0)

#define	CARP_DEBUG(...)	do {				\
	if (V_carp_log > 1)				\
		log(LOG_DEBUG, __VA_ARGS__);		\
} while (0)

#define	IFNET_FOREACH_IFA(ifp, ifa)					\
	CK_STAILQ_FOREACH((ifa), &(ifp)->if_addrhead, ifa_link)	\
		if ((ifa)->ifa_carp != NULL)

#define	CARP_FOREACH_IFA(sc, ifa)					\
	CARP_LOCK_ASSERT(sc);						\
	for (int _i = 0;						\
		_i < (sc)->sc_naddrs + (sc)->sc_naddrs6 &&		\
		((ifa) = sc->sc_ifas[_i]) != NULL;			\
		++_i)

#define	IFNET_FOREACH_CARP(ifp, sc)					\
	KASSERT(mtx_owned(&ifp->if_carp->cif_mtx) ||			\
	    sx_xlocked(&carp_sx), ("cif_vrs not locked"));		\
	TAILQ_FOREACH((sc), &(ifp)->if_carp->cif_vrs, sc_list)

#define	DEMOTE_ADVSKEW(sc)					\
    (((sc)->sc_advskew + V_carp_demotion > CARP_MAXSKEW) ?	\
    CARP_MAXSKEW : ((sc)->sc_advskew + V_carp_demotion))

static void	carp_input_c(struct mbuf *, struct carp_header *, sa_family_t);
static struct carp_softc
		*carp_alloc(struct ifnet *);
static void	carp_destroy(struct carp_softc *);
static struct carp_if
		*carp_alloc_if(struct ifnet *);
static void	carp_free_if(struct carp_if *);
static void	carp_set_state(struct carp_softc *, int, const char* reason);
static void	carp_sc_state(struct carp_softc *);
static void	carp_setrun(struct carp_softc *, sa_family_t);
static void	carp_master_down(void *);
static void	carp_master_down_locked(struct carp_softc *,
    		    const char* reason);
static void	carp_send_ad(void *);
static void	carp_send_ad_locked(struct carp_softc *);
static void	carp_addroute(struct carp_softc *);
static void	carp_ifa_addroute(struct ifaddr *);
static void	carp_delroute(struct carp_softc *);
static void	carp_ifa_delroute(struct ifaddr *);
static void	carp_send_ad_all(void *, int);
static void	carp_demote_adj(int, char *);

static LIST_HEAD(, carp_softc) carp_list;
static struct mtx carp_mtx;
static struct sx carp_sx;
static struct task carp_sendall_task =
    TASK_INITIALIZER(0, carp_send_ad_all, NULL);

static void
carp_hmac_prepare(struct carp_softc *sc)
{
	uint8_t version = CARP_VERSION, type = CARP_ADVERTISEMENT;
	uint8_t vhid = sc->sc_vhid & 0xff;
	struct ifaddr *ifa;
	int i, found;
#ifdef INET
	struct in_addr last, cur, in;
#endif
#ifdef INET6
	struct in6_addr last6, cur6, in6;
#endif

	CARP_LOCK_ASSERT(sc);

	/* Compute ipad from key. */
	bzero(sc->sc_pad, sizeof(sc->sc_pad));
	bcopy(sc->sc_key, sc->sc_pad, sizeof(sc->sc_key));
	for (i = 0; i < sizeof(sc->sc_pad); i++)
		sc->sc_pad[i] ^= 0x36;

	/* Precompute first part of inner hash. */
	SHA1Init(&sc->sc_sha1);
	SHA1Update(&sc->sc_sha1, sc->sc_pad, sizeof(sc->sc_pad));
	SHA1Update(&sc->sc_sha1, (void *)&version, sizeof(version));
	SHA1Update(&sc->sc_sha1, (void *)&type, sizeof(type));
	SHA1Update(&sc->sc_sha1, (void *)&vhid, sizeof(vhid));
#ifdef INET
	cur.s_addr = 0;
	do {
		found = 0;
		last = cur;
		cur.s_addr = 0xffffffff;
		CARP_FOREACH_IFA(sc, ifa) {
			in.s_addr = ifatoia(ifa)->ia_addr.sin_addr.s_addr;
			if (ifa->ifa_addr->sa_family == AF_INET &&
			    ntohl(in.s_addr) > ntohl(last.s_addr) &&
			    ntohl(in.s_addr) < ntohl(cur.s_addr)) {
				cur.s_addr = in.s_addr;
				found++;
			}
		}
		if (found)
			SHA1Update(&sc->sc_sha1, (void *)&cur, sizeof(cur));
	} while (found);
#endif /* INET */
#ifdef INET6
	memset(&cur6, 0, sizeof(cur6));
	do {
		found = 0;
		last6 = cur6;
		memset(&cur6, 0xff, sizeof(cur6));
		CARP_FOREACH_IFA(sc, ifa) {
			in6 = ifatoia6(ifa)->ia_addr.sin6_addr;
			if (IN6_IS_SCOPE_EMBED(&in6))
				in6.s6_addr16[1] = 0;
			if (ifa->ifa_addr->sa_family == AF_INET6 &&
			    memcmp(&in6, &last6, sizeof(in6)) > 0 &&
			    memcmp(&in6, &cur6, sizeof(in6)) < 0) {
				cur6 = in6;
				found++;
			}
		}
		if (found)
			SHA1Update(&sc->sc_sha1, (void *)&cur6, sizeof(cur6));
	} while (found);
#endif /* INET6 */

	/* convert ipad to opad */
	for (i = 0; i < sizeof(sc->sc_pad); i++)
		sc->sc_pad[i] ^= 0x36 ^ 0x5c;
}

static void
carp_hmac_generate(struct carp_softc *sc, uint32_t counter[2],
    unsigned char md[20])
{
	SHA1_CTX sha1ctx;

	CARP_LOCK_ASSERT(sc);

	/* fetch first half of inner hash */
	bcopy(&sc->sc_sha1, &sha1ctx, sizeof(sha1ctx));

	SHA1Update(&sha1ctx, (void *)counter, sizeof(sc->sc_counter));
	SHA1Final(md, &sha1ctx);

	/* outer hash */
	SHA1Init(&sha1ctx);
	SHA1Update(&sha1ctx, sc->sc_pad, sizeof(sc->sc_pad));
	SHA1Update(&sha1ctx, md, 20);
	SHA1Final(md, &sha1ctx);
}

static int
carp_hmac_verify(struct carp_softc *sc, uint32_t counter[2],
    unsigned char md[20])
{
	unsigned char md2[20];

	CARP_LOCK_ASSERT(sc);

	carp_hmac_generate(sc, counter, md2);

	return (bcmp(md, md2, sizeof(md2)));
}

/*
 * process input packet.
 * we have rearranged checks order compared to the rfc,
 * but it seems more efficient this way or not possible otherwise.
 */
#ifdef INET
int
carp_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct ip *ip = mtod(m, struct ip *);
	struct carp_header *ch;
	int iplen, len;

	iplen = *offp;
	*mp = NULL;

	CARPSTATS_INC(carps_ipackets);

	if (!V_carp_allow) {
		m_freem(m);
		return (IPPROTO_DONE);
	}

	/* verify that the IP TTL is 255.  */
	if (ip->ip_ttl != CARP_DFLTTL) {
		CARPSTATS_INC(carps_badttl);
		CARP_DEBUG("%s: received ttl %d != 255 on %s\n", __func__,
		    ip->ip_ttl,
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	iplen = ip->ip_hl << 2;

	if (m->m_pkthdr.len < iplen + sizeof(*ch)) {
		CARPSTATS_INC(carps_badlen);
		CARP_DEBUG("%s: received len %zd < sizeof(struct carp_header) "
		    "on %s\n", __func__, m->m_len - sizeof(struct ip),
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	if (iplen + sizeof(*ch) < m->m_len) {
		if ((m = m_pullup(m, iplen + sizeof(*ch))) == NULL) {
			CARPSTATS_INC(carps_hdrops);
			CARP_DEBUG("%s: pullup failed\n", __func__);
			return (IPPROTO_DONE);
		}
		ip = mtod(m, struct ip *);
	}
	ch = (struct carp_header *)((char *)ip + iplen);

	/*
	 * verify that the received packet length is
	 * equal to the CARP header
	 */
	len = iplen + sizeof(*ch);
	if (len > m->m_pkthdr.len) {
		CARPSTATS_INC(carps_badlen);
		CARP_DEBUG("%s: packet too short %d on %s\n", __func__,
		    m->m_pkthdr.len,
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	if ((m = m_pullup(m, len)) == NULL) {
		CARPSTATS_INC(carps_hdrops);
		return (IPPROTO_DONE);
	}
	ip = mtod(m, struct ip *);
	ch = (struct carp_header *)((char *)ip + iplen);

	/* verify the CARP checksum */
	m->m_data += iplen;
	if (in_cksum(m, len - iplen)) {
		CARPSTATS_INC(carps_badsum);
		CARP_DEBUG("%s: checksum failed on %s\n", __func__,
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return (IPPROTO_DONE);
	}
	m->m_data -= iplen;

	carp_input_c(m, ch, AF_INET);
	return (IPPROTO_DONE);
}
#endif

#ifdef INET6
int
carp6_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct carp_header *ch;
	u_int len;

	CARPSTATS_INC(carps_ipackets6);

	if (!V_carp_allow) {
		m_freem(m);
		return (IPPROTO_DONE);
	}

	/* check if received on a valid carp interface */
	if (m->m_pkthdr.rcvif->if_carp == NULL) {
		CARPSTATS_INC(carps_badif);
		CARP_DEBUG("%s: packet received on non-carp interface: %s\n",
		    __func__, m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	/* verify that the IP TTL is 255 */
	if (ip6->ip6_hlim != CARP_DFLTTL) {
		CARPSTATS_INC(carps_badttl);
		CARP_DEBUG("%s: received ttl %d != 255 on %s\n", __func__,
		    ip6->ip6_hlim, m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	/* verify that we have a complete carp packet */
	len = m->m_len;
	IP6_EXTHDR_GET(ch, struct carp_header *, m, *offp, sizeof(*ch));
	if (ch == NULL) {
		CARPSTATS_INC(carps_badlen);
		CARP_DEBUG("%s: packet size %u too small\n", __func__, len);
		return (IPPROTO_DONE);
	}


	/* verify the CARP checksum */
	m->m_data += *offp;
	if (in_cksum(m, sizeof(*ch))) {
		CARPSTATS_INC(carps_badsum);
		CARP_DEBUG("%s: checksum failed, on %s\n", __func__,
		    m->m_pkthdr.rcvif->if_xname);
		m_freem(m);
		return (IPPROTO_DONE);
	}
	m->m_data -= *offp;

	carp_input_c(m, ch, AF_INET6);
	return (IPPROTO_DONE);
}
#endif /* INET6 */

/*
 * This routine should not be necessary at all, but some switches
 * (VMWare ESX vswitches) can echo our own packets back at us,
 * and we must ignore them or they will cause us to drop out of
 * MASTER mode.
 *
 * We cannot catch all cases of network loops.  Instead, what we
 * do here is catch any packet that arrives with a carp header
 * with a VHID of 0, that comes from an address that is our own.
 * These packets are by definition "from us" (even if they are from
 * a misconfigured host that is pretending to be us).
 *
 * The VHID test is outside this mini-function.
 */
static int
carp_source_is_self(struct mbuf *m, struct ifaddr *ifa, sa_family_t af)
{
#ifdef INET
	struct ip *ip4;
	struct in_addr in4;
#endif
#ifdef INET6
	struct ip6_hdr *ip6;
	struct in6_addr in6;
#endif

	switch (af) {
#ifdef INET
	case AF_INET:
		ip4 = mtod(m, struct ip *);
		in4 = ifatoia(ifa)->ia_addr.sin_addr;
		return (in4.s_addr == ip4->ip_src.s_addr);
#endif
#ifdef INET6
	case AF_INET6:
		ip6 = mtod(m, struct ip6_hdr *);
		in6 = ifatoia6(ifa)->ia_addr.sin6_addr;
		return (memcmp(&in6, &ip6->ip6_src, sizeof(in6)) == 0);
#endif
	default:
		break;
	}
	return (0);
}

static void
carp_input_c(struct mbuf *m, struct carp_header *ch, sa_family_t af)
{
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct ifaddr *ifa, *match;
	struct carp_softc *sc;
	uint64_t tmp_counter;
	struct timeval sc_tv, ch_tv;
	struct epoch_tracker et;
	int error;

	/*
	 * Verify that the VHID is valid on the receiving interface.
	 *
	 * There should be just one match.  If there are none
	 * the VHID is not valid and we drop the packet.  If
	 * there are multiple VHID matches, take just the first
	 * one, for compatibility with previous code.  While we're
	 * scanning, check for obvious loops in the network topology
	 * (these should never happen, and as noted above, we may
	 * miss real loops; this is just a double-check).
	 */
	NET_EPOCH_ENTER(et);
	error = 0;
	match = NULL;
	IFNET_FOREACH_IFA(ifp, ifa) {
		if (match == NULL && ifa->ifa_carp != NULL &&
		    ifa->ifa_addr->sa_family == af &&
		    ifa->ifa_carp->sc_vhid == ch->carp_vhid)
			match = ifa;
		if (ch->carp_vhid == 0 && carp_source_is_self(m, ifa, af))
			error = ELOOP;
	}
	ifa = error ? NULL : match;
	if (ifa != NULL)
		ifa_ref(ifa);
	NET_EPOCH_EXIT(et);

	if (ifa == NULL) {
		if (error == ELOOP) {
			CARP_DEBUG("dropping looped packet on interface %s\n",
			    ifp->if_xname);
			CARPSTATS_INC(carps_badif);	/* ??? */
		} else {
			CARPSTATS_INC(carps_badvhid);
		}
		m_freem(m);
		return;
	}

	/* verify the CARP version. */
	if (ch->carp_version != CARP_VERSION) {
		CARPSTATS_INC(carps_badver);
		CARP_DEBUG("%s: invalid version %d\n", ifp->if_xname,
		    ch->carp_version);
		ifa_free(ifa);
		m_freem(m);
		return;
	}

	sc = ifa->ifa_carp;
	CARP_LOCK(sc);
	ifa_free(ifa);

	if (carp_hmac_verify(sc, ch->carp_counter, ch->carp_md)) {
		CARPSTATS_INC(carps_badauth);
		CARP_DEBUG("%s: incorrect hash for VHID %u@%s\n", __func__,
		    sc->sc_vhid, ifp->if_xname);
		goto out;
	}

	tmp_counter = ntohl(ch->carp_counter[0]);
	tmp_counter = tmp_counter<<32;
	tmp_counter += ntohl(ch->carp_counter[1]);

	/* XXX Replay protection goes here */

	sc->sc_init_counter = 0;
	sc->sc_counter = tmp_counter;

	sc_tv.tv_sec = sc->sc_advbase;
	sc_tv.tv_usec = DEMOTE_ADVSKEW(sc) * 1000000 / 256;
	ch_tv.tv_sec = ch->carp_advbase;
	ch_tv.tv_usec = ch->carp_advskew * 1000000 / 256;

	switch (sc->sc_state) {
	case INIT:
		break;
	case MASTER:
		/*
		 * If we receive an advertisement from a master who's going to
		 * be more frequent than us, go into BACKUP state.
		 */
		if (timevalcmp(&sc_tv, &ch_tv, >) ||
		    timevalcmp(&sc_tv, &ch_tv, ==)) {
			callout_stop(&sc->sc_ad_tmo);
			carp_set_state(sc, BACKUP,
			    "more frequent advertisement received");
			carp_setrun(sc, 0);
			carp_delroute(sc);
		}
		break;
	case BACKUP:
		/*
		 * If we're pre-empting masters who advertise slower than us,
		 * and this one claims to be slower, treat him as down.
		 */
		if (V_carp_preempt && timevalcmp(&sc_tv, &ch_tv, <)) {
			carp_master_down_locked(sc,
			    "preempting a slower master");
			break;
		}

		/*
		 *  If the master is going to advertise at such a low frequency
		 *  that he's guaranteed to time out, we'd might as well just
		 *  treat him as timed out now.
		 */
		sc_tv.tv_sec = sc->sc_advbase * 3;
		if (timevalcmp(&sc_tv, &ch_tv, <)) {
			carp_master_down_locked(sc, "master will time out");
			break;
		}

		/*
		 * Otherwise, we reset the counter and wait for the next
		 * advertisement.
		 */
		carp_setrun(sc, af);
		break;
	}

out:
	CARP_UNLOCK(sc);
	m_freem(m);
}

static int
carp_prepare_ad(struct mbuf *m, struct carp_softc *sc, struct carp_header *ch)
{
	struct m_tag *mtag;

	if (sc->sc_init_counter) {
		/* this could also be seconds since unix epoch */
		sc->sc_counter = arc4random();
		sc->sc_counter = sc->sc_counter << 32;
		sc->sc_counter += arc4random();
	} else
		sc->sc_counter++;

	ch->carp_counter[0] = htonl((sc->sc_counter>>32)&0xffffffff);
	ch->carp_counter[1] = htonl(sc->sc_counter&0xffffffff);

	carp_hmac_generate(sc, ch->carp_counter, ch->carp_md);

	/* Tag packet for carp_output */
	if ((mtag = m_tag_get(PACKET_TAG_CARP, sizeof(struct carp_softc *),
	    M_NOWAIT)) == NULL) {
		m_freem(m);
		CARPSTATS_INC(carps_onomem);
		return (ENOMEM);
	}
	bcopy(&sc, mtag + 1, sizeof(sc));
	m_tag_prepend(m, mtag);

	return (0);
}

/*
 * To avoid LORs and possible recursions this function shouldn't
 * be called directly, but scheduled via taskqueue.
 */
static void
carp_send_ad_all(void *ctx __unused, int pending __unused)
{
	struct carp_softc *sc;

	mtx_lock(&carp_mtx);
	LIST_FOREACH(sc, &carp_list, sc_next)
		if (sc->sc_state == MASTER) {
			CARP_LOCK(sc);
			CURVNET_SET(sc->sc_carpdev->if_vnet);
			carp_send_ad_locked(sc);
			CURVNET_RESTORE();
			CARP_UNLOCK(sc);
		}
	mtx_unlock(&carp_mtx);
}

/* Send a periodic advertisement, executed in callout context. */
static void
carp_send_ad(void *v)
{
	struct carp_softc *sc = v;

	CARP_LOCK_ASSERT(sc);
	CURVNET_SET(sc->sc_carpdev->if_vnet);
	carp_send_ad_locked(sc);
	CURVNET_RESTORE();
	CARP_UNLOCK(sc);
}

static void
carp_send_ad_error(struct carp_softc *sc, int error)
{

	if (error) {
		if (sc->sc_sendad_errors < INT_MAX)
			sc->sc_sendad_errors++;
		if (sc->sc_sendad_errors == CARP_SENDAD_MAX_ERRORS) {
			static const char fmt[] = "send error %d on %s";
			char msg[sizeof(fmt) + IFNAMSIZ];

			sprintf(msg, fmt, error, sc->sc_carpdev->if_xname);
			carp_demote_adj(V_carp_senderr_adj, msg);
		}
		sc->sc_sendad_success = 0;
	} else {
		if (sc->sc_sendad_errors >= CARP_SENDAD_MAX_ERRORS &&
		    ++sc->sc_sendad_success >= CARP_SENDAD_MIN_SUCCESS) {
			static const char fmt[] = "send ok on %s";
			char msg[sizeof(fmt) + IFNAMSIZ];

			sprintf(msg, fmt, sc->sc_carpdev->if_xname);
			carp_demote_adj(-V_carp_senderr_adj, msg);
			sc->sc_sendad_errors = 0;
		} else
			sc->sc_sendad_errors = 0;
	}
}

/*
 * Pick the best ifaddr on the given ifp for sending CARP
 * advertisements.
 *
 * "Best" here is defined by ifa_preferred().  This function is much
 * much like ifaof_ifpforaddr() except that we just use ifa_preferred().
 *
 * (This could be simplified to return the actual address, except that
 * it has a different format in AF_INET and AF_INET6.)
 */
static struct ifaddr *
carp_best_ifa(int af, struct ifnet *ifp)
{
	struct epoch_tracker et;
	struct ifaddr *ifa, *best;

	if (af >= AF_MAX)
		return (NULL);
	best = NULL;
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family == af &&
		    (best == NULL || ifa_preferred(best, ifa)))
			best = ifa;
	}
	NET_EPOCH_EXIT(et);
	if (best != NULL)
		ifa_ref(best);
	return (best);
}

static void
carp_send_ad_locked(struct carp_softc *sc)
{
	struct carp_header ch;
	struct timeval tv;
	struct ifaddr *ifa;
	struct carp_header *ch_ptr;
	struct mbuf *m;
	int len, advskew;

	CARP_LOCK_ASSERT(sc);

	advskew = DEMOTE_ADVSKEW(sc);
	tv.tv_sec = sc->sc_advbase;
	tv.tv_usec = advskew * 1000000 / 256;

	ch.carp_version = CARP_VERSION;
	ch.carp_type = CARP_ADVERTISEMENT;
	ch.carp_vhid = sc->sc_vhid;
	ch.carp_advbase = sc->sc_advbase;
	ch.carp_advskew = advskew;
	ch.carp_authlen = 7;	/* XXX DEFINE */
	ch.carp_pad1 = 0;	/* must be zero */
	ch.carp_cksum = 0;

	/* XXXGL: OpenBSD picks first ifaddr with needed family. */

#ifdef INET
	if (sc->sc_naddrs) {
		struct ip *ip;

		m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m == NULL) {
			CARPSTATS_INC(carps_onomem);
			goto resched;
		}
		len = sizeof(*ip) + sizeof(ch);
		m->m_pkthdr.len = len;
		m->m_pkthdr.rcvif = NULL;
		m->m_len = len;
		M_ALIGN(m, m->m_len);
		m->m_flags |= M_MCAST;
		ip = mtod(m, struct ip *);
		ip->ip_v = IPVERSION;
		ip->ip_hl = sizeof(*ip) >> 2;
		ip->ip_tos = V_carp_dscp << IPTOS_DSCP_OFFSET;
		ip->ip_len = htons(len);
		ip->ip_off = htons(IP_DF);
		ip->ip_ttl = CARP_DFLTTL;
		ip->ip_p = IPPROTO_CARP;
		ip->ip_sum = 0;
		ip_fillid(ip);

		ifa = carp_best_ifa(AF_INET, sc->sc_carpdev);
		if (ifa != NULL) {
			ip->ip_src.s_addr =
			    ifatoia(ifa)->ia_addr.sin_addr.s_addr;
			ifa_free(ifa);
		} else
			ip->ip_src.s_addr = 0;
		ip->ip_dst.s_addr = htonl(INADDR_CARP_GROUP);

		ch_ptr = (struct carp_header *)(&ip[1]);
		bcopy(&ch, ch_ptr, sizeof(ch));
		if (carp_prepare_ad(m, sc, ch_ptr))
			goto resched;

		m->m_data += sizeof(*ip);
		ch_ptr->carp_cksum = in_cksum(m, len - sizeof(*ip));
		m->m_data -= sizeof(*ip);

		CARPSTATS_INC(carps_opackets);

		carp_send_ad_error(sc, ip_output(m, NULL, NULL, IP_RAWOUTPUT,
		    &sc->sc_carpdev->if_carp->cif_imo, NULL));
	}
#endif /* INET */
#ifdef INET6
	if (sc->sc_naddrs6) {
		struct ip6_hdr *ip6;

		m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m == NULL) {
			CARPSTATS_INC(carps_onomem);
			goto resched;
		}
		len = sizeof(*ip6) + sizeof(ch);
		m->m_pkthdr.len = len;
		m->m_pkthdr.rcvif = NULL;
		m->m_len = len;
		M_ALIGN(m, m->m_len);
		m->m_flags |= M_MCAST;
		ip6 = mtod(m, struct ip6_hdr *);
		bzero(ip6, sizeof(*ip6));
		ip6->ip6_vfc |= IPV6_VERSION;
		/* Traffic class isn't defined in ip6 struct instead
		 * it gets offset into flowid field */
		ip6->ip6_flow |= htonl(V_carp_dscp << (IPV6_FLOWLABEL_LEN +
		    IPTOS_DSCP_OFFSET));
		ip6->ip6_hlim = CARP_DFLTTL;
		ip6->ip6_nxt = IPPROTO_CARP;

		/* set the source address */
		ifa = carp_best_ifa(AF_INET6, sc->sc_carpdev);
		if (ifa != NULL) {
			bcopy(IFA_IN6(ifa), &ip6->ip6_src,
			    sizeof(struct in6_addr));
			ifa_free(ifa);
		} else
			/* This should never happen with IPv6. */
			bzero(&ip6->ip6_src, sizeof(struct in6_addr));

		/* Set the multicast destination. */
		ip6->ip6_dst.s6_addr16[0] = htons(0xff02);
		ip6->ip6_dst.s6_addr8[15] = 0x12;
		if (in6_setscope(&ip6->ip6_dst, sc->sc_carpdev, NULL) != 0) {
			m_freem(m);
			CARP_DEBUG("%s: in6_setscope failed\n", __func__);
			goto resched;
		}

		ch_ptr = (struct carp_header *)(&ip6[1]);
		bcopy(&ch, ch_ptr, sizeof(ch));
		if (carp_prepare_ad(m, sc, ch_ptr))
			goto resched;

		m->m_data += sizeof(*ip6);
		ch_ptr->carp_cksum = in_cksum(m, len - sizeof(*ip6));
		m->m_data -= sizeof(*ip6);

		CARPSTATS_INC(carps_opackets6);

		carp_send_ad_error(sc, ip6_output(m, NULL, NULL, 0,
		    &sc->sc_carpdev->if_carp->cif_im6o, NULL, NULL));
	}
#endif /* INET6 */

resched:
	callout_reset(&sc->sc_ad_tmo, tvtohz(&tv), carp_send_ad, sc);
}

static void
carp_addroute(struct carp_softc *sc)
{
	struct ifaddr *ifa;

	CARP_FOREACH_IFA(sc, ifa)
		carp_ifa_addroute(ifa);
}

static void
carp_ifa_addroute(struct ifaddr *ifa)
{

	switch (ifa->ifa_addr->sa_family) {
#ifdef INET
	case AF_INET:
		in_addprefix(ifatoia(ifa), RTF_UP);
		ifa_add_loopback_route(ifa,
		    (struct sockaddr *)&ifatoia(ifa)->ia_addr);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		ifa_add_loopback_route(ifa,
		    (struct sockaddr *)&ifatoia6(ifa)->ia_addr);
		nd6_add_ifa_lle(ifatoia6(ifa));
		break;
#endif
	}
}

static void
carp_delroute(struct carp_softc *sc)
{
	struct ifaddr *ifa;

	CARP_FOREACH_IFA(sc, ifa)
		carp_ifa_delroute(ifa);
}

static void
carp_ifa_delroute(struct ifaddr *ifa)
{

	switch (ifa->ifa_addr->sa_family) {
#ifdef INET
	case AF_INET:
		ifa_del_loopback_route(ifa,
		    (struct sockaddr *)&ifatoia(ifa)->ia_addr);
		in_scrubprefix(ifatoia(ifa), LLE_STATIC);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		ifa_del_loopback_route(ifa,
		    (struct sockaddr *)&ifatoia6(ifa)->ia_addr);
		nd6_rem_ifa_lle(ifatoia6(ifa), 1);
		break;
#endif
	}
}

int
carp_master(struct ifaddr *ifa)
{
	struct carp_softc *sc = ifa->ifa_carp;

	return (sc->sc_state == MASTER);
}

#ifdef INET
/*
 * Broadcast a gratuitous ARP request containing
 * the virtual router MAC address for each IP address
 * associated with the virtual router.
 */
static void
carp_send_arp(struct carp_softc *sc)
{
	struct ifaddr *ifa;
	struct in_addr addr;

	CARP_FOREACH_IFA(sc, ifa) {
		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;
		addr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
		arp_announce_ifaddr(sc->sc_carpdev, addr, LLADDR(&sc->sc_addr));
	}
}

int
carp_iamatch(struct ifaddr *ifa, uint8_t **enaddr)
{
	struct carp_softc *sc = ifa->ifa_carp;

	if (sc->sc_state == MASTER) {
		*enaddr = LLADDR(&sc->sc_addr);
		return (1);
	}

	return (0);
}
#endif

#ifdef INET6
static void
carp_send_na(struct carp_softc *sc)
{
	static struct in6_addr mcast = IN6ADDR_LINKLOCAL_ALLNODES_INIT;
	struct ifaddr *ifa;
	struct in6_addr *in6;

	CARP_FOREACH_IFA(sc, ifa) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		in6 = IFA_IN6(ifa);
		nd6_na_output(sc->sc_carpdev, &mcast, in6,
		    ND_NA_FLAG_OVERRIDE, 1, NULL);
		DELAY(1000);	/* XXX */
	}
}

/*
 * Returns ifa in case it's a carp address and it is MASTER, or if the address
 * matches and is not a carp address.  Returns NULL otherwise.
 */
struct ifaddr *
carp_iamatch6(struct ifnet *ifp, struct in6_addr *taddr)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;

	ifa = NULL;
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		if (!IN6_ARE_ADDR_EQUAL(taddr, IFA_IN6(ifa)))
			continue;
		if (ifa->ifa_carp && ifa->ifa_carp->sc_state != MASTER)
			ifa = NULL;
		else
			ifa_ref(ifa);
		break;
	}
	NET_EPOCH_EXIT(et);

	return (ifa);
}

caddr_t
carp_macmatch6(struct ifnet *ifp, struct mbuf *m, const struct in6_addr *taddr)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;

	NET_EPOCH_ENTER(et);
	IFNET_FOREACH_IFA(ifp, ifa)
		if (ifa->ifa_addr->sa_family == AF_INET6 &&
		    IN6_ARE_ADDR_EQUAL(taddr, IFA_IN6(ifa))) {
			struct carp_softc *sc = ifa->ifa_carp;
			struct m_tag *mtag;

			NET_EPOCH_EXIT(et);

			mtag = m_tag_get(PACKET_TAG_CARP,
			    sizeof(struct carp_softc *), M_NOWAIT);
			if (mtag == NULL)
				/* Better a bit than nothing. */
				return (LLADDR(&sc->sc_addr));

			bcopy(&sc, mtag + 1, sizeof(sc));
			m_tag_prepend(m, mtag);

			return (LLADDR(&sc->sc_addr));
		}
	NET_EPOCH_EXIT(et);

	return (NULL);
}
#endif /* INET6 */

int
carp_forus(struct ifnet *ifp, u_char *dhost)
{
	struct carp_softc *sc;
	uint8_t *ena = dhost;

	if (ena[0] || ena[1] || ena[2] != 0x5e || ena[3] || ena[4] != 1)
		return (0);

	CIF_LOCK(ifp->if_carp);
	IFNET_FOREACH_CARP(ifp, sc) {
		CARP_LOCK(sc);
		if (sc->sc_state == MASTER && !bcmp(dhost, LLADDR(&sc->sc_addr),
		    ETHER_ADDR_LEN)) {
			CARP_UNLOCK(sc);
			CIF_UNLOCK(ifp->if_carp);
			return (1);
		}
		CARP_UNLOCK(sc);
	}
	CIF_UNLOCK(ifp->if_carp);

	return (0);
}

/* Master down timeout event, executed in callout context. */
static void
carp_master_down(void *v)
{
	struct carp_softc *sc = v;

	CARP_LOCK_ASSERT(sc);

	CURVNET_SET(sc->sc_carpdev->if_vnet);
	if (sc->sc_state == BACKUP) {
		carp_master_down_locked(sc, "master timed out");
	}
	CURVNET_RESTORE();

	CARP_UNLOCK(sc);
}

static void
carp_master_down_locked(struct carp_softc *sc, const char *reason)
{

	CARP_LOCK_ASSERT(sc);

	switch (sc->sc_state) {
	case BACKUP:
		carp_set_state(sc, MASTER, reason);
		carp_send_ad_locked(sc);
#ifdef INET
		carp_send_arp(sc);
#endif
#ifdef INET6
		carp_send_na(sc);
#endif
		carp_setrun(sc, 0);
		carp_addroute(sc);
		break;
	case INIT:
	case MASTER:
#ifdef INVARIANTS
		panic("carp: VHID %u@%s: master_down event in %s state\n",
		    sc->sc_vhid,
		    sc->sc_carpdev->if_xname,
		    sc->sc_state ? "MASTER" : "INIT");
#endif
		break;
	}
}

/*
 * When in backup state, af indicates whether to reset the master down timer
 * for v4 or v6. If it's set to zero, reset the ones which are already pending.
 */
static void
carp_setrun(struct carp_softc *sc, sa_family_t af)
{
	struct timeval tv;

	CARP_LOCK_ASSERT(sc);

	if ((sc->sc_carpdev->if_flags & IFF_UP) == 0 ||
	    sc->sc_carpdev->if_link_state != LINK_STATE_UP ||
	    (sc->sc_naddrs == 0 && sc->sc_naddrs6 == 0) ||
	    !V_carp_allow)
		return;

	switch (sc->sc_state) {
	case INIT:
		carp_set_state(sc, BACKUP, "initialization complete");
		carp_setrun(sc, 0);
		break;
	case BACKUP:
		callout_stop(&sc->sc_ad_tmo);
		tv.tv_sec = 3 * sc->sc_advbase;
		tv.tv_usec = sc->sc_advskew * 1000000 / 256;
		switch (af) {
#ifdef INET
		case AF_INET:
			callout_reset(&sc->sc_md_tmo, tvtohz(&tv),
			    carp_master_down, sc);
			break;
#endif
#ifdef INET6
		case AF_INET6:
			callout_reset(&sc->sc_md6_tmo, tvtohz(&tv),
			    carp_master_down, sc);
			break;
#endif
		default:
#ifdef INET
			if (sc->sc_naddrs)
				callout_reset(&sc->sc_md_tmo, tvtohz(&tv),
				    carp_master_down, sc);
#endif
#ifdef INET6
			if (sc->sc_naddrs6)
				callout_reset(&sc->sc_md6_tmo, tvtohz(&tv),
				    carp_master_down, sc);
#endif
			break;
		}
		break;
	case MASTER:
		tv.tv_sec = sc->sc_advbase;
		tv.tv_usec = sc->sc_advskew * 1000000 / 256;
		callout_reset(&sc->sc_ad_tmo, tvtohz(&tv),
		    carp_send_ad, sc);
		break;
	}
}

/*
 * Setup multicast structures.
 */
static int
carp_multicast_setup(struct carp_if *cif, sa_family_t sa)
{
	struct ifnet *ifp = cif->cif_ifp;
	int error = 0;

	switch (sa) {
#ifdef INET
	case AF_INET:
	    {
		struct ip_moptions *imo = &cif->cif_imo;
		struct in_addr addr;

		if (imo->imo_membership)
			return (0);

		imo->imo_membership = (struct in_multi **)malloc(
		    (sizeof(struct in_multi *) * IP_MIN_MEMBERSHIPS), M_CARP,
		    M_WAITOK);
		imo->imo_mfilters = NULL;
		imo->imo_max_memberships = IP_MIN_MEMBERSHIPS;
		imo->imo_multicast_vif = -1;

		addr.s_addr = htonl(INADDR_CARP_GROUP);
		if ((error = in_joingroup(ifp, &addr, NULL,
		    &imo->imo_membership[0])) != 0) {
			free(imo->imo_membership, M_CARP);
			break;
		}
		imo->imo_num_memberships++;
		imo->imo_multicast_ifp = ifp;
		imo->imo_multicast_ttl = CARP_DFLTTL;
		imo->imo_multicast_loop = 0;
		break;
	   }
#endif
#ifdef INET6
	case AF_INET6:
	    {
		struct ip6_moptions *im6o = &cif->cif_im6o;
		struct in6_addr in6;
		struct in6_multi *in6m;

		if (im6o->im6o_membership)
			return (0);

		im6o->im6o_membership = (struct in6_multi **)malloc(
		    (sizeof(struct in6_multi *) * IPV6_MIN_MEMBERSHIPS), M_CARP,
		    M_ZERO | M_WAITOK);
		im6o->im6o_mfilters = NULL;
		im6o->im6o_max_memberships = IPV6_MIN_MEMBERSHIPS;
		im6o->im6o_multicast_hlim = CARP_DFLTTL;
		im6o->im6o_multicast_ifp = ifp;

		/* Join IPv6 CARP multicast group. */
		bzero(&in6, sizeof(in6));
		in6.s6_addr16[0] = htons(0xff02);
		in6.s6_addr8[15] = 0x12;
		if ((error = in6_setscope(&in6, ifp, NULL)) != 0) {
			free(im6o->im6o_membership, M_CARP);
			break;
		}
		in6m = NULL;
		if ((error = in6_joingroup(ifp, &in6, NULL, &in6m, 0)) != 0) {
			free(im6o->im6o_membership, M_CARP);
			break;
		}
		in6m_acquire(in6m);
		im6o->im6o_membership[0] = in6m;
		im6o->im6o_num_memberships++;

		/* Join solicited multicast address. */
		bzero(&in6, sizeof(in6));
		in6.s6_addr16[0] = htons(0xff02);
		in6.s6_addr32[1] = 0;
		in6.s6_addr32[2] = htonl(1);
		in6.s6_addr32[3] = 0;
		in6.s6_addr8[12] = 0xff;
		if ((error = in6_setscope(&in6, ifp, NULL)) != 0) {
			in6_leavegroup(im6o->im6o_membership[0], NULL);
			free(im6o->im6o_membership, M_CARP);
			break;
		}
		in6m = NULL;
		if ((error = in6_joingroup(ifp, &in6, NULL, &in6m, 0)) != 0) {
			in6_leavegroup(im6o->im6o_membership[0], NULL);
			free(im6o->im6o_membership, M_CARP);
			break;
		}
		in6m_acquire(in6m);
		im6o->im6o_membership[1] = in6m;
		im6o->im6o_num_memberships++;
		break;
	    }
#endif
	}

	return (error);
}

/*
 * Free multicast structures.
 */
static void
carp_multicast_cleanup(struct carp_if *cif, sa_family_t sa)
{

	sx_assert(&carp_sx, SA_XLOCKED);

	switch (sa) {
#ifdef INET
	case AF_INET:
		if (cif->cif_naddrs == 0) {
			struct ip_moptions *imo = &cif->cif_imo;

			in_leavegroup(imo->imo_membership[0], NULL);
			KASSERT(imo->imo_mfilters == NULL,
			    ("%s: imo_mfilters != NULL", __func__));
			free(imo->imo_membership, M_CARP);
			imo->imo_membership = NULL;

		}
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (cif->cif_naddrs6 == 0) {
			struct ip6_moptions *im6o = &cif->cif_im6o;

			in6_leavegroup(im6o->im6o_membership[0], NULL);
			in6_leavegroup(im6o->im6o_membership[1], NULL);
			KASSERT(im6o->im6o_mfilters == NULL,
			    ("%s: im6o_mfilters != NULL", __func__));
			free(im6o->im6o_membership, M_CARP);
			im6o->im6o_membership = NULL;
		}
		break;
#endif
	}
}

int
carp_output(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *sa)
{
	struct m_tag *mtag;
	struct carp_softc *sc;

	if (!sa)
		return (0);

	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		break;
#endif
#ifdef INET6
	case AF_INET6:
		break;
#endif
	default:
		return (0);
	}

	mtag = m_tag_find(m, PACKET_TAG_CARP, NULL);
	if (mtag == NULL)
		return (0);

	bcopy(mtag + 1, &sc, sizeof(sc));

	/* Set the source MAC address to the Virtual Router MAC Address. */
	switch (ifp->if_type) {
	case IFT_ETHER:
	case IFT_BRIDGE:
	case IFT_L2VLAN: {
			struct ether_header *eh;

			eh = mtod(m, struct ether_header *);
			eh->ether_shost[0] = 0;
			eh->ether_shost[1] = 0;
			eh->ether_shost[2] = 0x5e;
			eh->ether_shost[3] = 0;
			eh->ether_shost[4] = 1;
			eh->ether_shost[5] = sc->sc_vhid;
		}
		break;
	default:
		printf("%s: carp is not supported for the %d interface type\n",
		    ifp->if_xname, ifp->if_type);
		return (EOPNOTSUPP);
	}

	return (0);
}

static struct carp_softc*
carp_alloc(struct ifnet *ifp)
{
	struct carp_softc *sc;
	struct carp_if *cif;

	sx_assert(&carp_sx, SA_XLOCKED);

	if ((cif = ifp->if_carp) == NULL)
		cif = carp_alloc_if(ifp);

	sc = malloc(sizeof(*sc), M_CARP, M_WAITOK|M_ZERO);

	sc->sc_advbase = CARP_DFLTINTV;
	sc->sc_vhid = -1;	/* required setting */
	sc->sc_init_counter = 1;
	sc->sc_state = INIT;

	sc->sc_ifasiz = sizeof(struct ifaddr *);
	sc->sc_ifas = malloc(sc->sc_ifasiz, M_CARP, M_WAITOK|M_ZERO);
	sc->sc_carpdev = ifp;

	CARP_LOCK_INIT(sc);
#ifdef INET
	callout_init_mtx(&sc->sc_md_tmo, &sc->sc_mtx, CALLOUT_RETURNUNLOCKED);
#endif
#ifdef INET6
	callout_init_mtx(&sc->sc_md6_tmo, &sc->sc_mtx, CALLOUT_RETURNUNLOCKED);
#endif
	callout_init_mtx(&sc->sc_ad_tmo, &sc->sc_mtx, CALLOUT_RETURNUNLOCKED);

	CIF_LOCK(cif);
	TAILQ_INSERT_TAIL(&cif->cif_vrs, sc, sc_list);
	CIF_UNLOCK(cif);

	mtx_lock(&carp_mtx);
	LIST_INSERT_HEAD(&carp_list, sc, sc_next);
	mtx_unlock(&carp_mtx);

	return (sc);
}

static void
carp_grow_ifas(struct carp_softc *sc)
{
	struct ifaddr **new;

	new = malloc(sc->sc_ifasiz * 2, M_CARP, M_WAITOK | M_ZERO);
	CARP_LOCK(sc);
	bcopy(sc->sc_ifas, new, sc->sc_ifasiz);
	free(sc->sc_ifas, M_CARP);
	sc->sc_ifas = new;
	sc->sc_ifasiz *= 2;
	CARP_UNLOCK(sc);
}

static void
carp_destroy(struct carp_softc *sc)
{
	struct ifnet *ifp = sc->sc_carpdev;
	struct carp_if *cif = ifp->if_carp;

	sx_assert(&carp_sx, SA_XLOCKED);

	if (sc->sc_suppress)
		carp_demote_adj(-V_carp_ifdown_adj, "vhid removed");
	CARP_UNLOCK(sc);

	CIF_LOCK(cif);
	TAILQ_REMOVE(&cif->cif_vrs, sc, sc_list);
	CIF_UNLOCK(cif);

	mtx_lock(&carp_mtx);
	LIST_REMOVE(sc, sc_next);
	mtx_unlock(&carp_mtx);

	callout_drain(&sc->sc_ad_tmo);
#ifdef INET
	callout_drain(&sc->sc_md_tmo);
#endif
#ifdef INET6
	callout_drain(&sc->sc_md6_tmo);
#endif
	CARP_LOCK_DESTROY(sc);

	free(sc->sc_ifas, M_CARP);
	free(sc, M_CARP);
}

static struct carp_if*
carp_alloc_if(struct ifnet *ifp)
{
	struct carp_if *cif;
	int error;

	cif = malloc(sizeof(*cif), M_CARP, M_WAITOK|M_ZERO);

	if ((error = ifpromisc(ifp, 1)) != 0)
		printf("%s: ifpromisc(%s) failed: %d\n",
		    __func__, ifp->if_xname, error);
	else
		cif->cif_flags |= CIF_PROMISC;

	CIF_LOCK_INIT(cif);
	cif->cif_ifp = ifp;
	TAILQ_INIT(&cif->cif_vrs);

	IF_ADDR_WLOCK(ifp);
	ifp->if_carp = cif;
	if_ref(ifp);
	IF_ADDR_WUNLOCK(ifp);

	return (cif);
}

static void
carp_free_if(struct carp_if *cif)
{
	struct ifnet *ifp = cif->cif_ifp;

	CIF_LOCK_ASSERT(cif);
	KASSERT(TAILQ_EMPTY(&cif->cif_vrs), ("%s: softc list not empty",
	    __func__));

	IF_ADDR_WLOCK(ifp);
	ifp->if_carp = NULL;
	IF_ADDR_WUNLOCK(ifp);

	CIF_LOCK_DESTROY(cif);

	if (cif->cif_flags & CIF_PROMISC)
		ifpromisc(ifp, 0);
	if_rele(ifp);

	free(cif, M_CARP);
}

static void
carp_carprcp(struct carpreq *carpr, struct carp_softc *sc, int priv)
{

	CARP_LOCK(sc);
	carpr->carpr_state = sc->sc_state;
	carpr->carpr_vhid = sc->sc_vhid;
	carpr->carpr_advbase = sc->sc_advbase;
	carpr->carpr_advskew = sc->sc_advskew;
	if (priv)
		bcopy(sc->sc_key, carpr->carpr_key, sizeof(carpr->carpr_key));
	else
		bzero(carpr->carpr_key, sizeof(carpr->carpr_key));
	CARP_UNLOCK(sc);
}

int
carp_ioctl(struct ifreq *ifr, u_long cmd, struct thread *td)
{
	struct carpreq carpr;
	struct ifnet *ifp;
	struct carp_softc *sc = NULL;
	int error = 0, locked = 0;

	if ((error = copyin(ifr_data_get_ptr(ifr), &carpr, sizeof carpr)))
		return (error);

	ifp = ifunit_ref(ifr->ifr_name);
	if (ifp == NULL)
		return (ENXIO);

	switch (ifp->if_type) {
	case IFT_ETHER:
	case IFT_L2VLAN:
	case IFT_BRIDGE:
		break;
	default:
		error = EOPNOTSUPP;
		goto out;
	}

	if ((ifp->if_flags & IFF_MULTICAST) == 0) {
		error = EADDRNOTAVAIL;
		goto out;
	}

	sx_xlock(&carp_sx);
	switch (cmd) {
	case SIOCSVH:
		if ((error = priv_check(td, PRIV_NETINET_CARP)))
			break;
		if (carpr.carpr_vhid <= 0 || carpr.carpr_vhid > CARP_MAXVHID ||
		    carpr.carpr_advbase < 0 || carpr.carpr_advskew < 0) {
			error = EINVAL;
			break;
		}

		if (ifp->if_carp) {
			IFNET_FOREACH_CARP(ifp, sc)
				if (sc->sc_vhid == carpr.carpr_vhid)
					break;
		}
		if (sc == NULL) {
			sc = carp_alloc(ifp);
			CARP_LOCK(sc);
			sc->sc_vhid = carpr.carpr_vhid;
			LLADDR(&sc->sc_addr)[0] = 0;
			LLADDR(&sc->sc_addr)[1] = 0;
			LLADDR(&sc->sc_addr)[2] = 0x5e;
			LLADDR(&sc->sc_addr)[3] = 0;
			LLADDR(&sc->sc_addr)[4] = 1;
			LLADDR(&sc->sc_addr)[5] = sc->sc_vhid;
		} else
			CARP_LOCK(sc);
		locked = 1;
		if (carpr.carpr_advbase > 0) {
			if (carpr.carpr_advbase > 255 ||
			    carpr.carpr_advbase < CARP_DFLTINTV) {
				error = EINVAL;
				break;
			}
			sc->sc_advbase = carpr.carpr_advbase;
		}
		if (carpr.carpr_advskew >= 255) {
			error = EINVAL;
			break;
		}
		sc->sc_advskew = carpr.carpr_advskew;
		if (carpr.carpr_key[0] != '\0') {
			bcopy(carpr.carpr_key, sc->sc_key, sizeof(sc->sc_key));
			carp_hmac_prepare(sc);
		}
		if (sc->sc_state != INIT &&
		    carpr.carpr_state != sc->sc_state) {
			switch (carpr.carpr_state) {
			case BACKUP:
				callout_stop(&sc->sc_ad_tmo);
				carp_set_state(sc, BACKUP,
				    "user requested via ifconfig");
				carp_setrun(sc, 0);
				carp_delroute(sc);
				break;
			case MASTER:
				carp_master_down_locked(sc,
				    "user requested via ifconfig");
				break;
			default:
				break;
			}
		}
		break;

	case SIOCGVH:
	    {
		int priveleged;

		if (carpr.carpr_vhid < 0 || carpr.carpr_vhid > CARP_MAXVHID) {
			error = EINVAL;
			break;
		}
		if (carpr.carpr_count < 1) {
			error = EMSGSIZE;
			break;
		}
		if (ifp->if_carp == NULL) {
			error = ENOENT;
			break;
		}

		priveleged = (priv_check(td, PRIV_NETINET_CARP) == 0);
		if (carpr.carpr_vhid != 0) {
			IFNET_FOREACH_CARP(ifp, sc)
				if (sc->sc_vhid == carpr.carpr_vhid)
					break;
			if (sc == NULL) {
				error = ENOENT;
				break;
			}
			carp_carprcp(&carpr, sc, priveleged);
			error = copyout(&carpr, ifr_data_get_ptr(ifr),
			    sizeof(carpr));
		} else  {
			int i, count;

			count = 0;
			IFNET_FOREACH_CARP(ifp, sc)
				count++;

			if (count > carpr.carpr_count) {
				CIF_UNLOCK(ifp->if_carp);
				error = EMSGSIZE;
				break;
			}

			i = 0;
			IFNET_FOREACH_CARP(ifp, sc) {
				carp_carprcp(&carpr, sc, priveleged);
				carpr.carpr_count = count;
				error = copyout(&carpr,
				    (caddr_t)ifr_data_get_ptr(ifr) +
				    (i * sizeof(carpr)), sizeof(carpr));
				if (error) {
					CIF_UNLOCK(ifp->if_carp);
					break;
				}
				i++;
			}
		}
		break;
	    }
	default:
		error = EINVAL;
	}
	sx_xunlock(&carp_sx);

out:
	if (locked)
		CARP_UNLOCK(sc);
	if_rele(ifp);

	return (error);
}

static int
carp_get_vhid(struct ifaddr *ifa)
{

	if (ifa == NULL || ifa->ifa_carp == NULL)
		return (0);

	return (ifa->ifa_carp->sc_vhid);
}

int
carp_attach(struct ifaddr *ifa, int vhid)
{
	struct ifnet *ifp = ifa->ifa_ifp;
	struct carp_if *cif = ifp->if_carp;
	struct carp_softc *sc;
	int index, error;

	KASSERT(ifa->ifa_carp == NULL, ("%s: ifa %p attached", __func__, ifa));

	switch (ifa->ifa_addr->sa_family) {
#ifdef INET
	case AF_INET:
#endif
#ifdef INET6
	case AF_INET6:
#endif
		break;
	default:
		return (EPROTOTYPE);
	}

	sx_xlock(&carp_sx);
	if (ifp->if_carp == NULL) {
		sx_xunlock(&carp_sx);
		return (ENOPROTOOPT);
	}

	IFNET_FOREACH_CARP(ifp, sc)
		if (sc->sc_vhid == vhid)
			break;
	if (sc == NULL) {
		sx_xunlock(&carp_sx);
		return (ENOENT);
	}

	error = carp_multicast_setup(cif, ifa->ifa_addr->sa_family);
	if (error) {
		CIF_FREE(cif);
		sx_xunlock(&carp_sx);
		return (error);
	}

	index = sc->sc_naddrs + sc->sc_naddrs6 + 1;
	if (index > sc->sc_ifasiz / sizeof(struct ifaddr *))
		carp_grow_ifas(sc);

	switch (ifa->ifa_addr->sa_family) {
#ifdef INET
	case AF_INET:
		cif->cif_naddrs++;
		sc->sc_naddrs++;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		cif->cif_naddrs6++;
		sc->sc_naddrs6++;
		break;
#endif
	}

	ifa_ref(ifa);

	CARP_LOCK(sc);
	sc->sc_ifas[index - 1] = ifa;
	ifa->ifa_carp = sc;
	carp_hmac_prepare(sc);
	carp_sc_state(sc);
	CARP_UNLOCK(sc);

	sx_xunlock(&carp_sx);

	return (0);
}

void
carp_detach(struct ifaddr *ifa, bool keep_cif)
{
	struct ifnet *ifp = ifa->ifa_ifp;
	struct carp_if *cif = ifp->if_carp;
	struct carp_softc *sc = ifa->ifa_carp;
	int i, index;

	KASSERT(sc != NULL, ("%s: %p not attached", __func__, ifa));

	sx_xlock(&carp_sx);

	CARP_LOCK(sc);
	/* Shift array. */
	index = sc->sc_naddrs + sc->sc_naddrs6;
	for (i = 0; i < index; i++)
		if (sc->sc_ifas[i] == ifa)
			break;
	KASSERT(i < index, ("%s: %p no backref", __func__, ifa));
	for (; i < index - 1; i++)
		sc->sc_ifas[i] = sc->sc_ifas[i+1];
	sc->sc_ifas[index - 1] = NULL;

	switch (ifa->ifa_addr->sa_family) {
#ifdef INET
	case AF_INET:
		cif->cif_naddrs--;
		sc->sc_naddrs--;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		cif->cif_naddrs6--;
		sc->sc_naddrs6--;
		break;
#endif
	}

	carp_ifa_delroute(ifa);
	carp_multicast_cleanup(cif, ifa->ifa_addr->sa_family);

	ifa->ifa_carp = NULL;
	ifa_free(ifa);

	carp_hmac_prepare(sc);
	carp_sc_state(sc);

	if (!keep_cif && sc->sc_naddrs == 0 && sc->sc_naddrs6 == 0)
		carp_destroy(sc);
	else
		CARP_UNLOCK(sc);

	if (!keep_cif)
		CIF_FREE(cif);

	sx_xunlock(&carp_sx);
}

static void
carp_set_state(struct carp_softc *sc, int state, const char *reason)
{

	CARP_LOCK_ASSERT(sc);

	if (sc->sc_state != state) {
		const char *carp_states[] = { CARP_STATES };
		char subsys[IFNAMSIZ+5];

		snprintf(subsys, IFNAMSIZ+5, "%u@%s", sc->sc_vhid,
		    sc->sc_carpdev->if_xname);

		CARP_LOG("%s: %s -> %s (%s)\n", subsys,
		    carp_states[sc->sc_state], carp_states[state], reason);

		sc->sc_state = state;

		devctl_notify("CARP", subsys, carp_states[state], NULL);
	}
}

static void
carp_linkstate(struct ifnet *ifp)
{
	struct carp_softc *sc;

	CIF_LOCK(ifp->if_carp);
	IFNET_FOREACH_CARP(ifp, sc) {
		CARP_LOCK(sc);
		carp_sc_state(sc);
		CARP_UNLOCK(sc);
	}
	CIF_UNLOCK(ifp->if_carp);
}

static void
carp_sc_state(struct carp_softc *sc)
{

	CARP_LOCK_ASSERT(sc);

	if (sc->sc_carpdev->if_link_state != LINK_STATE_UP ||
	    !(sc->sc_carpdev->if_flags & IFF_UP) ||
	    !V_carp_allow) {
		callout_stop(&sc->sc_ad_tmo);
#ifdef INET
		callout_stop(&sc->sc_md_tmo);
#endif
#ifdef INET6
		callout_stop(&sc->sc_md6_tmo);
#endif
		carp_set_state(sc, INIT, "hardware interface down");
		carp_setrun(sc, 0);
		if (!sc->sc_suppress)
			carp_demote_adj(V_carp_ifdown_adj, "interface down");
		sc->sc_suppress = 1;
	} else {
		carp_set_state(sc, INIT, "hardware interface up");
		carp_setrun(sc, 0);
		if (sc->sc_suppress)
			carp_demote_adj(-V_carp_ifdown_adj, "interface up");
		sc->sc_suppress = 0;
	}
}

static void
carp_demote_adj(int adj, char *reason)
{
	atomic_add_int(&V_carp_demotion, adj);
	CARP_LOG("demoted by %d to %d (%s)\n", adj, V_carp_demotion, reason);
	taskqueue_enqueue(taskqueue_swi, &carp_sendall_task);
}

static int
carp_allow_sysctl(SYSCTL_HANDLER_ARGS)
{
	int new, error;
	struct carp_softc *sc;

	new = V_carp_allow;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || !req->newptr)
		return (error);

	if (V_carp_allow != new) {
		V_carp_allow = new;

		mtx_lock(&carp_mtx);
		LIST_FOREACH(sc, &carp_list, sc_next) {
			CARP_LOCK(sc);
			if (curvnet == sc->sc_carpdev->if_vnet)
				carp_sc_state(sc);
			CARP_UNLOCK(sc);
		}
		mtx_unlock(&carp_mtx);
	}

	return (0);
}

static int
carp_dscp_sysctl(SYSCTL_HANDLER_ARGS)
{
	int new, error;

	new = V_carp_dscp;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || !req->newptr)
		return (error);

	if (new < 0 || new > 63)
		return (EINVAL);

	V_carp_dscp = new;

	return (0);
}

static int
carp_demote_adj_sysctl(SYSCTL_HANDLER_ARGS)
{
	int new, error;

	new = V_carp_demotion;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || !req->newptr)
		return (error);

	carp_demote_adj(new, "sysctl");

	return (0);
}

#ifdef INET
extern  struct domain inetdomain;
static struct protosw in_carp_protosw = {
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inetdomain,
	.pr_protocol =		IPPROTO_CARP,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		carp_input,
	.pr_output =		rip_output,
	.pr_ctloutput =		rip_ctloutput,
	.pr_usrreqs =		&rip_usrreqs
};
#endif

#ifdef INET6
extern	struct domain inet6domain;
static struct protosw in6_carp_protosw = {
	.pr_type =		SOCK_RAW,
	.pr_domain =		&inet6domain,
	.pr_protocol =		IPPROTO_CARP,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_input =		carp6_input,
	.pr_output =		rip6_output,
	.pr_ctloutput =		rip6_ctloutput,
	.pr_usrreqs =		&rip6_usrreqs
};
#endif

#ifdef VIMAGE
#if defined(__i386__)
/*
 * XXX This is a hack to work around an absolute relocation outside
 * set_vnet by one (on the stop symbol) for carpstats.  Add a dummy variable
 * to the end of the file in the hope that the linker will just keep the
 * order (as it seems to do at the moment).  It is understood to be fragile.
 * See PR 230857 for a longer discussion of the problem and the referenced
 * review for possible alternate solutions.  Each is a hack; we just need
 * the least intrusive one for the next release.
 */
VNET_DEFINE(char, carp_zzz) = 0xde;
#endif
#endif

static void
carp_mod_cleanup(void)
{

#ifdef INET
	if (proto_reg[CARP_INET] == 0) {
		(void)ipproto_unregister(IPPROTO_CARP);
		pf_proto_unregister(PF_INET, IPPROTO_CARP, SOCK_RAW);
		proto_reg[CARP_INET] = -1;
	}
	carp_iamatch_p = NULL;
#endif
#ifdef INET6
	if (proto_reg[CARP_INET6] == 0) {
		(void)ip6proto_unregister(IPPROTO_CARP);
		pf_proto_unregister(PF_INET6, IPPROTO_CARP, SOCK_RAW);
		proto_reg[CARP_INET6] = -1;
	}
	carp_iamatch6_p = NULL;
	carp_macmatch6_p = NULL;
#endif
	carp_ioctl_p = NULL;
	carp_attach_p = NULL;
	carp_detach_p = NULL;
	carp_get_vhid_p = NULL;
	carp_linkstate_p = NULL;
	carp_forus_p = NULL;
	carp_output_p = NULL;
	carp_demote_adj_p = NULL;
	carp_master_p = NULL;
	mtx_unlock(&carp_mtx);
	taskqueue_drain(taskqueue_swi, &carp_sendall_task);
	mtx_destroy(&carp_mtx);
	sx_destroy(&carp_sx);
}

static int
carp_mod_load(void)
{
	int err;

	mtx_init(&carp_mtx, "carp_mtx", NULL, MTX_DEF);
	sx_init(&carp_sx, "carp_sx");
	LIST_INIT(&carp_list);
	carp_get_vhid_p = carp_get_vhid;
	carp_forus_p = carp_forus;
	carp_output_p = carp_output;
	carp_linkstate_p = carp_linkstate;
	carp_ioctl_p = carp_ioctl;
	carp_attach_p = carp_attach;
	carp_detach_p = carp_detach;
	carp_demote_adj_p = carp_demote_adj;
	carp_master_p = carp_master;
#ifdef INET6
	carp_iamatch6_p = carp_iamatch6;
	carp_macmatch6_p = carp_macmatch6;
	proto_reg[CARP_INET6] = pf_proto_register(PF_INET6,
	    (struct protosw *)&in6_carp_protosw);
	if (proto_reg[CARP_INET6]) {
		printf("carp: error %d attaching to PF_INET6\n",
		    proto_reg[CARP_INET6]);
		carp_mod_cleanup();
		return (proto_reg[CARP_INET6]);
	}
	err = ip6proto_register(IPPROTO_CARP);
	if (err) {
		printf("carp: error %d registering with INET6\n", err);
		carp_mod_cleanup();
		return (err);
	}
#endif
#ifdef INET
	carp_iamatch_p = carp_iamatch;
	proto_reg[CARP_INET] = pf_proto_register(PF_INET, &in_carp_protosw);
	if (proto_reg[CARP_INET]) {
		printf("carp: error %d attaching to PF_INET\n",
		    proto_reg[CARP_INET]);
		carp_mod_cleanup();
		return (proto_reg[CARP_INET]);
	}
	err = ipproto_register(IPPROTO_CARP);
	if (err) {
		printf("carp: error %d registering with INET\n", err);
		carp_mod_cleanup();
		return (err);
	}
#endif
	return (0);
}

static int
carp_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		return carp_mod_load();
		/* NOTREACHED */
	case MOD_UNLOAD:
		mtx_lock(&carp_mtx);
		if (LIST_EMPTY(&carp_list))
			carp_mod_cleanup();
		else {
			mtx_unlock(&carp_mtx);
			return (EBUSY);
		}
		break;

	default:
		return (EINVAL);
	}

	return (0);
}

static moduledata_t carp_mod = {
	"carp",
	carp_modevent,
	0
};

DECLARE_MODULE(carp, carp_mod, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY);

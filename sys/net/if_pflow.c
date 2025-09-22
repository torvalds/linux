/*	$OpenBSD: if_pflow.c,v 1.111 2025/07/07 02:28:50 jsg Exp $	*/

/*
 * Copyright (c) 2011 Florian Obser <florian@narrans.de>
 * Copyright (c) 2011 Sebastian Benoit <benoit-lists@fb12.de>
 * Copyright (c) 2008 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2008 Joerg Goltermann <jg@osn.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <sys/ioctl.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/mutex.h>

#include <net/if.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>

#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/icmp6.h>

#include <net/pfvar.h>
#include <net/pfvar_priv.h>
#include <net/if_pflow.h>

#define PFLOW_MINMTU	\
    (sizeof(struct pflow_header) + sizeof(struct pflow_flow))

#ifdef PFLOWDEBUG
#define DPRINTF(x)	do { printf x ; } while (0)
#else
#define DPRINTF(x)
#endif

SMR_SLIST_HEAD(, pflow_softc) pflowif_list;

enum pflowstat_counters {
	pflow_flows,
	pflow_packets,
	pflow_onomem,
	pflow_oerrors,
	pflow_ncounters,
};

struct cpumem *pflow_counters;

static inline void
pflowstat_inc(enum pflowstat_counters c)
{
	counters_inc(pflow_counters, c);
}

void	pflowattach(int);
int	pflow_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	struct rtentry *rt);
void	pflow_output_process(void *);
int	pflow_clone_create(struct if_clone *, int);
int	pflow_clone_destroy(struct ifnet *);
int	pflow_set(struct pflow_softc *, struct pflowreq *);
int	pflow_calc_mtu(struct pflow_softc *, int, int);
void	pflow_setmtu(struct pflow_softc *, int);
int	pflowvalidsockaddr(const struct sockaddr *, int);
int	pflowioctl(struct ifnet *, u_long, caddr_t);

struct mbuf	*pflow_get_mbuf(struct pflow_softc *, u_int16_t);
void	pflow_flush(struct pflow_softc *);
int	pflow_sendout_v5(struct pflow_softc *);
int	pflow_sendout_ipfix(struct pflow_softc *, sa_family_t);
int	pflow_sendout_ipfix_tmpl(struct pflow_softc *);
int	pflow_sendout_mbuf(struct pflow_softc *, struct mbuf *);
void	pflow_timeout(void *);
void	pflow_timeout6(void *);
void	pflow_timeout_tmpl(void *);
void	copy_flow_data(struct pflow_flow *, struct pflow_flow *,
	struct pf_state *, struct pf_state_key *, int, int);
void	copy_flow_ipfix_4_data(struct pflow_ipfix_flow4 *,
	struct pflow_ipfix_flow4 *, struct pf_state *, struct pf_state_key *,
	struct pflow_softc *, int, int);
void	copy_flow_ipfix_6_data(struct pflow_ipfix_flow6 *,
	struct pflow_ipfix_flow6 *, struct pf_state *, struct pf_state_key *,
	struct pflow_softc *, int, int);
int	pflow_pack_flow(struct pf_state *, struct pf_state_key *,
	struct pflow_softc *);
int	pflow_pack_flow_ipfix(struct pf_state *, struct pf_state_key *,
	struct pflow_softc *);
int	export_pflow_if(struct pf_state*, struct pf_state_key *,
	struct pflow_softc *);
int	copy_flow_to_m(struct pflow_flow *flow, struct pflow_softc *sc);
int	copy_flow_ipfix_4_to_m(struct pflow_ipfix_flow4 *flow,
	struct pflow_softc *sc);
int	copy_flow_ipfix_6_to_m(struct pflow_ipfix_flow6 *flow,
	struct pflow_softc *sc);

struct if_clone	pflow_cloner =
    IF_CLONE_INITIALIZER("pflow", pflow_clone_create,
    pflow_clone_destroy);

void
pflowattach(int npflow)
{
	SMR_SLIST_INIT(&pflowif_list);
	pflow_counters = counters_alloc(pflow_ncounters);
	if_clone_attach(&pflow_cloner);
}

int
pflow_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	struct rtentry *rt)
{
	m_freem(m);	/* drop packet */
	return (EAFNOSUPPORT);
}

void
pflow_output_process(void *arg)
{
	struct mbuf_list ml;
	struct pflow_softc *sc = arg;
	struct mbuf *m;

	mq_delist(&sc->sc_outputqueue, &ml);
	rw_enter_read(&sc->sc_lock);
	while ((m = ml_dequeue(&ml)) != NULL) {
		pflow_sendout_mbuf(sc, m);
	}
	rw_exit_read(&sc->sc_lock);
}

int
pflow_clone_create(struct if_clone *ifc, int unit)
{
	struct ifnet		*ifp;
	struct pflow_softc	*pflowif;

	pflowif = malloc(sizeof(*pflowif), M_DEVBUF, M_WAITOK|M_ZERO);
	rw_init(&pflowif->sc_lock, "pflowlk");
	mtx_init(&pflowif->sc_mtx, IPL_MPFLOOR);
	MGET(pflowif->send_nam, M_WAIT, MT_SONAME);
	pflowif->sc_version = PFLOW_PROTO_DEFAULT;

	/* ipfix template init */
	bzero(&pflowif->sc_tmpl_ipfix,sizeof(pflowif->sc_tmpl_ipfix));
	pflowif->sc_tmpl_ipfix.set_header.set_id =
	    htons(PFLOW_IPFIX_TMPL_SET_ID);
	pflowif->sc_tmpl_ipfix.set_header.set_length =
	    htons(sizeof(struct pflow_ipfix_tmpl));

	/* ipfix IPv4 template */
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.h.tmpl_id =
	    htons(PFLOW_IPFIX_TMPL_IPV4_ID);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.h.field_count
	    = htons(PFLOW_IPFIX_TMPL_IPV4_FIELD_COUNT);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.src_ip.field_id =
	    htons(PFIX_IE_sourceIPv4Address);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.src_ip.len = htons(4);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.dest_ip.field_id =
	    htons(PFIX_IE_destinationIPv4Address);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.dest_ip.len = htons(4);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.if_index_in.field_id =
	    htons(PFIX_IE_ingressInterface);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.if_index_in.len = htons(4);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.if_index_out.field_id =
	    htons(PFIX_IE_egressInterface);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.if_index_out.len = htons(4);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.packets.field_id =
	    htons(PFIX_IE_packetDeltaCount);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.packets.len = htons(8);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.octets.field_id =
	    htons(PFIX_IE_octetDeltaCount);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.octets.len = htons(8);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.start.field_id =
	    htons(PFIX_IE_flowStartMilliseconds);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.start.len = htons(8);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.finish.field_id =
	    htons(PFIX_IE_flowEndMilliseconds);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.finish.len = htons(8);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.src_port.field_id =
	    htons(PFIX_IE_sourceTransportPort);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.src_port.len = htons(2);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.dest_port.field_id =
	    htons(PFIX_IE_destinationTransportPort);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.dest_port.len = htons(2);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.tos.field_id =
	    htons(PFIX_IE_ipClassOfService);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.tos.len = htons(1);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.protocol.field_id =
	    htons(PFIX_IE_protocolIdentifier);
	pflowif->sc_tmpl_ipfix.ipv4_tmpl.protocol.len = htons(1);

	/* ipfix IPv6 template */
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.h.tmpl_id =
	    htons(PFLOW_IPFIX_TMPL_IPV6_ID);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.h.field_count =
	    htons(PFLOW_IPFIX_TMPL_IPV6_FIELD_COUNT);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.src_ip.field_id =
	    htons(PFIX_IE_sourceIPv6Address);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.src_ip.len = htons(16);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.dest_ip.field_id =
	    htons(PFIX_IE_destinationIPv6Address);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.dest_ip.len = htons(16);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.if_index_in.field_id =
	    htons(PFIX_IE_ingressInterface);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.if_index_in.len = htons(4);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.if_index_out.field_id =
	    htons(PFIX_IE_egressInterface);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.if_index_out.len = htons(4);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.packets.field_id =
	    htons(PFIX_IE_packetDeltaCount);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.packets.len = htons(8);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.octets.field_id =
	    htons(PFIX_IE_octetDeltaCount);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.octets.len = htons(8);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.start.field_id =
	    htons(PFIX_IE_flowStartMilliseconds);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.start.len = htons(8);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.finish.field_id =
	    htons(PFIX_IE_flowEndMilliseconds);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.finish.len = htons(8);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.src_port.field_id =
	    htons(PFIX_IE_sourceTransportPort);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.src_port.len = htons(2);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.dest_port.field_id =
	    htons(PFIX_IE_destinationTransportPort);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.dest_port.len = htons(2);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.tos.field_id =
	    htons(PFIX_IE_ipClassOfService);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.tos.len = htons(1);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.protocol.field_id =
	    htons(PFIX_IE_protocolIdentifier);
	pflowif->sc_tmpl_ipfix.ipv6_tmpl.protocol.len = htons(1);

	ifp = &pflowif->sc_if;
	snprintf(ifp->if_xname, sizeof ifp->if_xname, "pflow%d", unit);
	ifp->if_softc = pflowif;
	ifp->if_ioctl = pflowioctl;
	ifp->if_output = pflow_output;
	ifp->if_start = NULL;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_type = IFT_PFLOW;
	ifp->if_hdrlen = PFLOW_HDRLEN;
	ifp->if_flags = IFF_UP;
	ifp->if_flags &= ~IFF_RUNNING;	/* not running, need receiver */
	mq_init(&pflowif->sc_outputqueue, 8192, IPL_SOFTNET);
	pflow_setmtu(pflowif, ETHERMTU);

	timeout_set_proc(&pflowif->sc_tmo, pflow_timeout, pflowif);
	timeout_set_proc(&pflowif->sc_tmo6, pflow_timeout6, pflowif);
	timeout_set_proc(&pflowif->sc_tmo_tmpl, pflow_timeout_tmpl, pflowif);

	task_set(&pflowif->sc_outputtask, pflow_output_process, pflowif);

	if_counters_alloc(ifp);
	if_attach(ifp);
	if_alloc_sadl(ifp);

	/* Insert into list of pflows */
	KERNEL_ASSERT_LOCKED();
	SMR_SLIST_INSERT_HEAD_LOCKED(&pflowif_list, pflowif, sc_next);
	return (0);
}

int
pflow_clone_destroy(struct ifnet *ifp)
{
	struct pflow_softc	*sc = ifp->if_softc;
	int			 error;

	error = 0;

	rw_enter_write(&sc->sc_lock);
	sc->sc_dying = 1;
	rw_exit_write(&sc->sc_lock);

	KERNEL_ASSERT_LOCKED();
	SMR_SLIST_REMOVE_LOCKED(&pflowif_list, sc, pflow_softc, sc_next);
	smr_barrier();

	timeout_del(&sc->sc_tmo);
	timeout_del(&sc->sc_tmo6);
	timeout_del(&sc->sc_tmo_tmpl);

	pflow_flush(sc);
	taskq_del_barrier(net_tq(ifp->if_index), &sc->sc_outputtask);
	mq_purge(&sc->sc_outputqueue);
	m_freem(sc->send_nam);
	if (sc->so != NULL) {
		error = soclose(sc->so, MSG_DONTWAIT);
		sc->so = NULL;
	}
	if (sc->sc_flowdst != NULL)
		free(sc->sc_flowdst, M_DEVBUF, sc->sc_flowdst->sa_len);
	if (sc->sc_flowsrc != NULL)
		free(sc->sc_flowsrc, M_DEVBUF, sc->sc_flowsrc->sa_len);
	if_detach(ifp);
	free(sc, M_DEVBUF, sizeof(*sc));
	return (error);
}

int
pflowvalidsockaddr(const struct sockaddr *sa, int ignore_port)
{
	struct sockaddr_in6	*sin6;
	struct sockaddr_in	*sin;

	if (sa == NULL)
		return (0);
	switch(sa->sa_family) {
	case AF_INET:
		sin = (struct sockaddr_in*) sa;
		return (sin->sin_addr.s_addr != INADDR_ANY &&
		    (ignore_port || sin->sin_port != 0));
	case AF_INET6:
		sin6 = (struct sockaddr_in6*) sa;
		return (!IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) &&
		    (ignore_port || sin6->sin6_port != 0));
	default:
		return (0);
	}
}

int
pflow_set(struct pflow_softc *sc, struct pflowreq *pflowr)
{
	struct proc		*p = curproc;
	struct socket		*so;
	struct sockaddr		*sa;
	int			 error = 0;

	if (pflowr->addrmask & PFLOW_MASK_VERSION) {
		switch(pflowr->version) {
		case PFLOW_PROTO_5:
		case PFLOW_PROTO_10:
			break;
		default:
			return(EINVAL);
		}
	}

	rw_assert_wrlock(&sc->sc_lock);

	pflow_flush(sc);

	if (pflowr->addrmask & PFLOW_MASK_DSTIP) {
		if (sc->sc_flowdst != NULL &&
		    sc->sc_flowdst->sa_family != pflowr->flowdst.ss_family) {
			free(sc->sc_flowdst, M_DEVBUF, sc->sc_flowdst->sa_len);
			sc->sc_flowdst = NULL;
			if (sc->so != NULL) {
				soclose(sc->so, MSG_DONTWAIT);
				sc->so = NULL;
			}
		}

		switch (pflowr->flowdst.ss_family) {
		case AF_INET:
			if (sc->sc_flowdst == NULL) {
				if ((sc->sc_flowdst = malloc(
				    sizeof(struct sockaddr_in),
				    M_DEVBUF,  M_NOWAIT)) == NULL)
					return (ENOMEM);
			}
			memcpy(sc->sc_flowdst, &pflowr->flowdst,
			    sizeof(struct sockaddr_in));
			sc->sc_flowdst->sa_len = sizeof(struct
			    sockaddr_in);
			break;
		case AF_INET6:
			if (sc->sc_flowdst == NULL) {
				if ((sc->sc_flowdst = malloc(
				    sizeof(struct sockaddr_in6),
				    M_DEVBUF, M_NOWAIT)) == NULL)
					return (ENOMEM);
			}
			memcpy(sc->sc_flowdst, &pflowr->flowdst,
			    sizeof(struct sockaddr_in6));
			sc->sc_flowdst->sa_len = sizeof(struct
			    sockaddr_in6);
			break;
		default:
			break;
		}

		if (sc->sc_flowdst != NULL) {
			sc->send_nam->m_len = sc->sc_flowdst->sa_len;
			sa = mtod(sc->send_nam, struct sockaddr *);
			memcpy(sa, sc->sc_flowdst, sc->sc_flowdst->sa_len);
		}
	}

	if (pflowr->addrmask & PFLOW_MASK_SRCIP) {
		if (sc->sc_flowsrc != NULL)
			free(sc->sc_flowsrc, M_DEVBUF, sc->sc_flowsrc->sa_len);
		sc->sc_flowsrc = NULL;
		if (sc->so != NULL) {
			soclose(sc->so, MSG_DONTWAIT);
			sc->so = NULL;
		}
		switch(pflowr->flowsrc.ss_family) {
		case AF_INET:
			if ((sc->sc_flowsrc = malloc(
			    sizeof(struct sockaddr_in),
			    M_DEVBUF, M_NOWAIT)) == NULL)
				return (ENOMEM);
			memcpy(sc->sc_flowsrc, &pflowr->flowsrc,
			    sizeof(struct sockaddr_in));
			sc->sc_flowsrc->sa_len = sizeof(struct
			    sockaddr_in);
			break;
		case AF_INET6:
			if ((sc->sc_flowsrc = malloc(
			    sizeof(struct sockaddr_in6),
			    M_DEVBUF, M_NOWAIT)) == NULL)
				return (ENOMEM);
			memcpy(sc->sc_flowsrc, &pflowr->flowsrc,
			    sizeof(struct sockaddr_in6));
			sc->sc_flowsrc->sa_len = sizeof(struct
			    sockaddr_in6);
			break;
		default:
			break;
		}
	}

	if (sc->so == NULL) {
		if (pflowvalidsockaddr(sc->sc_flowdst, 0)) {
			error = socreate(sc->sc_flowdst->sa_family,
			    &so, SOCK_DGRAM, 0);
			if (error)
				return (error);
			if (pflowvalidsockaddr(sc->sc_flowsrc, 1)) {
				struct mbuf *m;

				MGET(m, M_WAIT, MT_SONAME);
				m->m_len = sc->sc_flowsrc->sa_len;
				sa = mtod(m, struct sockaddr *);
				memcpy(sa, sc->sc_flowsrc,
				    sc->sc_flowsrc->sa_len);

				solock(so);
				error = sobind(so, m, p);
				sounlock(so);
				m_freem(m);
				if (error) {
					soclose(so, MSG_DONTWAIT);
					return (error);
				}
			}
			sc->so = so;
		}
	} else if (!pflowvalidsockaddr(sc->sc_flowdst, 0)) {
		soclose(sc->so, MSG_DONTWAIT);
		sc->so = NULL;
	}

	NET_LOCK();
	mtx_enter(&sc->sc_mtx);

	/* error check is above */
	if (pflowr->addrmask & PFLOW_MASK_VERSION)
		sc->sc_version = pflowr->version;

	pflow_setmtu(sc, ETHERMTU);

	switch (sc->sc_version) {
	case PFLOW_PROTO_5:
		timeout_del(&sc->sc_tmo6);
		timeout_del(&sc->sc_tmo_tmpl);
		break;
	case PFLOW_PROTO_10:
		timeout_add_sec(&sc->sc_tmo_tmpl, PFLOW_TMPL_TIMEOUT);
		break;
	default: /* NOTREACHED */
		break;
	}

	mtx_leave(&sc->sc_mtx);
	NET_UNLOCK();

	return (0);
}

int
pflowioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct proc		*p = curproc;
	struct pflow_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	struct pflowreq		 pflowr;
	int			 error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFFLAGS:
	case SIOCSIFMTU:
	case SIOCGETPFLOW:
	case SIOCSETPFLOW:
		break;
	default:
		return (ENOTTY);
	}

	/* XXXSMP: enforce lock order */
	NET_UNLOCK();
	rw_enter_write(&sc->sc_lock);

	if (sc->sc_dying) {
		error = ENXIO;
		goto out;
	}

	switch (cmd) {
	case SIOCSIFADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFFLAGS:
		NET_LOCK();
		if ((ifp->if_flags & IFF_UP) && sc->so != NULL) {
			ifp->if_flags |= IFF_RUNNING;
			mtx_enter(&sc->sc_mtx);
			/* send templates on startup */
			if (sc->sc_version == PFLOW_PROTO_10)
				pflow_sendout_ipfix_tmpl(sc);
			mtx_leave(&sc->sc_mtx);
		} else
			ifp->if_flags &= ~IFF_RUNNING;
		NET_UNLOCK();
		break;

	case SIOCSIFMTU:
		if (ifr->ifr_mtu < PFLOW_MINMTU) {
			error = EINVAL;
			goto out;
		}
		if (ifr->ifr_mtu > MCLBYTES)
			ifr->ifr_mtu = MCLBYTES;
		NET_LOCK();
		if (ifr->ifr_mtu < ifp->if_mtu)
			pflow_flush(sc);
		mtx_enter(&sc->sc_mtx);
		pflow_setmtu(sc, ifr->ifr_mtu);
		mtx_leave(&sc->sc_mtx);
		NET_UNLOCK();
		break;

	case SIOCGETPFLOW:
		bzero(&pflowr, sizeof(pflowr));

		if (sc->sc_flowsrc != NULL)
			memcpy(&pflowr.flowsrc, sc->sc_flowsrc,
			    sc->sc_flowsrc->sa_len);
		if (sc->sc_flowdst != NULL)
			memcpy(&pflowr.flowdst, sc->sc_flowdst,
			    sc->sc_flowdst->sa_len);
		mtx_enter(&sc->sc_mtx);
		pflowr.version = sc->sc_version;
		mtx_leave(&sc->sc_mtx);

		if ((error = copyout(&pflowr, ifr->ifr_data, sizeof(pflowr))))
			goto out;
		break;

	case SIOCSETPFLOW:
		if ((error = suser(p)) != 0)
			goto out;
		if ((error = copyin(ifr->ifr_data, &pflowr, sizeof(pflowr))))
			goto out;

		error = pflow_set(sc, &pflowr);
		if (error != 0)
			goto out;

		NET_LOCK();
		if ((ifp->if_flags & IFF_UP) && sc->so != NULL) {
			ifp->if_flags |= IFF_RUNNING;
			mtx_enter(&sc->sc_mtx);
			if (sc->sc_version == PFLOW_PROTO_10)
				pflow_sendout_ipfix_tmpl(sc);
			mtx_leave(&sc->sc_mtx);
		} else
			ifp->if_flags &= ~IFF_RUNNING;
		NET_UNLOCK();

		break;
	}

out:
	rw_exit_write(&sc->sc_lock);
	NET_LOCK();

	return (error);
}

int
pflow_calc_mtu(struct pflow_softc *sc, int mtu, int hdrsz)
{
	sc->sc_maxcount4 = (mtu - hdrsz -
	    sizeof(struct udpiphdr)) / sizeof(struct pflow_ipfix_flow4);
	sc->sc_maxcount6 = (mtu - hdrsz -
	    sizeof(struct udpiphdr)) / sizeof(struct pflow_ipfix_flow6);
	if (sc->sc_maxcount4 > PFLOW_MAXFLOWS)
		sc->sc_maxcount4 = PFLOW_MAXFLOWS;
	if (sc->sc_maxcount6 > PFLOW_MAXFLOWS)
		sc->sc_maxcount6 = PFLOW_MAXFLOWS;
	return (hdrsz + sizeof(struct udpiphdr) +
	    MIN(sc->sc_maxcount4 * sizeof(struct pflow_ipfix_flow4),
	    sc->sc_maxcount6 * sizeof(struct pflow_ipfix_flow6)));
}

void
pflow_setmtu(struct pflow_softc *sc, int mtu_req)
{
	int	mtu;

	mtu = mtu_req;

	switch (sc->sc_version) {
	case PFLOW_PROTO_5:
		sc->sc_maxcount = (mtu - sizeof(struct pflow_header) -
		    sizeof(struct udpiphdr)) / sizeof(struct pflow_flow);
		if (sc->sc_maxcount > PFLOW_MAXFLOWS)
		    sc->sc_maxcount = PFLOW_MAXFLOWS;
		sc->sc_if.if_mtu = sizeof(struct pflow_header) +
		    sizeof(struct udpiphdr) +
		    sc->sc_maxcount * sizeof(struct pflow_flow);
		break;
	case PFLOW_PROTO_10:
		sc->sc_if.if_mtu =
		    pflow_calc_mtu(sc, mtu, sizeof(struct pflow_v10_header));
		break;
	default: /* NOTREACHED */
		break;
	}
}

struct mbuf *
pflow_get_mbuf(struct pflow_softc *sc, u_int16_t set_id)
{
	struct pflow_set_header	 set_hdr;
	struct pflow_header	 h;
	struct mbuf		*m;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL) {
		pflowstat_inc(pflow_onomem);
		return (NULL);
	}

	MCLGET(m, M_DONTWAIT);
	if ((m->m_flags & M_EXT) == 0) {
		m_free(m);
		pflowstat_inc(pflow_onomem);
		return (NULL);
	}

	m->m_len = m->m_pkthdr.len = 0;
	m->m_pkthdr.ph_ifidx = 0;

	if (sc == NULL)		/* get only a new empty mbuf */
		return (m);

	switch (sc->sc_version) {
	case PFLOW_PROTO_5:
		/* populate pflow_header */
		h.reserved1 = 0;
		h.reserved2 = 0;
		h.count = 0;
		h.version = htons(PFLOW_PROTO_5);
		h.flow_sequence = htonl(sc->sc_gcounter);
		h.engine_type = PFLOW_ENGINE_TYPE;
		h.engine_id = PFLOW_ENGINE_ID;
		m_copyback(m, 0, PFLOW_HDRLEN, &h, M_NOWAIT);

		sc->sc_count = 0;
		timeout_add_sec(&sc->sc_tmo, PFLOW_TIMEOUT);
		break;
	case PFLOW_PROTO_10:
		/* populate pflow_set_header */
		set_hdr.set_length = 0;
		set_hdr.set_id = htons(set_id);
		m_copyback(m, 0, PFLOW_SET_HDRLEN, &set_hdr, M_NOWAIT);
		break;
	default: /* NOTREACHED */
		break;
	}

	return (m);
}

void
copy_flow_data(struct pflow_flow *flow1, struct pflow_flow *flow2,
    struct pf_state *st, struct pf_state_key *sk, int src, int dst)
{
	flow1->src_ip = flow2->dest_ip = sk->addr[src].v4.s_addr;
	flow1->src_port = flow2->dest_port = sk->port[src];
	flow1->dest_ip = flow2->src_ip = sk->addr[dst].v4.s_addr;
	flow1->dest_port = flow2->src_port = sk->port[dst];

	flow1->dest_as = flow2->src_as =
	    flow1->src_as = flow2->dest_as = 0;
	flow1->if_index_in = htons(st->if_index_in);
	flow1->if_index_out = htons(st->if_index_out);
	flow2->if_index_in = htons(st->if_index_out);
	flow2->if_index_out = htons(st->if_index_in);
	flow1->dest_mask = flow2->src_mask =
	    flow1->src_mask = flow2->dest_mask = 0;

	flow1->flow_packets = htonl(st->packets[0]);
	flow2->flow_packets = htonl(st->packets[1]);
	flow1->flow_octets = htonl(st->bytes[0]);
	flow2->flow_octets = htonl(st->bytes[1]);

	/*
	 * Pretend the flow was created or expired when the machine came up
	 * when creation is in the future of the last time a package was seen
	 * or was created / expired before this machine came up due to pfsync.
	 */
	flow1->flow_start = flow2->flow_start = st->creation < 0 ||
	    st->creation > st->expire ? htonl(0) : htonl(st->creation * 1000);
	flow1->flow_finish = flow2->flow_finish = st->expire < 0 ? htonl(0) :
	    htonl(st->expire * 1000);
	flow1->tcp_flags = flow2->tcp_flags = 0;
	flow1->protocol = flow2->protocol = sk->proto;
	flow1->tos = flow2->tos = st->rule.ptr->tos;
}

void
copy_flow_ipfix_4_data(struct pflow_ipfix_flow4 *flow1,
    struct pflow_ipfix_flow4 *flow2, struct pf_state *st,
    struct pf_state_key *sk, struct pflow_softc *sc, int src, int dst)
{
	flow1->src_ip = flow2->dest_ip = sk->addr[src].v4.s_addr;
	flow1->src_port = flow2->dest_port = sk->port[src];
	flow1->dest_ip = flow2->src_ip = sk->addr[dst].v4.s_addr;
	flow1->dest_port = flow2->src_port = sk->port[dst];

	flow1->if_index_in = htonl(st->if_index_in);
	flow1->if_index_out = htonl(st->if_index_out);
	flow2->if_index_in = htonl(st->if_index_out);
	flow2->if_index_out = htonl(st->if_index_in);

	flow1->flow_packets = htobe64(st->packets[0]);
	flow2->flow_packets = htobe64(st->packets[1]);
	flow1->flow_octets = htobe64(st->bytes[0]);
	flow2->flow_octets = htobe64(st->bytes[1]);

	/*
	 * Pretend the flow was created when the machine came up when creation
	 * is in the future of the last time a package was seen due to pfsync.
	 */
	if (st->creation > st->expire)
		flow1->flow_start = flow2->flow_start = htobe64((gettime() -
		    getuptime())*1000);
	else
		flow1->flow_start = flow2->flow_start = htobe64((gettime() -
		    (getuptime() - st->creation))*1000);
	flow1->flow_finish = flow2->flow_finish = htobe64((gettime() -
	    (getuptime() - st->expire))*1000);

	flow1->protocol = flow2->protocol = sk->proto;
	flow1->tos = flow2->tos = st->rule.ptr->tos;
}

void
copy_flow_ipfix_6_data(struct pflow_ipfix_flow6 *flow1,
    struct pflow_ipfix_flow6 *flow2, struct pf_state *st,
    struct pf_state_key *sk, struct pflow_softc *sc, int src, int dst)
{
	bcopy(&sk->addr[src].v6, &flow1->src_ip, sizeof(flow1->src_ip));
	bcopy(&sk->addr[src].v6, &flow2->dest_ip, sizeof(flow2->dest_ip));
	flow1->src_port = flow2->dest_port = sk->port[src];
	bcopy(&sk->addr[dst].v6, &flow1->dest_ip, sizeof(flow1->dest_ip));
	bcopy(&sk->addr[dst].v6, &flow2->src_ip, sizeof(flow2->src_ip));
	flow1->dest_port = flow2->src_port = sk->port[dst];

	flow1->if_index_in = htonl(st->if_index_in);
	flow1->if_index_out = htonl(st->if_index_out);
	flow2->if_index_in = htonl(st->if_index_out);
	flow2->if_index_out = htonl(st->if_index_in);

	flow1->flow_packets = htobe64(st->packets[0]);
	flow2->flow_packets = htobe64(st->packets[1]);
	flow1->flow_octets = htobe64(st->bytes[0]);
	flow2->flow_octets = htobe64(st->bytes[1]);

	/*
	 * Pretend the flow was created when the machine came up when creation
	 * is in the future of the last time a package was seen due to pfsync.
	 */
	if (st->creation > st->expire)
		flow1->flow_start = flow2->flow_start = htobe64((gettime() -
		    getuptime())*1000);
	else
		flow1->flow_start = flow2->flow_start = htobe64((gettime() -
		    (getuptime() - st->creation))*1000);
	flow1->flow_finish = flow2->flow_finish = htobe64((gettime() -
	    (getuptime() - st->expire))*1000);

	flow1->protocol = flow2->protocol = sk->proto;
	flow1->tos = flow2->tos = st->rule.ptr->tos;
}

int
export_pflow(struct pf_state *st)
{
	struct pflow_softc	*sc = NULL;
	struct pf_state_key	*sk;

	sk = st->key[st->direction == PF_IN ? PF_SK_WIRE : PF_SK_STACK];

	SMR_SLIST_FOREACH(sc, &pflowif_list, sc_next) {
		mtx_enter(&sc->sc_mtx);
		switch (sc->sc_version) {
		case PFLOW_PROTO_5:
			if (sk->af == AF_INET)
				export_pflow_if(st, sk, sc);
			break;
		case PFLOW_PROTO_10:
			if (sk->af == AF_INET || sk->af == AF_INET6)
				export_pflow_if(st, sk, sc);
			break;
		default: /* NOTREACHED */
			break;
		}
		mtx_leave(&sc->sc_mtx);
	}

	return (0);
}

int
export_pflow_if(struct pf_state *st, struct pf_state_key *sk,
    struct pflow_softc *sc)
{
	struct pf_state		 pfs_copy;
	struct ifnet		*ifp = &sc->sc_if;
	u_int64_t		 bytes[2];
	int			 ret = 0;

	if (!(ifp->if_flags & IFF_RUNNING))
		return (0);

	if (sc->sc_version == PFLOW_PROTO_10)
		return (pflow_pack_flow_ipfix(st, sk, sc));

	/* PFLOW_PROTO_5 */
	if ((st->bytes[0] < (u_int64_t)PFLOW_MAXBYTES)
	    && (st->bytes[1] < (u_int64_t)PFLOW_MAXBYTES))
		return (pflow_pack_flow(st, sk, sc));

	/* flow > PFLOW_MAXBYTES need special handling */
	bcopy(st, &pfs_copy, sizeof(pfs_copy));
	bytes[0] = pfs_copy.bytes[0];
	bytes[1] = pfs_copy.bytes[1];

	while (bytes[0] > PFLOW_MAXBYTES) {
		pfs_copy.bytes[0] = PFLOW_MAXBYTES;
		pfs_copy.bytes[1] = 0;

		if ((ret = pflow_pack_flow(&pfs_copy, sk, sc)) != 0)
			return (ret);
		if ((bytes[0] - PFLOW_MAXBYTES) > 0)
			bytes[0] -= PFLOW_MAXBYTES;
	}

	while (bytes[1] > (u_int64_t)PFLOW_MAXBYTES) {
		pfs_copy.bytes[1] = PFLOW_MAXBYTES;
		pfs_copy.bytes[0] = 0;

		if ((ret = pflow_pack_flow(&pfs_copy, sk, sc)) != 0)
			return (ret);
		if ((bytes[1] - PFLOW_MAXBYTES) > 0)
			bytes[1] -= PFLOW_MAXBYTES;
	}

	pfs_copy.bytes[0] = bytes[0];
	pfs_copy.bytes[1] = bytes[1];

	return (pflow_pack_flow(&pfs_copy, sk, sc));
}

int
copy_flow_to_m(struct pflow_flow *flow, struct pflow_softc *sc)
{
	int		ret = 0;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	if (sc->sc_mbuf == NULL) {
		if ((sc->sc_mbuf = pflow_get_mbuf(sc, 0)) == NULL)
			return (ENOBUFS);
	}
	m_copyback(sc->sc_mbuf, PFLOW_HDRLEN +
	    (sc->sc_count * sizeof(struct pflow_flow)),
	    sizeof(struct pflow_flow), flow, M_NOWAIT);

	pflowstat_inc(pflow_flows);
	sc->sc_gcounter++;
	sc->sc_count++;

	if (sc->sc_count >= sc->sc_maxcount)
		ret = pflow_sendout_v5(sc);

	return(ret);
}

int
copy_flow_ipfix_4_to_m(struct pflow_ipfix_flow4 *flow, struct pflow_softc *sc)
{
	int		ret = 0;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	if (sc->sc_mbuf == NULL) {
		if ((sc->sc_mbuf =
		    pflow_get_mbuf(sc, PFLOW_IPFIX_TMPL_IPV4_ID)) == NULL) {
			return (ENOBUFS);
		}
		sc->sc_count4 = 0;
		timeout_add_sec(&sc->sc_tmo, PFLOW_TIMEOUT);
	}
	m_copyback(sc->sc_mbuf, PFLOW_SET_HDRLEN +
	    (sc->sc_count4 * sizeof(struct pflow_ipfix_flow4)),
	    sizeof(struct pflow_ipfix_flow4), flow, M_NOWAIT);

	pflowstat_inc(pflow_flows);
	sc->sc_gcounter++;
	sc->sc_count4++;

	if (sc->sc_count4 >= sc->sc_maxcount4)
		ret = pflow_sendout_ipfix(sc, AF_INET);
	return(ret);
}

int
copy_flow_ipfix_6_to_m(struct pflow_ipfix_flow6 *flow, struct pflow_softc *sc)
{
	int		ret = 0;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	if (sc->sc_mbuf6 == NULL) {
		if ((sc->sc_mbuf6 =
		    pflow_get_mbuf(sc, PFLOW_IPFIX_TMPL_IPV6_ID)) == NULL) {
			return (ENOBUFS);
		}
		sc->sc_count6 = 0;
		timeout_add_sec(&sc->sc_tmo6, PFLOW_TIMEOUT);
	}
	m_copyback(sc->sc_mbuf6, PFLOW_SET_HDRLEN +
	    (sc->sc_count6 * sizeof(struct pflow_ipfix_flow6)),
	    sizeof(struct pflow_ipfix_flow6), flow, M_NOWAIT);

	pflowstat_inc(pflow_flows);
	sc->sc_gcounter++;
	sc->sc_count6++;

	if (sc->sc_count6 >= sc->sc_maxcount6)
		ret = pflow_sendout_ipfix(sc, AF_INET6);

	return(ret);
}

int
pflow_pack_flow(struct pf_state *st, struct pf_state_key *sk,
    struct pflow_softc *sc)
{
	struct pflow_flow	 flow1;
	struct pflow_flow	 flow2;
	int			 ret = 0;

	bzero(&flow1, sizeof(flow1));
	bzero(&flow2, sizeof(flow2));

	if (st->direction == PF_OUT)
		copy_flow_data(&flow1, &flow2, st, sk, 1, 0);
	else
		copy_flow_data(&flow1, &flow2, st, sk, 0, 1);

	if (st->bytes[0] != 0) /* first flow from state */
		ret = copy_flow_to_m(&flow1, sc);

	if (st->bytes[1] != 0) /* second flow from state */
		ret = copy_flow_to_m(&flow2, sc);

	return (ret);
}

int
pflow_pack_flow_ipfix(struct pf_state *st, struct pf_state_key *sk,
    struct pflow_softc *sc)
{
	struct pflow_ipfix_flow4	 flow4_1, flow4_2;
	struct pflow_ipfix_flow6	 flow6_1, flow6_2;
	int				 ret = 0;
	if (sk->af == AF_INET) {
		bzero(&flow4_1, sizeof(flow4_1));
		bzero(&flow4_2, sizeof(flow4_2));

		if (st->direction == PF_OUT)
			copy_flow_ipfix_4_data(&flow4_1, &flow4_2, st, sk, sc,
			    1, 0);
		else
			copy_flow_ipfix_4_data(&flow4_1, &flow4_2, st, sk, sc,
			    0, 1);

		if (st->bytes[0] != 0) /* first flow from state */
			ret = copy_flow_ipfix_4_to_m(&flow4_1, sc);

		if (st->bytes[1] != 0) /* second flow from state */
			ret = copy_flow_ipfix_4_to_m(&flow4_2, sc);
	} else if (sk->af == AF_INET6) {
		bzero(&flow6_1, sizeof(flow6_1));
		bzero(&flow6_2, sizeof(flow6_2));

		if (st->direction == PF_OUT)
			copy_flow_ipfix_6_data(&flow6_1, &flow6_2, st, sk, sc,
			    1, 0);
		else
			copy_flow_ipfix_6_data(&flow6_1, &flow6_2, st, sk, sc,
			    0, 1);

		if (st->bytes[0] != 0) /* first flow from state */
			ret = copy_flow_ipfix_6_to_m(&flow6_1, sc);

		if (st->bytes[1] != 0) /* second flow from state */
			ret = copy_flow_ipfix_6_to_m(&flow6_2, sc);
	}
	return (ret);
}

void
pflow_timeout(void *v)
{
	struct pflow_softc	*sc = v;

	mtx_enter(&sc->sc_mtx);
	switch (sc->sc_version) {
	case PFLOW_PROTO_5:
		pflow_sendout_v5(sc);
		break;
	case PFLOW_PROTO_10:
		pflow_sendout_ipfix(sc, AF_INET);
		break;
	default: /* NOTREACHED */
		break;
	}
	mtx_leave(&sc->sc_mtx);
}

void
pflow_timeout6(void *v)
{
	struct pflow_softc	*sc = v;

	mtx_enter(&sc->sc_mtx);
	pflow_sendout_ipfix(sc, AF_INET6);
	mtx_leave(&sc->sc_mtx);
}

void
pflow_timeout_tmpl(void *v)
{
	struct pflow_softc	*sc = v;

	mtx_enter(&sc->sc_mtx);
	pflow_sendout_ipfix_tmpl(sc);
	mtx_leave(&sc->sc_mtx);
}

void
pflow_flush(struct pflow_softc *sc)
{
	mtx_enter(&sc->sc_mtx);
	switch (sc->sc_version) {
	case PFLOW_PROTO_5:
		pflow_sendout_v5(sc);
		break;
	case PFLOW_PROTO_10:
		pflow_sendout_ipfix(sc, AF_INET);
		pflow_sendout_ipfix(sc, AF_INET6);
		break;
	default: /* NOTREACHED */
		break;
	}
	mtx_leave(&sc->sc_mtx);
}

int
pflow_sendout_v5(struct pflow_softc *sc)
{
	struct mbuf		*m = sc->sc_mbuf;
	struct pflow_header	*h;
	struct ifnet		*ifp = &sc->sc_if;
	struct timespec		tv;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	timeout_del(&sc->sc_tmo);

	if (m == NULL)
		return (0);

	sc->sc_mbuf = NULL;
	if (!(ifp->if_flags & IFF_RUNNING)) {
		m_freem(m);
		return (0);
	}

	pflowstat_inc(pflow_packets);
	h = mtod(m, struct pflow_header *);
	h->count = htons(sc->sc_count);

	/* populate pflow_header */
	h->uptime_ms = htonl(getuptime() * 1000);

	getnanotime(&tv);
	h->time_sec = htonl(tv.tv_sec);			/* XXX 2038 */
	h->time_nanosec = htonl(tv.tv_nsec);
	if (mq_enqueue(&sc->sc_outputqueue, m) == 0)
		task_add(net_tq(ifp->if_index), &sc->sc_outputtask);
	return (0);
}

int
pflow_sendout_ipfix(struct pflow_softc *sc, sa_family_t af)
{
	struct mbuf			*m;
	struct pflow_v10_header		*h10;
	struct pflow_set_header		*set_hdr;
	struct ifnet			*ifp = &sc->sc_if;
	u_int32_t			 count;
	int				 set_length;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	switch (af) {
	case AF_INET:
		m = sc->sc_mbuf;
		timeout_del(&sc->sc_tmo);
		if (m == NULL)
			return (0);
		sc->sc_mbuf = NULL;
		count = sc->sc_count4;
		set_length = sizeof(struct pflow_set_header)
		    + sc->sc_count4 * sizeof(struct pflow_ipfix_flow4);
		break;
	case AF_INET6:
		m = sc->sc_mbuf6;
		timeout_del(&sc->sc_tmo6);
		if (m == NULL)
			return (0);
		sc->sc_mbuf6 = NULL;
		count = sc->sc_count6;
		set_length = sizeof(struct pflow_set_header)
		    + sc->sc_count6 * sizeof(struct pflow_ipfix_flow6);
		break;
	default:
		unhandled_af(af);
	}

	if (!(ifp->if_flags & IFF_RUNNING)) {
		m_freem(m);
		return (0);
	}

	pflowstat_inc(pflow_packets);
	set_hdr = mtod(m, struct pflow_set_header *);
	set_hdr->set_length = htons(set_length);

	/* populate pflow_header */
	M_PREPEND(m, sizeof(struct pflow_v10_header), M_DONTWAIT);
	if (m == NULL) {
		pflowstat_inc(pflow_onomem);
		return (ENOBUFS);
	}
	h10 = mtod(m, struct pflow_v10_header *);
	h10->version = htons(PFLOW_PROTO_10);
	h10->length = htons(PFLOW_IPFIX_HDRLEN + set_length);
	h10->time_sec = htonl(gettime());		/* XXX 2038 */
	h10->flow_sequence = htonl(sc->sc_sequence);
	sc->sc_sequence += count;
	h10->observation_dom = htonl(PFLOW_ENGINE_TYPE);
	if (mq_enqueue(&sc->sc_outputqueue, m) == 0)
		task_add(net_tq(ifp->if_index), &sc->sc_outputtask);
	return (0);
}

int
pflow_sendout_ipfix_tmpl(struct pflow_softc *sc)
{
	struct mbuf			*m;
	struct pflow_v10_header		*h10;
	struct ifnet			*ifp = &sc->sc_if;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	timeout_del(&sc->sc_tmo_tmpl);

	if (!(ifp->if_flags & IFF_RUNNING)) {
		return (0);
	}
	m = pflow_get_mbuf(sc, 0);
	if (m == NULL)
		return (0);
	if (m_copyback(m, 0, sizeof(struct pflow_ipfix_tmpl),
	    &sc->sc_tmpl_ipfix, M_NOWAIT)) {
		m_freem(m);
		return (0);
	}
	pflowstat_inc(pflow_packets);

	/* populate pflow_header */
	M_PREPEND(m, sizeof(struct pflow_v10_header), M_DONTWAIT);
	if (m == NULL) {
		pflowstat_inc(pflow_onomem);
		return (ENOBUFS);
	}
	h10 = mtod(m, struct pflow_v10_header *);
	h10->version = htons(PFLOW_PROTO_10);
	h10->length = htons(PFLOW_IPFIX_HDRLEN + sizeof(struct
	    pflow_ipfix_tmpl));
	h10->time_sec = htonl(gettime());		/* XXX 2038 */
	h10->flow_sequence = htonl(sc->sc_sequence);
	h10->observation_dom = htonl(PFLOW_ENGINE_TYPE);

	timeout_add_sec(&sc->sc_tmo_tmpl, PFLOW_TMPL_TIMEOUT);
	if (mq_enqueue(&sc->sc_outputqueue, m) == 0)
		task_add(net_tq(ifp->if_index), &sc->sc_outputtask);
	return (0);
}

int
pflow_sendout_mbuf(struct pflow_softc *sc, struct mbuf *m)
{
	rw_assert_anylock(&sc->sc_lock);

	counters_pkt(sc->sc_if.if_counters,
	            ifc_opackets, ifc_obytes, m->m_pkthdr.len);

	if (sc->so == NULL) {
		m_freem(m);
		return (EINVAL);
	}
	return (sosend(sc->so, sc->send_nam, NULL, m, NULL, 0));
}

int
pflow_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case NET_PFLOW_STATS: {
		uint64_t counters[pflow_ncounters];
		struct pflowstats pflowstats;

		if (newp != NULL)
			return (EPERM);

		counters_read(pflow_counters, counters, pflow_ncounters, NULL);

		pflowstats.pflow_flows = counters[pflow_flows];
		pflowstats.pflow_packets = counters[pflow_packets];
		pflowstats.pflow_onomem = counters[pflow_onomem];
		pflowstats.pflow_oerrors = counters[pflow_oerrors];

		return (sysctl_struct(oldp, oldlenp, newp, newlen,
		    &pflowstats, sizeof(pflowstats)));
	}
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

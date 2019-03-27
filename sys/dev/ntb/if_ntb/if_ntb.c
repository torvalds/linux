/*-
 * Copyright (c) 2016 Alexander Motin <mav@FreeBSD.org>
 * Copyright (C) 2013 Intel Corporation
 * Copyright (C) 2015 EMC Corporation
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

/*
 * The Non-Transparent Bridge (NTB) is a device that allows you to connect
 * two or more systems using a PCI-e links, providing remote memory access.
 *
 * This module contains a driver for simulated Ethernet device, using
 * underlying NTB Transport device.
 *
 * NOTE: Much of the code in this module is shared with Linux. Any patches may
 * be picked up and redistributed in Linux with a dual GPL/BSD license.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/buf_ring.h>
#include <sys/bus.h>
#include <sys/limits.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/if_var.h>
#include <net/bpf.h>
#include <net/ethernet.h>

#include <machine/bus.h>

#include "../ntb_transport.h"

#define KTR_NTB KTR_SPARE3
#define NTB_MEDIATYPE		 (IFM_ETHER | IFM_AUTO | IFM_FDX)

#define	NTB_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP | CSUM_SCTP)
#define	NTB_CSUM_FEATURES6	(CSUM_TCP_IPV6 | CSUM_UDP_IPV6 | CSUM_SCTP_IPV6)
#define	NTB_CSUM_SET		(CSUM_DATA_VALID | CSUM_DATA_VALID_IPV6 | \
				    CSUM_PSEUDO_HDR | \
				    CSUM_IP_CHECKED | CSUM_IP_VALID | \
				    CSUM_SCTP_VALID)

static SYSCTL_NODE(_hw, OID_AUTO, if_ntb, CTLFLAG_RW, 0, "if_ntb");

static unsigned g_if_ntb_num_queues = UINT_MAX;
SYSCTL_UINT(_hw_if_ntb, OID_AUTO, num_queues, CTLFLAG_RWTUN,
    &g_if_ntb_num_queues, 0, "Number of queues per interface");

struct ntb_net_queue {
	struct ntb_net_ctx	*sc;
	if_t			 ifp;
	struct ntb_transport_qp *qp;
	struct buf_ring		*br;
	struct task		 tx_task;
	struct taskqueue	*tx_tq;
	struct mtx		 tx_lock;
	struct callout		 queue_full;
};

struct ntb_net_ctx {
	if_t			 ifp;
	struct ifmedia		 media;
	u_char			 eaddr[ETHER_ADDR_LEN];
	int			 num_queues;
	struct ntb_net_queue	*queues;
	int			 mtu;
};

static int ntb_net_probe(device_t dev);
static int ntb_net_attach(device_t dev);
static int ntb_net_detach(device_t dev);
static void ntb_net_init(void *arg);
static int ntb_ifmedia_upd(struct ifnet *);
static void ntb_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int ntb_ioctl(if_t ifp, u_long command, caddr_t data);
static int ntb_transmit(if_t ifp, struct mbuf *m);
static void ntb_net_tx_handler(struct ntb_transport_qp *qp, void *qp_data,
    void *data, int len);
static void ntb_net_rx_handler(struct ntb_transport_qp *qp, void *qp_data,
    void *data, int len);
static void ntb_net_event_handler(void *data, enum ntb_link_event status);
static void ntb_handle_tx(void *arg, int pending);
static void ntb_qp_full(void *arg);
static void ntb_qflush(if_t ifp);
static void create_random_local_eui48(u_char *eaddr);

static int
ntb_net_probe(device_t dev)
{

	device_set_desc(dev, "NTB Network Interface");
	return (0);
}

static int
ntb_net_attach(device_t dev)
{
	struct ntb_net_ctx *sc = device_get_softc(dev);
	struct ntb_net_queue *q;
	if_t ifp;
	struct ntb_queue_handlers handlers = { ntb_net_rx_handler,
	    ntb_net_tx_handler, ntb_net_event_handler };
	int i;

	ifp = sc->ifp = if_gethandle(IFT_ETHER);
	if (ifp == NULL) {
		printf("ntb: Cannot allocate ifnet structure\n");
		return (ENOMEM);
	}
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	if_setdev(ifp, dev);

	sc->num_queues = min(g_if_ntb_num_queues,
	    ntb_transport_queue_count(dev));
	sc->queues = malloc(sc->num_queues * sizeof(struct ntb_net_queue),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	sc->mtu = INT_MAX;
	for (i = 0; i < sc->num_queues; i++) {
		q = &sc->queues[i];
		q->sc = sc;
		q->ifp = ifp;
		q->qp = ntb_transport_create_queue(dev, i, &handlers, q);
		if (q->qp == NULL)
			break;
		sc->mtu = imin(sc->mtu, ntb_transport_max_size(q->qp));
		mtx_init(&q->tx_lock, "ntb tx", NULL, MTX_DEF);
		q->br = buf_ring_alloc(4096, M_DEVBUF, M_WAITOK, &q->tx_lock);
		TASK_INIT(&q->tx_task, 0, ntb_handle_tx, q);
		q->tx_tq = taskqueue_create_fast("ntb_txq", M_NOWAIT,
		    taskqueue_thread_enqueue, &q->tx_tq);
		taskqueue_start_threads(&q->tx_tq, 1, PI_NET, "%s txq%d",
		    device_get_nameunit(dev), i);
		callout_init(&q->queue_full, 1);
	}
	sc->num_queues = i;
	device_printf(dev, "%d queue(s)\n", sc->num_queues);

	if_setinitfn(ifp, ntb_net_init);
	if_setsoftc(ifp, sc);
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	if_setioctlfn(ifp, ntb_ioctl);
	if_settransmitfn(ifp, ntb_transmit);
	if_setqflushfn(ifp, ntb_qflush);
	create_random_local_eui48(sc->eaddr);
	ether_ifattach(ifp, sc->eaddr);
	if_setcapabilities(ifp, IFCAP_HWCSUM | IFCAP_HWCSUM_IPV6 |
	    IFCAP_JUMBO_MTU | IFCAP_LINKSTATE);
	if_setcapenable(ifp, IFCAP_JUMBO_MTU | IFCAP_LINKSTATE);
	if_setmtu(ifp, sc->mtu - ETHER_HDR_LEN);

	ifmedia_init(&sc->media, IFM_IMASK, ntb_ifmedia_upd,
	    ntb_ifmedia_sts);
	ifmedia_add(&sc->media, NTB_MEDIATYPE, 0, NULL);
	ifmedia_set(&sc->media, NTB_MEDIATYPE);

	for (i = 0; i < sc->num_queues; i++)
		ntb_transport_link_up(sc->queues[i].qp);
	return (0);
}

static int
ntb_net_detach(device_t dev)
{
	struct ntb_net_ctx *sc = device_get_softc(dev);
	struct ntb_net_queue *q;
	int i;

	for (i = 0; i < sc->num_queues; i++)
		ntb_transport_link_down(sc->queues[i].qp);
	ether_ifdetach(sc->ifp);
	if_free(sc->ifp);
	ifmedia_removeall(&sc->media);
	for (i = 0; i < sc->num_queues; i++) {
		q = &sc->queues[i];
		ntb_transport_free_queue(q->qp);
		buf_ring_free(q->br, M_DEVBUF);
		callout_drain(&q->queue_full);
		taskqueue_drain_all(q->tx_tq);
		mtx_destroy(&q->tx_lock);
	}
	free(sc->queues, M_DEVBUF);
	return (0);
}

/* Network device interface */

static void
ntb_net_init(void *arg)
{
	struct ntb_net_ctx *sc = arg;
	if_t ifp = sc->ifp;

	if_setdrvflagbits(ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);
	if_setbaudrate(ifp, ntb_transport_link_speed(sc->queues[0].qp));
	if_link_state_change(ifp, ntb_transport_link_query(sc->queues[0].qp) ?
	    LINK_STATE_UP : LINK_STATE_DOWN);
}

static int
ntb_ioctl(if_t ifp, u_long command, caddr_t data)
{
	struct ntb_net_ctx *sc = if_getsoftc(ifp);
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCSIFMTU:
	    {
		if (ifr->ifr_mtu > sc->mtu - ETHER_HDR_LEN) {
			error = EINVAL;
			break;
		}

		if_setmtu(ifp, ifr->ifr_mtu);
		break;
	    }

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;

	case SIOCSIFCAP:
		if (ifr->ifr_reqcap & IFCAP_RXCSUM)
			if_setcapenablebit(ifp, IFCAP_RXCSUM, 0);
		else
			if_setcapenablebit(ifp, 0, IFCAP_RXCSUM);
		if (ifr->ifr_reqcap & IFCAP_TXCSUM) {
			if_setcapenablebit(ifp, IFCAP_TXCSUM, 0);
			if_sethwassistbits(ifp, NTB_CSUM_FEATURES, 0);
		} else {
			if_setcapenablebit(ifp, 0, IFCAP_TXCSUM);
			if_sethwassistbits(ifp, 0, NTB_CSUM_FEATURES);
		}
		if (ifr->ifr_reqcap & IFCAP_RXCSUM_IPV6)
			if_setcapenablebit(ifp, IFCAP_RXCSUM_IPV6, 0);
		else
			if_setcapenablebit(ifp, 0, IFCAP_RXCSUM_IPV6);
		if (ifr->ifr_reqcap & IFCAP_TXCSUM_IPV6) {
			if_setcapenablebit(ifp, IFCAP_TXCSUM_IPV6, 0);
			if_sethwassistbits(ifp, NTB_CSUM_FEATURES6, 0);
		} else {
			if_setcapenablebit(ifp, 0, IFCAP_TXCSUM_IPV6);
			if_sethwassistbits(ifp, 0, NTB_CSUM_FEATURES6);
		}
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static int
ntb_ifmedia_upd(struct ifnet *ifp)
{
	struct ntb_net_ctx *sc = if_getsoftc(ifp);
	struct ifmedia *ifm = &sc->media;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	return (0);
}

static void
ntb_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ntb_net_ctx *sc = if_getsoftc(ifp);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = NTB_MEDIATYPE;
	if (ntb_transport_link_query(sc->queues[0].qp))
		ifmr->ifm_status |= IFM_ACTIVE;
}

static void
ntb_transmit_locked(struct ntb_net_queue *q)
{
	if_t ifp = q->ifp;
	struct mbuf *m;
	int rc, len;
	short mflags;

	CTR0(KTR_NTB, "TX: ntb_transmit_locked");
	while ((m = drbr_peek(ifp, q->br)) != NULL) {
		CTR1(KTR_NTB, "TX: start mbuf %p", m);
		if_etherbpfmtap(ifp, m);
		len = m->m_pkthdr.len;
		mflags = m->m_flags;
		rc = ntb_transport_tx_enqueue(q->qp, m, m, len);
		if (rc != 0) {
			CTR2(KTR_NTB, "TX: could not tx mbuf %p: %d", m, rc);
			if (rc == EAGAIN) {
				drbr_putback(ifp, q->br, m);
				callout_reset_sbt(&q->queue_full,
				    SBT_1MS / 4, SBT_1MS / 4,
				    ntb_qp_full, q, 0);
			} else {
				m_freem(m);
				drbr_advance(ifp, q->br);
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			}
			break;
		}
		drbr_advance(ifp, q->br);
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
		if (mflags & M_MCAST)
			if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);
	}
}

static int
ntb_transmit(if_t ifp, struct mbuf *m)
{
	struct ntb_net_ctx *sc = if_getsoftc(ifp);
	struct ntb_net_queue *q;
	int error, i;

	CTR0(KTR_NTB, "TX: ntb_transmit");
	if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE)
		i = m->m_pkthdr.flowid % sc->num_queues;
	else
		i = curcpu % sc->num_queues;
	q = &sc->queues[i];

	error = drbr_enqueue(ifp, q->br, m);
	if (error)
		return (error);

	if (mtx_trylock(&q->tx_lock)) {
		ntb_transmit_locked(q);
		mtx_unlock(&q->tx_lock);
	} else
		taskqueue_enqueue(q->tx_tq, &q->tx_task);
	return (0);
}

static void
ntb_handle_tx(void *arg, int pending)
{
	struct ntb_net_queue *q = arg;

	mtx_lock(&q->tx_lock);
	ntb_transmit_locked(q);
	mtx_unlock(&q->tx_lock);
}

static void
ntb_qp_full(void *arg)
{
	struct ntb_net_queue *q = arg;

	CTR0(KTR_NTB, "TX: qp_full callout");
	if (ntb_transport_tx_free_entry(q->qp) > 0)
		taskqueue_enqueue(q->tx_tq, &q->tx_task);
	else
		callout_schedule_sbt(&q->queue_full,
		    SBT_1MS / 4, SBT_1MS / 4, 0);
}

static void
ntb_qflush(if_t ifp)
{
	struct ntb_net_ctx *sc = if_getsoftc(ifp);
	struct ntb_net_queue *q;
	struct mbuf *m;
	int i;

	for (i = 0; i < sc->num_queues; i++) {
		q = &sc->queues[i];
		mtx_lock(&q->tx_lock);
		while ((m = buf_ring_dequeue_sc(q->br)) != NULL)
			m_freem(m);
		mtx_unlock(&q->tx_lock);
	}
	if_qflush(ifp);
}

/* Network Device Callbacks */
static void
ntb_net_tx_handler(struct ntb_transport_qp *qp, void *qp_data, void *data,
    int len)
{

	m_freem(data);
	CTR1(KTR_NTB, "TX: tx_handler freeing mbuf %p", data);
}

static void
ntb_net_rx_handler(struct ntb_transport_qp *qp, void *qp_data, void *data,
    int len)
{
	struct ntb_net_queue *q = qp_data;
	struct ntb_net_ctx *sc = q->sc;
	struct mbuf *m = data;
	if_t ifp = q->ifp;
	uint16_t proto;

	CTR1(KTR_NTB, "RX: rx handler (%d)", len);
	if (len < 0) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		return;
	}

	m->m_pkthdr.rcvif = ifp;
	if (sc->num_queues > 1) {
		m->m_pkthdr.flowid = q - sc->queues;
		M_HASHTYPE_SET(m, M_HASHTYPE_OPAQUE);
	}
	if (if_getcapenable(ifp) & (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6)) {
		m_copydata(m, 12, 2, (void *)&proto);
		switch (ntohs(proto)) {
		case ETHERTYPE_IP:
			if (if_getcapenable(ifp) & IFCAP_RXCSUM) {
				m->m_pkthdr.csum_data = 0xffff;
				m->m_pkthdr.csum_flags = NTB_CSUM_SET;
			}
			break;
		case ETHERTYPE_IPV6:
			if (if_getcapenable(ifp) & IFCAP_RXCSUM_IPV6) {
				m->m_pkthdr.csum_data = 0xffff;
				m->m_pkthdr.csum_flags = NTB_CSUM_SET;
			}
			break;
		}
	}
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	if_input(ifp, m);
}

static void
ntb_net_event_handler(void *data, enum ntb_link_event status)
{
	struct ntb_net_queue *q = data;

	if_setbaudrate(q->ifp, ntb_transport_link_speed(q->qp));
	if_link_state_change(q->ifp, (status == NTB_LINK_UP) ? LINK_STATE_UP :
	    LINK_STATE_DOWN);
}

/* Helper functions */
/* TODO: This too should really be part of the kernel */
#define EUI48_MULTICAST			1 << 0
#define EUI48_LOCALLY_ADMINISTERED	1 << 1
static void
create_random_local_eui48(u_char *eaddr)
{
	static uint8_t counter = 0;

	eaddr[0] = EUI48_LOCALLY_ADMINISTERED;
	arc4rand(&eaddr[1], 4, 0);
	eaddr[5] = counter++;
}

static device_method_t ntb_net_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,     ntb_net_probe),
	DEVMETHOD(device_attach,    ntb_net_attach),
	DEVMETHOD(device_detach,    ntb_net_detach),
	DEVMETHOD_END
};

devclass_t ntb_net_devclass;
static DEFINE_CLASS_0(ntb, ntb_net_driver, ntb_net_methods,
    sizeof(struct ntb_net_ctx));
DRIVER_MODULE(if_ntb, ntb_transport, ntb_net_driver, ntb_net_devclass,
    NULL, NULL);
MODULE_DEPEND(if_ntb, ntb_transport, 1, 1, 1);
MODULE_VERSION(if_ntb, 1);

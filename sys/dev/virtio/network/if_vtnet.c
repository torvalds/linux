/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, Bryan Venteicher <bryanv@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Driver for VirtIO network devices. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/eventhandler.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/random.h>
#include <sys/sglist.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>
#include <sys/smp.h>
#include <machine/smp.h>

#include <vm/uma.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/if_vlan_var.h>

#include <net/bpf.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/sctp.h>
#include <netinet/netdump/netdump.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/virtio/virtio.h>
#include <dev/virtio/virtqueue.h>
#include <dev/virtio/network/virtio_net.h>
#include <dev/virtio/network/if_vtnetvar.h>
#include "virtio_if.h"

#include "opt_inet.h"
#include "opt_inet6.h"

static int	vtnet_modevent(module_t, int, void *);

static int	vtnet_probe(device_t);
static int	vtnet_attach(device_t);
static int	vtnet_detach(device_t);
static int	vtnet_suspend(device_t);
static int	vtnet_resume(device_t);
static int	vtnet_shutdown(device_t);
static int	vtnet_attach_completed(device_t);
static int	vtnet_config_change(device_t);

static void	vtnet_negotiate_features(struct vtnet_softc *);
static void	vtnet_setup_features(struct vtnet_softc *);
static int	vtnet_init_rxq(struct vtnet_softc *, int);
static int	vtnet_init_txq(struct vtnet_softc *, int);
static int	vtnet_alloc_rxtx_queues(struct vtnet_softc *);
static void	vtnet_free_rxtx_queues(struct vtnet_softc *);
static int	vtnet_alloc_rx_filters(struct vtnet_softc *);
static void	vtnet_free_rx_filters(struct vtnet_softc *);
static int	vtnet_alloc_virtqueues(struct vtnet_softc *);
static int	vtnet_setup_interface(struct vtnet_softc *);
static int	vtnet_change_mtu(struct vtnet_softc *, int);
static int	vtnet_ioctl(struct ifnet *, u_long, caddr_t);
static uint64_t	vtnet_get_counter(struct ifnet *, ift_counter);

static int	vtnet_rxq_populate(struct vtnet_rxq *);
static void	vtnet_rxq_free_mbufs(struct vtnet_rxq *);
static struct mbuf *
		vtnet_rx_alloc_buf(struct vtnet_softc *, int , struct mbuf **);
static int	vtnet_rxq_replace_lro_nomgr_buf(struct vtnet_rxq *,
		    struct mbuf *, int);
static int	vtnet_rxq_replace_buf(struct vtnet_rxq *, struct mbuf *, int);
static int	vtnet_rxq_enqueue_buf(struct vtnet_rxq *, struct mbuf *);
static int	vtnet_rxq_new_buf(struct vtnet_rxq *);
static int	vtnet_rxq_csum(struct vtnet_rxq *, struct mbuf *,
		     struct virtio_net_hdr *);
static void	vtnet_rxq_discard_merged_bufs(struct vtnet_rxq *, int);
static void	vtnet_rxq_discard_buf(struct vtnet_rxq *, struct mbuf *);
static int	vtnet_rxq_merged_eof(struct vtnet_rxq *, struct mbuf *, int);
static void	vtnet_rxq_input(struct vtnet_rxq *, struct mbuf *,
		    struct virtio_net_hdr *);
static int	vtnet_rxq_eof(struct vtnet_rxq *);
static void	vtnet_rx_vq_intr(void *);
static void	vtnet_rxq_tq_intr(void *, int);

static int	vtnet_txq_below_threshold(struct vtnet_txq *);
static int	vtnet_txq_notify(struct vtnet_txq *);
static void	vtnet_txq_free_mbufs(struct vtnet_txq *);
static int	vtnet_txq_offload_ctx(struct vtnet_txq *, struct mbuf *,
		    int *, int *, int *);
static int	vtnet_txq_offload_tso(struct vtnet_txq *, struct mbuf *, int,
		    int, struct virtio_net_hdr *);
static struct mbuf *
		vtnet_txq_offload(struct vtnet_txq *, struct mbuf *,
		    struct virtio_net_hdr *);
static int	vtnet_txq_enqueue_buf(struct vtnet_txq *, struct mbuf **,
		    struct vtnet_tx_header *);
static int	vtnet_txq_encap(struct vtnet_txq *, struct mbuf **, int);
#ifdef VTNET_LEGACY_TX
static void	vtnet_start_locked(struct vtnet_txq *, struct ifnet *);
static void	vtnet_start(struct ifnet *);
#else
static int	vtnet_txq_mq_start_locked(struct vtnet_txq *, struct mbuf *);
static int	vtnet_txq_mq_start(struct ifnet *, struct mbuf *);
static void	vtnet_txq_tq_deferred(void *, int);
#endif
static void	vtnet_txq_start(struct vtnet_txq *);
static void	vtnet_txq_tq_intr(void *, int);
static int	vtnet_txq_eof(struct vtnet_txq *);
static void	vtnet_tx_vq_intr(void *);
static void	vtnet_tx_start_all(struct vtnet_softc *);

#ifndef VTNET_LEGACY_TX
static void	vtnet_qflush(struct ifnet *);
#endif

static int	vtnet_watchdog(struct vtnet_txq *);
static void	vtnet_accum_stats(struct vtnet_softc *,
		    struct vtnet_rxq_stats *, struct vtnet_txq_stats *);
static void	vtnet_tick(void *);

static void	vtnet_start_taskqueues(struct vtnet_softc *);
static void	vtnet_free_taskqueues(struct vtnet_softc *);
static void	vtnet_drain_taskqueues(struct vtnet_softc *);

static void	vtnet_drain_rxtx_queues(struct vtnet_softc *);
static void	vtnet_stop_rendezvous(struct vtnet_softc *);
static void	vtnet_stop(struct vtnet_softc *);
static int	vtnet_virtio_reinit(struct vtnet_softc *);
static void	vtnet_init_rx_filters(struct vtnet_softc *);
static int	vtnet_init_rx_queues(struct vtnet_softc *);
static int	vtnet_init_tx_queues(struct vtnet_softc *);
static int	vtnet_init_rxtx_queues(struct vtnet_softc *);
static void	vtnet_set_active_vq_pairs(struct vtnet_softc *);
static int	vtnet_reinit(struct vtnet_softc *);
static void	vtnet_init_locked(struct vtnet_softc *);
static void	vtnet_init(void *);

static void	vtnet_free_ctrl_vq(struct vtnet_softc *);
static void	vtnet_exec_ctrl_cmd(struct vtnet_softc *, void *,
		    struct sglist *, int, int);
static int	vtnet_ctrl_mac_cmd(struct vtnet_softc *, uint8_t *);
static int	vtnet_ctrl_mq_cmd(struct vtnet_softc *, uint16_t);
static int	vtnet_ctrl_rx_cmd(struct vtnet_softc *, int, int);
static int	vtnet_set_promisc(struct vtnet_softc *, int);
static int	vtnet_set_allmulti(struct vtnet_softc *, int);
static void	vtnet_attach_disable_promisc(struct vtnet_softc *);
static void	vtnet_rx_filter(struct vtnet_softc *);
static void	vtnet_rx_filter_mac(struct vtnet_softc *);
static int	vtnet_exec_vlan_filter(struct vtnet_softc *, int, uint16_t);
static void	vtnet_rx_filter_vlan(struct vtnet_softc *);
static void	vtnet_update_vlan_filter(struct vtnet_softc *, int, uint16_t);
static void	vtnet_register_vlan(void *, struct ifnet *, uint16_t);
static void	vtnet_unregister_vlan(void *, struct ifnet *, uint16_t);

static int	vtnet_is_link_up(struct vtnet_softc *);
static void	vtnet_update_link_status(struct vtnet_softc *);
static int	vtnet_ifmedia_upd(struct ifnet *);
static void	vtnet_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static void	vtnet_get_hwaddr(struct vtnet_softc *);
static void	vtnet_set_hwaddr(struct vtnet_softc *);
static void	vtnet_vlan_tag_remove(struct mbuf *);
static void	vtnet_set_rx_process_limit(struct vtnet_softc *);
static void	vtnet_set_tx_intr_threshold(struct vtnet_softc *);

static void	vtnet_setup_rxq_sysctl(struct sysctl_ctx_list *,
		    struct sysctl_oid_list *, struct vtnet_rxq *);
static void	vtnet_setup_txq_sysctl(struct sysctl_ctx_list *,
		    struct sysctl_oid_list *, struct vtnet_txq *);
static void	vtnet_setup_queue_sysctl(struct vtnet_softc *);
static void	vtnet_setup_sysctl(struct vtnet_softc *);

static int	vtnet_rxq_enable_intr(struct vtnet_rxq *);
static void	vtnet_rxq_disable_intr(struct vtnet_rxq *);
static int	vtnet_txq_enable_intr(struct vtnet_txq *);
static void	vtnet_txq_disable_intr(struct vtnet_txq *);
static void	vtnet_enable_rx_interrupts(struct vtnet_softc *);
static void	vtnet_enable_tx_interrupts(struct vtnet_softc *);
static void	vtnet_enable_interrupts(struct vtnet_softc *);
static void	vtnet_disable_rx_interrupts(struct vtnet_softc *);
static void	vtnet_disable_tx_interrupts(struct vtnet_softc *);
static void	vtnet_disable_interrupts(struct vtnet_softc *);

static int	vtnet_tunable_int(struct vtnet_softc *, const char *, int);

NETDUMP_DEFINE(vtnet);

/* Tunables. */
static SYSCTL_NODE(_hw, OID_AUTO, vtnet, CTLFLAG_RD, 0, "VNET driver parameters");
static int vtnet_csum_disable = 0;
TUNABLE_INT("hw.vtnet.csum_disable", &vtnet_csum_disable);
SYSCTL_INT(_hw_vtnet, OID_AUTO, csum_disable, CTLFLAG_RDTUN,
    &vtnet_csum_disable, 0, "Disables receive and send checksum offload");
static int vtnet_tso_disable = 0;
TUNABLE_INT("hw.vtnet.tso_disable", &vtnet_tso_disable);
SYSCTL_INT(_hw_vtnet, OID_AUTO, tso_disable, CTLFLAG_RDTUN, &vtnet_tso_disable,
    0, "Disables TCP Segmentation Offload");
static int vtnet_lro_disable = 0;
TUNABLE_INT("hw.vtnet.lro_disable", &vtnet_lro_disable);
SYSCTL_INT(_hw_vtnet, OID_AUTO, lro_disable, CTLFLAG_RDTUN, &vtnet_lro_disable,
    0, "Disables TCP Large Receive Offload");
static int vtnet_mq_disable = 0;
TUNABLE_INT("hw.vtnet.mq_disable", &vtnet_mq_disable);
SYSCTL_INT(_hw_vtnet, OID_AUTO, mq_disable, CTLFLAG_RDTUN, &vtnet_mq_disable,
    0, "Disables Multi Queue support");
static int vtnet_mq_max_pairs = VTNET_MAX_QUEUE_PAIRS;
TUNABLE_INT("hw.vtnet.mq_max_pairs", &vtnet_mq_max_pairs);
SYSCTL_INT(_hw_vtnet, OID_AUTO, mq_max_pairs, CTLFLAG_RDTUN,
    &vtnet_mq_max_pairs, 0, "Sets the maximum number of Multi Queue pairs");
static int vtnet_rx_process_limit = 512;
TUNABLE_INT("hw.vtnet.rx_process_limit", &vtnet_rx_process_limit);
SYSCTL_INT(_hw_vtnet, OID_AUTO, rx_process_limit, CTLFLAG_RDTUN,
    &vtnet_rx_process_limit, 0,
    "Limits the number RX segments processed in a single pass");

static uma_zone_t vtnet_tx_header_zone;

static struct virtio_feature_desc vtnet_feature_desc[] = {
	{ VIRTIO_NET_F_CSUM,		"TxChecksum"	},
	{ VIRTIO_NET_F_GUEST_CSUM,	"RxChecksum"	},
	{ VIRTIO_NET_F_MAC,		"MacAddress"	},
	{ VIRTIO_NET_F_GSO,		"TxAllGSO"	},
	{ VIRTIO_NET_F_GUEST_TSO4,	"RxTSOv4"	},
	{ VIRTIO_NET_F_GUEST_TSO6,	"RxTSOv6"	},
	{ VIRTIO_NET_F_GUEST_ECN,	"RxECN"		},
	{ VIRTIO_NET_F_GUEST_UFO,	"RxUFO"		},
	{ VIRTIO_NET_F_HOST_TSO4,	"TxTSOv4"	},
	{ VIRTIO_NET_F_HOST_TSO6,	"TxTSOv6"	},
	{ VIRTIO_NET_F_HOST_ECN,	"TxTSOECN"	},
	{ VIRTIO_NET_F_HOST_UFO,	"TxUFO"		},
	{ VIRTIO_NET_F_MRG_RXBUF,	"MrgRxBuf"	},
	{ VIRTIO_NET_F_STATUS,		"Status"	},
	{ VIRTIO_NET_F_CTRL_VQ,		"ControlVq"	},
	{ VIRTIO_NET_F_CTRL_RX,		"RxMode"	},
	{ VIRTIO_NET_F_CTRL_VLAN,	"VLanFilter"	},
	{ VIRTIO_NET_F_CTRL_RX_EXTRA,	"RxModeExtra"	},
	{ VIRTIO_NET_F_GUEST_ANNOUNCE,	"GuestAnnounce"	},
	{ VIRTIO_NET_F_MQ,		"Multiqueue"	},
	{ VIRTIO_NET_F_CTRL_MAC_ADDR,	"SetMacAddress"	},

	{ 0, NULL }
};

static device_method_t vtnet_methods[] = {
	/* Device methods. */
	DEVMETHOD(device_probe,			vtnet_probe),
	DEVMETHOD(device_attach,		vtnet_attach),
	DEVMETHOD(device_detach,		vtnet_detach),
	DEVMETHOD(device_suspend,		vtnet_suspend),
	DEVMETHOD(device_resume,		vtnet_resume),
	DEVMETHOD(device_shutdown,		vtnet_shutdown),

	/* VirtIO methods. */
	DEVMETHOD(virtio_attach_completed,	vtnet_attach_completed),
	DEVMETHOD(virtio_config_change,		vtnet_config_change),

	DEVMETHOD_END
};

#ifdef DEV_NETMAP
#include <dev/netmap/if_vtnet_netmap.h>
#endif /* DEV_NETMAP */

static driver_t vtnet_driver = {
	"vtnet",
	vtnet_methods,
	sizeof(struct vtnet_softc)
};
static devclass_t vtnet_devclass;

DRIVER_MODULE(vtnet, virtio_mmio, vtnet_driver, vtnet_devclass,
    vtnet_modevent, 0);
DRIVER_MODULE(vtnet, virtio_pci, vtnet_driver, vtnet_devclass,
    vtnet_modevent, 0);
MODULE_VERSION(vtnet, 1);
MODULE_DEPEND(vtnet, virtio, 1, 1, 1);
#ifdef DEV_NETMAP
MODULE_DEPEND(vtnet, netmap, 1, 1, 1);
#endif /* DEV_NETMAP */

static int
vtnet_modevent(module_t mod, int type, void *unused)
{
	int error = 0;
	static int loaded = 0;

	switch (type) {
	case MOD_LOAD:
		if (loaded++ == 0)
			vtnet_tx_header_zone = uma_zcreate("vtnet_tx_hdr",
				sizeof(struct vtnet_tx_header),
				NULL, NULL, NULL, NULL, 0, 0);
		break;
	case MOD_QUIESCE:
		if (uma_zone_get_cur(vtnet_tx_header_zone) > 0)
			error = EBUSY;
		break;
	case MOD_UNLOAD:
		if (--loaded == 0) {
			uma_zdestroy(vtnet_tx_header_zone);
			vtnet_tx_header_zone = NULL;
		}
		break;
	case MOD_SHUTDOWN:
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static int
vtnet_probe(device_t dev)
{

	if (virtio_get_device_type(dev) != VIRTIO_ID_NETWORK)
		return (ENXIO);

	device_set_desc(dev, "VirtIO Networking Adapter");

	return (BUS_PROBE_DEFAULT);
}

static int
vtnet_attach(device_t dev)
{
	struct vtnet_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->vtnet_dev = dev;

	/* Register our feature descriptions. */
	virtio_set_feature_desc(dev, vtnet_feature_desc);

	VTNET_CORE_LOCK_INIT(sc);
	callout_init_mtx(&sc->vtnet_tick_ch, VTNET_CORE_MTX(sc), 0);

	vtnet_setup_sysctl(sc);
	vtnet_setup_features(sc);

	error = vtnet_alloc_rx_filters(sc);
	if (error) {
		device_printf(dev, "cannot allocate Rx filters\n");
		goto fail;
	}

	error = vtnet_alloc_rxtx_queues(sc);
	if (error) {
		device_printf(dev, "cannot allocate queues\n");
		goto fail;
	}

	error = vtnet_alloc_virtqueues(sc);
	if (error) {
		device_printf(dev, "cannot allocate virtqueues\n");
		goto fail;
	}

	error = vtnet_setup_interface(sc);
	if (error) {
		device_printf(dev, "cannot setup interface\n");
		goto fail;
	}

	error = virtio_setup_intr(dev, INTR_TYPE_NET);
	if (error) {
		device_printf(dev, "cannot setup virtqueue interrupts\n");
		/* BMV: This will crash if during boot! */
		ether_ifdetach(sc->vtnet_ifp);
		goto fail;
	}

#ifdef DEV_NETMAP
	vtnet_netmap_attach(sc);
#endif /* DEV_NETMAP */

	vtnet_start_taskqueues(sc);

fail:
	if (error)
		vtnet_detach(dev);

	return (error);
}

static int
vtnet_detach(device_t dev)
{
	struct vtnet_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = sc->vtnet_ifp;

	if (device_is_attached(dev)) {
		VTNET_CORE_LOCK(sc);
		vtnet_stop(sc);
		VTNET_CORE_UNLOCK(sc);

		callout_drain(&sc->vtnet_tick_ch);
		vtnet_drain_taskqueues(sc);

		ether_ifdetach(ifp);
	}

#ifdef DEV_NETMAP
	netmap_detach(ifp);
#endif /* DEV_NETMAP */

	vtnet_free_taskqueues(sc);

	if (sc->vtnet_vlan_attach != NULL) {
		EVENTHANDLER_DEREGISTER(vlan_config, sc->vtnet_vlan_attach);
		sc->vtnet_vlan_attach = NULL;
	}
	if (sc->vtnet_vlan_detach != NULL) {
		EVENTHANDLER_DEREGISTER(vlan_unconfig, sc->vtnet_vlan_detach);
		sc->vtnet_vlan_detach = NULL;
	}

	ifmedia_removeall(&sc->vtnet_media);

	if (ifp != NULL) {
		if_free(ifp);
		sc->vtnet_ifp = NULL;
	}

	vtnet_free_rxtx_queues(sc);
	vtnet_free_rx_filters(sc);

	if (sc->vtnet_ctrl_vq != NULL)
		vtnet_free_ctrl_vq(sc);

	VTNET_CORE_LOCK_DESTROY(sc);

	return (0);
}

static int
vtnet_suspend(device_t dev)
{
	struct vtnet_softc *sc;

	sc = device_get_softc(dev);

	VTNET_CORE_LOCK(sc);
	vtnet_stop(sc);
	sc->vtnet_flags |= VTNET_FLAG_SUSPENDED;
	VTNET_CORE_UNLOCK(sc);

	return (0);
}

static int
vtnet_resume(device_t dev)
{
	struct vtnet_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = sc->vtnet_ifp;

	VTNET_CORE_LOCK(sc);
	if (ifp->if_flags & IFF_UP)
		vtnet_init_locked(sc);
	sc->vtnet_flags &= ~VTNET_FLAG_SUSPENDED;
	VTNET_CORE_UNLOCK(sc);

	return (0);
}

static int
vtnet_shutdown(device_t dev)
{

	/*
	 * Suspend already does all of what we need to
	 * do here; we just never expect to be resumed.
	 */
	return (vtnet_suspend(dev));
}

static int
vtnet_attach_completed(device_t dev)
{

	vtnet_attach_disable_promisc(device_get_softc(dev));

	return (0);
}

static int
vtnet_config_change(device_t dev)
{
	struct vtnet_softc *sc;

	sc = device_get_softc(dev);

	VTNET_CORE_LOCK(sc);
	vtnet_update_link_status(sc);
	if (sc->vtnet_link_active != 0)
		vtnet_tx_start_all(sc);
	VTNET_CORE_UNLOCK(sc);

	return (0);
}

static void
vtnet_negotiate_features(struct vtnet_softc *sc)
{
	device_t dev;
	uint64_t mask, features;

	dev = sc->vtnet_dev;
	mask = 0;

	/*
	 * TSO and LRO are only available when their corresponding checksum
	 * offload feature is also negotiated.
	 */
	if (vtnet_tunable_int(sc, "csum_disable", vtnet_csum_disable)) {
		mask |= VIRTIO_NET_F_CSUM | VIRTIO_NET_F_GUEST_CSUM;
		mask |= VTNET_TSO_FEATURES | VTNET_LRO_FEATURES;
	}
	if (vtnet_tunable_int(sc, "tso_disable", vtnet_tso_disable))
		mask |= VTNET_TSO_FEATURES;
	if (vtnet_tunable_int(sc, "lro_disable", vtnet_lro_disable))
		mask |= VTNET_LRO_FEATURES;
#ifndef VTNET_LEGACY_TX
	if (vtnet_tunable_int(sc, "mq_disable", vtnet_mq_disable))
		mask |= VIRTIO_NET_F_MQ;
#else
	mask |= VIRTIO_NET_F_MQ;
#endif

	features = VTNET_FEATURES & ~mask;
	sc->vtnet_features = virtio_negotiate_features(dev, features);

	if (virtio_with_feature(dev, VTNET_LRO_FEATURES) &&
	    virtio_with_feature(dev, VIRTIO_NET_F_MRG_RXBUF) == 0) {
		/*
		 * LRO without mergeable buffers requires special care. This
		 * is not ideal because every receive buffer must be large
		 * enough to hold the maximum TCP packet, the Ethernet header,
		 * and the header. This requires up to 34 descriptors with
		 * MCLBYTES clusters. If we do not have indirect descriptors,
		 * LRO is disabled since the virtqueue will not contain very
		 * many receive buffers.
		 */
		if (!virtio_with_feature(dev, VIRTIO_RING_F_INDIRECT_DESC)) {
			device_printf(dev,
			    "LRO disabled due to both mergeable buffers and "
			    "indirect descriptors not negotiated\n");

			features &= ~VTNET_LRO_FEATURES;
			sc->vtnet_features =
			    virtio_negotiate_features(dev, features);
		} else
			sc->vtnet_flags |= VTNET_FLAG_LRO_NOMRG;
	}
}

static void
vtnet_setup_features(struct vtnet_softc *sc)
{
	device_t dev;

	dev = sc->vtnet_dev;

	vtnet_negotiate_features(sc);

	if (virtio_with_feature(dev, VIRTIO_RING_F_INDIRECT_DESC))
		sc->vtnet_flags |= VTNET_FLAG_INDIRECT;
	if (virtio_with_feature(dev, VIRTIO_RING_F_EVENT_IDX))
		sc->vtnet_flags |= VTNET_FLAG_EVENT_IDX;

	if (virtio_with_feature(dev, VIRTIO_NET_F_MAC)) {
		/* This feature should always be negotiated. */
		sc->vtnet_flags |= VTNET_FLAG_MAC;
	}

	if (virtio_with_feature(dev, VIRTIO_NET_F_MRG_RXBUF)) {
		sc->vtnet_flags |= VTNET_FLAG_MRG_RXBUFS;
		sc->vtnet_hdr_size = sizeof(struct virtio_net_hdr_mrg_rxbuf);
	} else
		sc->vtnet_hdr_size = sizeof(struct virtio_net_hdr);

	if (sc->vtnet_flags & VTNET_FLAG_MRG_RXBUFS)
		sc->vtnet_rx_nsegs = VTNET_MRG_RX_SEGS;
	else if (sc->vtnet_flags & VTNET_FLAG_LRO_NOMRG)
		sc->vtnet_rx_nsegs = VTNET_MAX_RX_SEGS;
	else
		sc->vtnet_rx_nsegs = VTNET_MIN_RX_SEGS;

	if (virtio_with_feature(dev, VIRTIO_NET_F_GSO) ||
	    virtio_with_feature(dev, VIRTIO_NET_F_HOST_TSO4) ||
	    virtio_with_feature(dev, VIRTIO_NET_F_HOST_TSO6))
		sc->vtnet_tx_nsegs = VTNET_MAX_TX_SEGS;
	else
		sc->vtnet_tx_nsegs = VTNET_MIN_TX_SEGS;

	if (virtio_with_feature(dev, VIRTIO_NET_F_CTRL_VQ)) {
		sc->vtnet_flags |= VTNET_FLAG_CTRL_VQ;

		if (virtio_with_feature(dev, VIRTIO_NET_F_CTRL_RX))
			sc->vtnet_flags |= VTNET_FLAG_CTRL_RX;
		if (virtio_with_feature(dev, VIRTIO_NET_F_CTRL_VLAN))
			sc->vtnet_flags |= VTNET_FLAG_VLAN_FILTER;
		if (virtio_with_feature(dev, VIRTIO_NET_F_CTRL_MAC_ADDR))
			sc->vtnet_flags |= VTNET_FLAG_CTRL_MAC;
	}

	if (virtio_with_feature(dev, VIRTIO_NET_F_MQ) &&
	    sc->vtnet_flags & VTNET_FLAG_CTRL_VQ) {
		sc->vtnet_max_vq_pairs = virtio_read_dev_config_2(dev,
		    offsetof(struct virtio_net_config, max_virtqueue_pairs));
	} else
		sc->vtnet_max_vq_pairs = 1;

	if (sc->vtnet_max_vq_pairs > 1) {
		/*
		 * Limit the maximum number of queue pairs to the lower of
		 * the number of CPUs and the configured maximum.
		 * The actual number of queues that get used may be less.
		 */
		int max;

		max = vtnet_tunable_int(sc, "mq_max_pairs", vtnet_mq_max_pairs);
		if (max > VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MIN) {
			if (max > mp_ncpus)
				max = mp_ncpus;
			if (max > VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX)
				max = VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX;
			if (max > 1) {
				sc->vtnet_requested_vq_pairs = max;
				sc->vtnet_flags |= VTNET_FLAG_MULTIQ;
			}
		}
	}
}

static int
vtnet_init_rxq(struct vtnet_softc *sc, int id)
{
	struct vtnet_rxq *rxq;

	rxq = &sc->vtnet_rxqs[id];

	snprintf(rxq->vtnrx_name, sizeof(rxq->vtnrx_name), "%s-rx%d",
	    device_get_nameunit(sc->vtnet_dev), id);
	mtx_init(&rxq->vtnrx_mtx, rxq->vtnrx_name, NULL, MTX_DEF);

	rxq->vtnrx_sc = sc;
	rxq->vtnrx_id = id;

	rxq->vtnrx_sg = sglist_alloc(sc->vtnet_rx_nsegs, M_NOWAIT);
	if (rxq->vtnrx_sg == NULL)
		return (ENOMEM);

	TASK_INIT(&rxq->vtnrx_intrtask, 0, vtnet_rxq_tq_intr, rxq);
	rxq->vtnrx_tq = taskqueue_create(rxq->vtnrx_name, M_NOWAIT,
	    taskqueue_thread_enqueue, &rxq->vtnrx_tq);

	return (rxq->vtnrx_tq == NULL ? ENOMEM : 0);
}

static int
vtnet_init_txq(struct vtnet_softc *sc, int id)
{
	struct vtnet_txq *txq;

	txq = &sc->vtnet_txqs[id];

	snprintf(txq->vtntx_name, sizeof(txq->vtntx_name), "%s-tx%d",
	    device_get_nameunit(sc->vtnet_dev), id);
	mtx_init(&txq->vtntx_mtx, txq->vtntx_name, NULL, MTX_DEF);

	txq->vtntx_sc = sc;
	txq->vtntx_id = id;

	txq->vtntx_sg = sglist_alloc(sc->vtnet_tx_nsegs, M_NOWAIT);
	if (txq->vtntx_sg == NULL)
		return (ENOMEM);

#ifndef VTNET_LEGACY_TX
	txq->vtntx_br = buf_ring_alloc(VTNET_DEFAULT_BUFRING_SIZE, M_DEVBUF,
	    M_NOWAIT, &txq->vtntx_mtx);
	if (txq->vtntx_br == NULL)
		return (ENOMEM);

	TASK_INIT(&txq->vtntx_defrtask, 0, vtnet_txq_tq_deferred, txq);
#endif
	TASK_INIT(&txq->vtntx_intrtask, 0, vtnet_txq_tq_intr, txq);
	txq->vtntx_tq = taskqueue_create(txq->vtntx_name, M_NOWAIT,
	    taskqueue_thread_enqueue, &txq->vtntx_tq);
	if (txq->vtntx_tq == NULL)
		return (ENOMEM);

	return (0);
}

static int
vtnet_alloc_rxtx_queues(struct vtnet_softc *sc)
{
	int i, npairs, error;

	npairs = sc->vtnet_max_vq_pairs;

	sc->vtnet_rxqs = malloc(sizeof(struct vtnet_rxq) * npairs, M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	sc->vtnet_txqs = malloc(sizeof(struct vtnet_txq) * npairs, M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (sc->vtnet_rxqs == NULL || sc->vtnet_txqs == NULL)
		return (ENOMEM);

	for (i = 0; i < npairs; i++) {
		error = vtnet_init_rxq(sc, i);
		if (error)
			return (error);
		error = vtnet_init_txq(sc, i);
		if (error)
			return (error);
	}

	vtnet_setup_queue_sysctl(sc);

	return (0);
}

static void
vtnet_destroy_rxq(struct vtnet_rxq *rxq)
{

	rxq->vtnrx_sc = NULL;
	rxq->vtnrx_id = -1;

	if (rxq->vtnrx_sg != NULL) {
		sglist_free(rxq->vtnrx_sg);
		rxq->vtnrx_sg = NULL;
	}

	if (mtx_initialized(&rxq->vtnrx_mtx) != 0)
		mtx_destroy(&rxq->vtnrx_mtx);
}

static void
vtnet_destroy_txq(struct vtnet_txq *txq)
{

	txq->vtntx_sc = NULL;
	txq->vtntx_id = -1;

	if (txq->vtntx_sg != NULL) {
		sglist_free(txq->vtntx_sg);
		txq->vtntx_sg = NULL;
	}

#ifndef VTNET_LEGACY_TX
	if (txq->vtntx_br != NULL) {
		buf_ring_free(txq->vtntx_br, M_DEVBUF);
		txq->vtntx_br = NULL;
	}
#endif

	if (mtx_initialized(&txq->vtntx_mtx) != 0)
		mtx_destroy(&txq->vtntx_mtx);
}

static void
vtnet_free_rxtx_queues(struct vtnet_softc *sc)
{
	int i;

	if (sc->vtnet_rxqs != NULL) {
		for (i = 0; i < sc->vtnet_max_vq_pairs; i++)
			vtnet_destroy_rxq(&sc->vtnet_rxqs[i]);
		free(sc->vtnet_rxqs, M_DEVBUF);
		sc->vtnet_rxqs = NULL;
	}

	if (sc->vtnet_txqs != NULL) {
		for (i = 0; i < sc->vtnet_max_vq_pairs; i++)
			vtnet_destroy_txq(&sc->vtnet_txqs[i]);
		free(sc->vtnet_txqs, M_DEVBUF);
		sc->vtnet_txqs = NULL;
	}
}

static int
vtnet_alloc_rx_filters(struct vtnet_softc *sc)
{

	if (sc->vtnet_flags & VTNET_FLAG_CTRL_RX) {
		sc->vtnet_mac_filter = malloc(sizeof(struct vtnet_mac_filter),
		    M_DEVBUF, M_NOWAIT | M_ZERO);
		if (sc->vtnet_mac_filter == NULL)
			return (ENOMEM);
	}

	if (sc->vtnet_flags & VTNET_FLAG_VLAN_FILTER) {
		sc->vtnet_vlan_filter = malloc(sizeof(uint32_t) *
		    VTNET_VLAN_FILTER_NWORDS, M_DEVBUF, M_NOWAIT | M_ZERO);
		if (sc->vtnet_vlan_filter == NULL)
			return (ENOMEM);
	}

	return (0);
}

static void
vtnet_free_rx_filters(struct vtnet_softc *sc)
{

	if (sc->vtnet_mac_filter != NULL) {
		free(sc->vtnet_mac_filter, M_DEVBUF);
		sc->vtnet_mac_filter = NULL;
	}

	if (sc->vtnet_vlan_filter != NULL) {
		free(sc->vtnet_vlan_filter, M_DEVBUF);
		sc->vtnet_vlan_filter = NULL;
	}
}

static int
vtnet_alloc_virtqueues(struct vtnet_softc *sc)
{
	device_t dev;
	struct vq_alloc_info *info;
	struct vtnet_rxq *rxq;
	struct vtnet_txq *txq;
	int i, idx, flags, nvqs, error;

	dev = sc->vtnet_dev;
	flags = 0;

	nvqs = sc->vtnet_max_vq_pairs * 2;
	if (sc->vtnet_flags & VTNET_FLAG_CTRL_VQ)
		nvqs++;

	info = malloc(sizeof(struct vq_alloc_info) * nvqs, M_TEMP, M_NOWAIT);
	if (info == NULL)
		return (ENOMEM);

	for (i = 0, idx = 0; i < sc->vtnet_max_vq_pairs; i++, idx+=2) {
		rxq = &sc->vtnet_rxqs[i];
		VQ_ALLOC_INFO_INIT(&info[idx], sc->vtnet_rx_nsegs,
		    vtnet_rx_vq_intr, rxq, &rxq->vtnrx_vq,
		    "%s-%d rx", device_get_nameunit(dev), rxq->vtnrx_id);

		txq = &sc->vtnet_txqs[i];
		VQ_ALLOC_INFO_INIT(&info[idx+1], sc->vtnet_tx_nsegs,
		    vtnet_tx_vq_intr, txq, &txq->vtntx_vq,
		    "%s-%d tx", device_get_nameunit(dev), txq->vtntx_id);
	}

	if (sc->vtnet_flags & VTNET_FLAG_CTRL_VQ) {
		VQ_ALLOC_INFO_INIT(&info[idx], 0, NULL, NULL,
		    &sc->vtnet_ctrl_vq, "%s ctrl", device_get_nameunit(dev));
	}

	/*
	 * Enable interrupt binding if this is multiqueue. This only matters
	 * when per-vq MSIX is available.
	 */
	if (sc->vtnet_flags & VTNET_FLAG_MULTIQ)
		flags |= 0;

	error = virtio_alloc_virtqueues(dev, flags, nvqs, info);
	free(info, M_TEMP);

	return (error);
}

static int
vtnet_setup_interface(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;

	dev = sc->vtnet_dev;

	ifp = sc->vtnet_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "cannot allocate ifnet structure\n");
		return (ENOSPC);
	}

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_baudrate = IF_Gbps(10);	/* Approx. */
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = vtnet_init;
	ifp->if_ioctl = vtnet_ioctl;
	ifp->if_get_counter = vtnet_get_counter;
#ifndef VTNET_LEGACY_TX
	ifp->if_transmit = vtnet_txq_mq_start;
	ifp->if_qflush = vtnet_qflush;
#else
	struct virtqueue *vq = sc->vtnet_txqs[0].vtntx_vq;
	ifp->if_start = vtnet_start;
	IFQ_SET_MAXLEN(&ifp->if_snd, virtqueue_size(vq) - 1);
	ifp->if_snd.ifq_drv_maxlen = virtqueue_size(vq) - 1;
	IFQ_SET_READY(&ifp->if_snd);
#endif

	ifmedia_init(&sc->vtnet_media, IFM_IMASK, vtnet_ifmedia_upd,
	    vtnet_ifmedia_sts);
	ifmedia_add(&sc->vtnet_media, VTNET_MEDIATYPE, 0, NULL);
	ifmedia_set(&sc->vtnet_media, VTNET_MEDIATYPE);

	/* Read (or generate) the MAC address for the adapter. */
	vtnet_get_hwaddr(sc);

	ether_ifattach(ifp, sc->vtnet_hwaddr);

	if (virtio_with_feature(dev, VIRTIO_NET_F_STATUS))
		ifp->if_capabilities |= IFCAP_LINKSTATE;

	/* Tell the upper layer(s) we support long frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_JUMBO_MTU | IFCAP_VLAN_MTU;

	if (virtio_with_feature(dev, VIRTIO_NET_F_CSUM)) {
		ifp->if_capabilities |= IFCAP_TXCSUM | IFCAP_TXCSUM_IPV6;

		if (virtio_with_feature(dev, VIRTIO_NET_F_GSO)) {
			ifp->if_capabilities |= IFCAP_TSO4 | IFCAP_TSO6;
			sc->vtnet_flags |= VTNET_FLAG_TSO_ECN;
		} else {
			if (virtio_with_feature(dev, VIRTIO_NET_F_HOST_TSO4))
				ifp->if_capabilities |= IFCAP_TSO4;
			if (virtio_with_feature(dev, VIRTIO_NET_F_HOST_TSO6))
				ifp->if_capabilities |= IFCAP_TSO6;
			if (virtio_with_feature(dev, VIRTIO_NET_F_HOST_ECN))
				sc->vtnet_flags |= VTNET_FLAG_TSO_ECN;
		}

		if (ifp->if_capabilities & IFCAP_TSO)
			ifp->if_capabilities |= IFCAP_VLAN_HWTSO;
	}

	if (virtio_with_feature(dev, VIRTIO_NET_F_GUEST_CSUM)) {
		ifp->if_capabilities |= IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6;

		if (virtio_with_feature(dev, VIRTIO_NET_F_GUEST_TSO4) ||
		    virtio_with_feature(dev, VIRTIO_NET_F_GUEST_TSO6))
			ifp->if_capabilities |= IFCAP_LRO;
	}

	if (ifp->if_capabilities & IFCAP_HWCSUM) {
		/*
		 * VirtIO does not support VLAN tagging, but we can fake
		 * it by inserting and removing the 802.1Q header during
		 * transmit and receive. We are then able to do checksum
		 * offloading of VLAN frames.
		 */
		ifp->if_capabilities |=
		    IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWCSUM;
	}

	ifp->if_capenable = ifp->if_capabilities;

	/*
	 * Capabilities after here are not enabled by default.
	 */

	if (sc->vtnet_flags & VTNET_FLAG_VLAN_FILTER) {
		ifp->if_capabilities |= IFCAP_VLAN_HWFILTER;

		sc->vtnet_vlan_attach = EVENTHANDLER_REGISTER(vlan_config,
		    vtnet_register_vlan, sc, EVENTHANDLER_PRI_FIRST);
		sc->vtnet_vlan_detach = EVENTHANDLER_REGISTER(vlan_unconfig,
		    vtnet_unregister_vlan, sc, EVENTHANDLER_PRI_FIRST);
	}

	vtnet_set_rx_process_limit(sc);
	vtnet_set_tx_intr_threshold(sc);

	NETDUMP_SET(ifp, vtnet);

	return (0);
}

static int
vtnet_change_mtu(struct vtnet_softc *sc, int new_mtu)
{
	struct ifnet *ifp;
	int frame_size, clsize;

	ifp = sc->vtnet_ifp;

	if (new_mtu < ETHERMIN || new_mtu > VTNET_MAX_MTU)
		return (EINVAL);

	frame_size = sc->vtnet_hdr_size + sizeof(struct ether_vlan_header) +
	    new_mtu;

	/*
	 * Based on the new MTU (and hence frame size) determine which
	 * cluster size is most appropriate for the receive queues.
	 */
	if (frame_size <= MCLBYTES) {
		clsize = MCLBYTES;
	} else if ((sc->vtnet_flags & VTNET_FLAG_MRG_RXBUFS) == 0) {
		/* Avoid going past 9K jumbos. */
		if (frame_size > MJUM9BYTES)
			return (EINVAL);
		clsize = MJUM9BYTES;
	} else
		clsize = MJUMPAGESIZE;

	ifp->if_mtu = new_mtu;
	sc->vtnet_rx_new_clsize = clsize;

	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		vtnet_init_locked(sc);
	}

	return (0);
}

static int
vtnet_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct vtnet_softc *sc;
	struct ifreq *ifr;
	int reinit, mask, error;

	sc = ifp->if_softc;
	ifr = (struct ifreq *) data;
	error = 0;

	switch (cmd) {
	case SIOCSIFMTU:
		if (ifp->if_mtu != ifr->ifr_mtu) {
			VTNET_CORE_LOCK(sc);
			error = vtnet_change_mtu(sc, ifr->ifr_mtu);
			VTNET_CORE_UNLOCK(sc);
		}
		break;

	case SIOCSIFFLAGS:
		VTNET_CORE_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) == 0) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				vtnet_stop(sc);
		} else if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			if ((ifp->if_flags ^ sc->vtnet_if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI)) {
				if (sc->vtnet_flags & VTNET_FLAG_CTRL_RX)
					vtnet_rx_filter(sc);
				else {
					ifp->if_flags |= IFF_PROMISC;
					if ((ifp->if_flags ^ sc->vtnet_if_flags)
					    & IFF_ALLMULTI)
						error = ENOTSUP;
				}
			}
		} else
			vtnet_init_locked(sc);

		if (error == 0)
			sc->vtnet_if_flags = ifp->if_flags;
		VTNET_CORE_UNLOCK(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if ((sc->vtnet_flags & VTNET_FLAG_CTRL_RX) == 0)
			break;
		VTNET_CORE_LOCK(sc);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			vtnet_rx_filter_mac(sc);
		VTNET_CORE_UNLOCK(sc);
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->vtnet_media, cmd);
		break;

	case SIOCSIFCAP:
		VTNET_CORE_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;

		if (mask & IFCAP_TXCSUM)
			ifp->if_capenable ^= IFCAP_TXCSUM;
		if (mask & IFCAP_TXCSUM_IPV6)
			ifp->if_capenable ^= IFCAP_TXCSUM_IPV6;
		if (mask & IFCAP_TSO4)
			ifp->if_capenable ^= IFCAP_TSO4;
		if (mask & IFCAP_TSO6)
			ifp->if_capenable ^= IFCAP_TSO6;

		if (mask & (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6 | IFCAP_LRO |
		    IFCAP_VLAN_HWFILTER)) {
			/* These Rx features require us to renegotiate. */
			reinit = 1;

			if (mask & IFCAP_RXCSUM)
				ifp->if_capenable ^= IFCAP_RXCSUM;
			if (mask & IFCAP_RXCSUM_IPV6)
				ifp->if_capenable ^= IFCAP_RXCSUM_IPV6;
			if (mask & IFCAP_LRO)
				ifp->if_capenable ^= IFCAP_LRO;
			if (mask & IFCAP_VLAN_HWFILTER)
				ifp->if_capenable ^= IFCAP_VLAN_HWFILTER;
		} else
			reinit = 0;

		if (mask & IFCAP_VLAN_HWTSO)
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;
		if (mask & IFCAP_VLAN_HWTAGGING)
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;

		if (reinit && (ifp->if_drv_flags & IFF_DRV_RUNNING)) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			vtnet_init_locked(sc);
		}

		VTNET_CORE_UNLOCK(sc);
		VLAN_CAPABILITIES(ifp);

		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	VTNET_CORE_LOCK_ASSERT_NOTOWNED(sc);

	return (error);
}

static int
vtnet_rxq_populate(struct vtnet_rxq *rxq)
{
	struct virtqueue *vq;
	int nbufs, error;

#ifdef DEV_NETMAP
	error = vtnet_netmap_rxq_populate(rxq);
	if (error >= 0)
		return (error);
#endif  /* DEV_NETMAP */

	vq = rxq->vtnrx_vq;
	error = ENOSPC;

	for (nbufs = 0; !virtqueue_full(vq); nbufs++) {
		error = vtnet_rxq_new_buf(rxq);
		if (error)
			break;
	}

	if (nbufs > 0) {
		virtqueue_notify(vq);
		/*
		 * EMSGSIZE signifies the virtqueue did not have enough
		 * entries available to hold the last mbuf. This is not
		 * an error.
		 */
		if (error == EMSGSIZE)
			error = 0;
	}

	return (error);
}

static void
vtnet_rxq_free_mbufs(struct vtnet_rxq *rxq)
{
	struct virtqueue *vq;
	struct mbuf *m;
	int last;
#ifdef DEV_NETMAP
	int netmap_bufs = vtnet_netmap_queue_on(rxq->vtnrx_sc, NR_RX,
						rxq->vtnrx_id);
#else  /* !DEV_NETMAP */
	int netmap_bufs = 0;
#endif /* !DEV_NETMAP */

	vq = rxq->vtnrx_vq;
	last = 0;

	while ((m = virtqueue_drain(vq, &last)) != NULL) {
		if (!netmap_bufs)
			m_freem(m);
	}

	KASSERT(virtqueue_empty(vq),
	    ("%s: mbufs remaining in rx queue %p", __func__, rxq));
}

static struct mbuf *
vtnet_rx_alloc_buf(struct vtnet_softc *sc, int nbufs, struct mbuf **m_tailp)
{
	struct mbuf *m_head, *m_tail, *m;
	int i, clsize;

	clsize = sc->vtnet_rx_clsize;

	KASSERT(nbufs == 1 || sc->vtnet_flags & VTNET_FLAG_LRO_NOMRG,
	    ("%s: chained mbuf %d request without LRO_NOMRG", __func__, nbufs));

	m_head = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, clsize);
	if (m_head == NULL)
		goto fail;

	m_head->m_len = clsize;
	m_tail = m_head;

	/* Allocate the rest of the chain. */
	for (i = 1; i < nbufs; i++) {
		m = m_getjcl(M_NOWAIT, MT_DATA, 0, clsize);
		if (m == NULL)
			goto fail;

		m->m_len = clsize;
		m_tail->m_next = m;
		m_tail = m;
	}

	if (m_tailp != NULL)
		*m_tailp = m_tail;

	return (m_head);

fail:
	sc->vtnet_stats.mbuf_alloc_failed++;
	m_freem(m_head);

	return (NULL);
}

/*
 * Slow path for when LRO without mergeable buffers is negotiated.
 */
static int
vtnet_rxq_replace_lro_nomgr_buf(struct vtnet_rxq *rxq, struct mbuf *m0,
    int len0)
{
	struct vtnet_softc *sc;
	struct mbuf *m, *m_prev;
	struct mbuf *m_new, *m_tail;
	int len, clsize, nreplace, error;

	sc = rxq->vtnrx_sc;
	clsize = sc->vtnet_rx_clsize;

	m_prev = NULL;
	m_tail = NULL;
	nreplace = 0;

	m = m0;
	len = len0;

	/*
	 * Since these mbuf chains are so large, we avoid allocating an
	 * entire replacement chain if possible. When the received frame
	 * did not consume the entire chain, the unused mbufs are moved
	 * to the replacement chain.
	 */
	while (len > 0) {
		/*
		 * Something is seriously wrong if we received a frame
		 * larger than the chain. Drop it.
		 */
		if (m == NULL) {
			sc->vtnet_stats.rx_frame_too_large++;
			return (EMSGSIZE);
		}

		/* We always allocate the same cluster size. */
		KASSERT(m->m_len == clsize,
		    ("%s: mbuf size %d is not the cluster size %d",
		    __func__, m->m_len, clsize));

		m->m_len = MIN(m->m_len, len);
		len -= m->m_len;

		m_prev = m;
		m = m->m_next;
		nreplace++;
	}

	KASSERT(nreplace <= sc->vtnet_rx_nmbufs,
	    ("%s: too many replacement mbufs %d max %d", __func__, nreplace,
	    sc->vtnet_rx_nmbufs));

	m_new = vtnet_rx_alloc_buf(sc, nreplace, &m_tail);
	if (m_new == NULL) {
		m_prev->m_len = clsize;
		return (ENOBUFS);
	}

	/*
	 * Move any unused mbufs from the received chain onto the end
	 * of the new chain.
	 */
	if (m_prev->m_next != NULL) {
		m_tail->m_next = m_prev->m_next;
		m_prev->m_next = NULL;
	}

	error = vtnet_rxq_enqueue_buf(rxq, m_new);
	if (error) {
		/*
		 * BAD! We could not enqueue the replacement mbuf chain. We
		 * must restore the m0 chain to the original state if it was
		 * modified so we can subsequently discard it.
		 *
		 * NOTE: The replacement is suppose to be an identical copy
		 * to the one just dequeued so this is an unexpected error.
		 */
		sc->vtnet_stats.rx_enq_replacement_failed++;

		if (m_tail->m_next != NULL) {
			m_prev->m_next = m_tail->m_next;
			m_tail->m_next = NULL;
		}

		m_prev->m_len = clsize;
		m_freem(m_new);
	}

	return (error);
}

static int
vtnet_rxq_replace_buf(struct vtnet_rxq *rxq, struct mbuf *m, int len)
{
	struct vtnet_softc *sc;
	struct mbuf *m_new;
	int error;

	sc = rxq->vtnrx_sc;

	KASSERT(sc->vtnet_flags & VTNET_FLAG_LRO_NOMRG || m->m_next == NULL,
	    ("%s: chained mbuf without LRO_NOMRG", __func__));

	if (m->m_next == NULL) {
		/* Fast-path for the common case of just one mbuf. */
		if (m->m_len < len)
			return (EINVAL);

		m_new = vtnet_rx_alloc_buf(sc, 1, NULL);
		if (m_new == NULL)
			return (ENOBUFS);

		error = vtnet_rxq_enqueue_buf(rxq, m_new);
		if (error) {
			/*
			 * The new mbuf is suppose to be an identical
			 * copy of the one just dequeued so this is an
			 * unexpected error.
			 */
			m_freem(m_new);
			sc->vtnet_stats.rx_enq_replacement_failed++;
		} else
			m->m_len = len;
	} else
		error = vtnet_rxq_replace_lro_nomgr_buf(rxq, m, len);

	return (error);
}

static int
vtnet_rxq_enqueue_buf(struct vtnet_rxq *rxq, struct mbuf *m)
{
	struct vtnet_softc *sc;
	struct sglist *sg;
	struct vtnet_rx_header *rxhdr;
	uint8_t *mdata;
	int offset, error;

	sc = rxq->vtnrx_sc;
	sg = rxq->vtnrx_sg;
	mdata = mtod(m, uint8_t *);

	VTNET_RXQ_LOCK_ASSERT(rxq);
	KASSERT(sc->vtnet_flags & VTNET_FLAG_LRO_NOMRG || m->m_next == NULL,
	    ("%s: chained mbuf without LRO_NOMRG", __func__));
	KASSERT(m->m_len == sc->vtnet_rx_clsize,
	    ("%s: unexpected cluster size %d/%d", __func__, m->m_len,
	     sc->vtnet_rx_clsize));

	sglist_reset(sg);
	if ((sc->vtnet_flags & VTNET_FLAG_MRG_RXBUFS) == 0) {
		MPASS(sc->vtnet_hdr_size == sizeof(struct virtio_net_hdr));
		rxhdr = (struct vtnet_rx_header *) mdata;
		sglist_append(sg, &rxhdr->vrh_hdr, sc->vtnet_hdr_size);
		offset = sizeof(struct vtnet_rx_header);
	} else
		offset = 0;

	sglist_append(sg, mdata + offset, m->m_len - offset);
	if (m->m_next != NULL) {
		error = sglist_append_mbuf(sg, m->m_next);
		MPASS(error == 0);
	}

	error = virtqueue_enqueue(rxq->vtnrx_vq, m, sg, 0, sg->sg_nseg);

	return (error);
}

static int
vtnet_rxq_new_buf(struct vtnet_rxq *rxq)
{
	struct vtnet_softc *sc;
	struct mbuf *m;
	int error;

	sc = rxq->vtnrx_sc;

	m = vtnet_rx_alloc_buf(sc, sc->vtnet_rx_nmbufs, NULL);
	if (m == NULL)
		return (ENOBUFS);

	error = vtnet_rxq_enqueue_buf(rxq, m);
	if (error)
		m_freem(m);

	return (error);
}

/*
 * Use the checksum offset in the VirtIO header to set the
 * correct CSUM_* flags.
 */
static int
vtnet_rxq_csum_by_offset(struct vtnet_rxq *rxq, struct mbuf *m,
    uint16_t eth_type, int ip_start, struct virtio_net_hdr *hdr)
{
	struct vtnet_softc *sc;
#if defined(INET) || defined(INET6)
	int offset = hdr->csum_start + hdr->csum_offset;
#endif

	sc = rxq->vtnrx_sc;

	/* Only do a basic sanity check on the offset. */
	switch (eth_type) {
#if defined(INET)
	case ETHERTYPE_IP:
		if (__predict_false(offset < ip_start + sizeof(struct ip)))
			return (1);
		break;
#endif
#if defined(INET6)
	case ETHERTYPE_IPV6:
		if (__predict_false(offset < ip_start + sizeof(struct ip6_hdr)))
			return (1);
		break;
#endif
	default:
		sc->vtnet_stats.rx_csum_bad_ethtype++;
		return (1);
	}

	/*
	 * Use the offset to determine the appropriate CSUM_* flags. This is
	 * a bit dirty, but we can get by with it since the checksum offsets
	 * happen to be different. We assume the host host does not do IPv4
	 * header checksum offloading.
	 */
	switch (hdr->csum_offset) {
	case offsetof(struct udphdr, uh_sum):
	case offsetof(struct tcphdr, th_sum):
		m->m_pkthdr.csum_flags |= CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
		m->m_pkthdr.csum_data = 0xFFFF;
		break;
	case offsetof(struct sctphdr, checksum):
		m->m_pkthdr.csum_flags |= CSUM_SCTP_VALID;
		break;
	default:
		sc->vtnet_stats.rx_csum_bad_offset++;
		return (1);
	}

	return (0);
}

static int
vtnet_rxq_csum_by_parse(struct vtnet_rxq *rxq, struct mbuf *m,
    uint16_t eth_type, int ip_start, struct virtio_net_hdr *hdr)
{
	struct vtnet_softc *sc;
	int offset, proto;

	sc = rxq->vtnrx_sc;

	switch (eth_type) {
#if defined(INET)
	case ETHERTYPE_IP: {
		struct ip *ip;
		if (__predict_false(m->m_len < ip_start + sizeof(struct ip)))
			return (1);
		ip = (struct ip *)(m->m_data + ip_start);
		proto = ip->ip_p;
		offset = ip_start + (ip->ip_hl << 2);
		break;
	}
#endif
#if defined(INET6)
	case ETHERTYPE_IPV6:
		if (__predict_false(m->m_len < ip_start +
		    sizeof(struct ip6_hdr)))
			return (1);
		offset = ip6_lasthdr(m, ip_start, IPPROTO_IPV6, &proto);
		if (__predict_false(offset < 0))
			return (1);
		break;
#endif
	default:
		sc->vtnet_stats.rx_csum_bad_ethtype++;
		return (1);
	}

	switch (proto) {
	case IPPROTO_TCP:
		if (__predict_false(m->m_len < offset + sizeof(struct tcphdr)))
			return (1);
		m->m_pkthdr.csum_flags |= CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
		m->m_pkthdr.csum_data = 0xFFFF;
		break;
	case IPPROTO_UDP:
		if (__predict_false(m->m_len < offset + sizeof(struct udphdr)))
			return (1);
		m->m_pkthdr.csum_flags |= CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
		m->m_pkthdr.csum_data = 0xFFFF;
		break;
	case IPPROTO_SCTP:
		if (__predict_false(m->m_len < offset + sizeof(struct sctphdr)))
			return (1);
		m->m_pkthdr.csum_flags |= CSUM_SCTP_VALID;
		break;
	default:
		/*
		 * For the remaining protocols, FreeBSD does not support
		 * checksum offloading, so the checksum will be recomputed.
		 */
#if 0
		if_printf(sc->vtnet_ifp, "cksum offload of unsupported "
		    "protocol eth_type=%#x proto=%d csum_start=%d "
		    "csum_offset=%d\n", __func__, eth_type, proto,
		    hdr->csum_start, hdr->csum_offset);
#endif
		break;
	}

	return (0);
}

/*
 * Set the appropriate CSUM_* flags. Unfortunately, the information
 * provided is not directly useful to us. The VirtIO header gives the
 * offset of the checksum, which is all Linux needs, but this is not
 * how FreeBSD does things. We are forced to peek inside the packet
 * a bit.
 *
 * It would be nice if VirtIO gave us the L4 protocol or if FreeBSD
 * could accept the offsets and let the stack figure it out.
 */
static int
vtnet_rxq_csum(struct vtnet_rxq *rxq, struct mbuf *m,
    struct virtio_net_hdr *hdr)
{
	struct ether_header *eh;
	struct ether_vlan_header *evh;
	uint16_t eth_type;
	int offset, error;

	eh = mtod(m, struct ether_header *);
	eth_type = ntohs(eh->ether_type);
	if (eth_type == ETHERTYPE_VLAN) {
		/* BMV: We should handle nested VLAN tags too. */
		evh = mtod(m, struct ether_vlan_header *);
		eth_type = ntohs(evh->evl_proto);
		offset = sizeof(struct ether_vlan_header);
	} else
		offset = sizeof(struct ether_header);

	if (hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM)
		error = vtnet_rxq_csum_by_offset(rxq, m, eth_type, offset, hdr);
	else
		error = vtnet_rxq_csum_by_parse(rxq, m, eth_type, offset, hdr);

	return (error);
}

static void
vtnet_rxq_discard_merged_bufs(struct vtnet_rxq *rxq, int nbufs)
{
	struct mbuf *m;

	while (--nbufs > 0) {
		m = virtqueue_dequeue(rxq->vtnrx_vq, NULL);
		if (m == NULL)
			break;
		vtnet_rxq_discard_buf(rxq, m);
	}
}

static void
vtnet_rxq_discard_buf(struct vtnet_rxq *rxq, struct mbuf *m)
{
	int error;

	/*
	 * Requeue the discarded mbuf. This should always be successful
	 * since it was just dequeued.
	 */
	error = vtnet_rxq_enqueue_buf(rxq, m);
	KASSERT(error == 0,
	    ("%s: cannot requeue discarded mbuf %d", __func__, error));
}

static int
vtnet_rxq_merged_eof(struct vtnet_rxq *rxq, struct mbuf *m_head, int nbufs)
{
	struct vtnet_softc *sc;
	struct virtqueue *vq;
	struct mbuf *m, *m_tail;
	int len;

	sc = rxq->vtnrx_sc;
	vq = rxq->vtnrx_vq;
	m_tail = m_head;

	while (--nbufs > 0) {
		m = virtqueue_dequeue(vq, &len);
		if (m == NULL) {
			rxq->vtnrx_stats.vrxs_ierrors++;
			goto fail;
		}

		if (vtnet_rxq_new_buf(rxq) != 0) {
			rxq->vtnrx_stats.vrxs_iqdrops++;
			vtnet_rxq_discard_buf(rxq, m);
			if (nbufs > 1)
				vtnet_rxq_discard_merged_bufs(rxq, nbufs);
			goto fail;
		}

		if (m->m_len < len)
			len = m->m_len;

		m->m_len = len;
		m->m_flags &= ~M_PKTHDR;

		m_head->m_pkthdr.len += len;
		m_tail->m_next = m;
		m_tail = m;
	}

	return (0);

fail:
	sc->vtnet_stats.rx_mergeable_failed++;
	m_freem(m_head);

	return (1);
}

static void
vtnet_rxq_input(struct vtnet_rxq *rxq, struct mbuf *m,
    struct virtio_net_hdr *hdr)
{
	struct vtnet_softc *sc;
	struct ifnet *ifp;
	struct ether_header *eh;

	sc = rxq->vtnrx_sc;
	ifp = sc->vtnet_ifp;

	if (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) {
		eh = mtod(m, struct ether_header *);
		if (eh->ether_type == htons(ETHERTYPE_VLAN)) {
			vtnet_vlan_tag_remove(m);
			/*
			 * With the 802.1Q header removed, update the
			 * checksum starting location accordingly.
			 */
			if (hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM)
				hdr->csum_start -= ETHER_VLAN_ENCAP_LEN;
		}
	}

	m->m_pkthdr.flowid = rxq->vtnrx_id;
	M_HASHTYPE_SET(m, M_HASHTYPE_OPAQUE);

	/*
	 * BMV: FreeBSD does not have the UNNECESSARY and PARTIAL checksum
	 * distinction that Linux does. Need to reevaluate if performing
	 * offloading for the NEEDS_CSUM case is really appropriate.
	 */
	if (hdr->flags & (VIRTIO_NET_HDR_F_NEEDS_CSUM |
	    VIRTIO_NET_HDR_F_DATA_VALID)) {
		if (vtnet_rxq_csum(rxq, m, hdr) == 0)
			rxq->vtnrx_stats.vrxs_csum++;
		else
			rxq->vtnrx_stats.vrxs_csum_failed++;
	}

	rxq->vtnrx_stats.vrxs_ipackets++;
	rxq->vtnrx_stats.vrxs_ibytes += m->m_pkthdr.len;

	VTNET_RXQ_UNLOCK(rxq);
	(*ifp->if_input)(ifp, m);
	VTNET_RXQ_LOCK(rxq);
}

static int
vtnet_rxq_eof(struct vtnet_rxq *rxq)
{
	struct virtio_net_hdr lhdr, *hdr;
	struct vtnet_softc *sc;
	struct ifnet *ifp;
	struct virtqueue *vq;
	struct mbuf *m;
	struct virtio_net_hdr_mrg_rxbuf *mhdr;
	int len, deq, nbufs, adjsz, count;

	sc = rxq->vtnrx_sc;
	vq = rxq->vtnrx_vq;
	ifp = sc->vtnet_ifp;
	hdr = &lhdr;
	deq = 0;
	count = sc->vtnet_rx_process_limit;

	VTNET_RXQ_LOCK_ASSERT(rxq);

	while (count-- > 0) {
		m = virtqueue_dequeue(vq, &len);
		if (m == NULL)
			break;
		deq++;

		if (len < sc->vtnet_hdr_size + ETHER_HDR_LEN) {
			rxq->vtnrx_stats.vrxs_ierrors++;
			vtnet_rxq_discard_buf(rxq, m);
			continue;
		}

		if ((sc->vtnet_flags & VTNET_FLAG_MRG_RXBUFS) == 0) {
			nbufs = 1;
			adjsz = sizeof(struct vtnet_rx_header);
			/*
			 * Account for our pad inserted between the header
			 * and the actual start of the frame.
			 */
			len += VTNET_RX_HEADER_PAD;
		} else {
			mhdr = mtod(m, struct virtio_net_hdr_mrg_rxbuf *);
			nbufs = mhdr->num_buffers;
			adjsz = sizeof(struct virtio_net_hdr_mrg_rxbuf);
		}

		if (vtnet_rxq_replace_buf(rxq, m, len) != 0) {
			rxq->vtnrx_stats.vrxs_iqdrops++;
			vtnet_rxq_discard_buf(rxq, m);
			if (nbufs > 1)
				vtnet_rxq_discard_merged_bufs(rxq, nbufs);
			continue;
		}

		m->m_pkthdr.len = len;
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.csum_flags = 0;

		if (nbufs > 1) {
			/* Dequeue the rest of chain. */
			if (vtnet_rxq_merged_eof(rxq, m, nbufs) != 0)
				continue;
		}

		/*
		 * Save copy of header before we strip it. For both mergeable
		 * and non-mergeable, the header is at the beginning of the
		 * mbuf data. We no longer need num_buffers, so always use a
		 * regular header.
		 *
		 * BMV: Is this memcpy() expensive? We know the mbuf data is
		 * still valid even after the m_adj().
		 */
		memcpy(hdr, mtod(m, void *), sizeof(struct virtio_net_hdr));
		m_adj(m, adjsz);

		vtnet_rxq_input(rxq, m, hdr);

		/* Must recheck after dropping the Rx lock. */
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;
	}

	if (deq > 0)
		virtqueue_notify(vq);

	return (count > 0 ? 0 : EAGAIN);
}

static void
vtnet_rx_vq_intr(void *xrxq)
{
	struct vtnet_softc *sc;
	struct vtnet_rxq *rxq;
	struct ifnet *ifp;
	int tries, more;

	rxq = xrxq;
	sc = rxq->vtnrx_sc;
	ifp = sc->vtnet_ifp;
	tries = 0;

	if (__predict_false(rxq->vtnrx_id >= sc->vtnet_act_vq_pairs)) {
		/*
		 * Ignore this interrupt. Either this is a spurious interrupt
		 * or multiqueue without per-VQ MSIX so every queue needs to
		 * be polled (a brain dead configuration we could try harder
		 * to avoid).
		 */
		vtnet_rxq_disable_intr(rxq);
		return;
	}

#ifdef DEV_NETMAP
	if (netmap_rx_irq(ifp, rxq->vtnrx_id, &more) != NM_IRQ_PASS)
		return;
#endif /* DEV_NETMAP */

	VTNET_RXQ_LOCK(rxq);

again:
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		VTNET_RXQ_UNLOCK(rxq);
		return;
	}

	more = vtnet_rxq_eof(rxq);
	if (more || vtnet_rxq_enable_intr(rxq) != 0) {
		if (!more)
			vtnet_rxq_disable_intr(rxq);
		/*
		 * This is an occasional condition or race (when !more),
		 * so retry a few times before scheduling the taskqueue.
		 */
		if (tries++ < VTNET_INTR_DISABLE_RETRIES)
			goto again;

		VTNET_RXQ_UNLOCK(rxq);
		rxq->vtnrx_stats.vrxs_rescheduled++;
		taskqueue_enqueue(rxq->vtnrx_tq, &rxq->vtnrx_intrtask);
	} else
		VTNET_RXQ_UNLOCK(rxq);
}

static void
vtnet_rxq_tq_intr(void *xrxq, int pending)
{
	struct vtnet_softc *sc;
	struct vtnet_rxq *rxq;
	struct ifnet *ifp;
	int more;

	rxq = xrxq;
	sc = rxq->vtnrx_sc;
	ifp = sc->vtnet_ifp;

	VTNET_RXQ_LOCK(rxq);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		VTNET_RXQ_UNLOCK(rxq);
		return;
	}

	more = vtnet_rxq_eof(rxq);
	if (more || vtnet_rxq_enable_intr(rxq) != 0) {
		if (!more)
			vtnet_rxq_disable_intr(rxq);
		rxq->vtnrx_stats.vrxs_rescheduled++;
		taskqueue_enqueue(rxq->vtnrx_tq, &rxq->vtnrx_intrtask);
	}

	VTNET_RXQ_UNLOCK(rxq);
}

static int
vtnet_txq_below_threshold(struct vtnet_txq *txq)
{
	struct vtnet_softc *sc;
	struct virtqueue *vq;

	sc = txq->vtntx_sc;
	vq = txq->vtntx_vq;

	return (virtqueue_nfree(vq) <= sc->vtnet_tx_intr_thresh);
}

static int
vtnet_txq_notify(struct vtnet_txq *txq)
{
	struct virtqueue *vq;

	vq = txq->vtntx_vq;

	txq->vtntx_watchdog = VTNET_TX_TIMEOUT;
	virtqueue_notify(vq);

	if (vtnet_txq_enable_intr(txq) == 0)
		return (0);

	/*
	 * Drain frames that were completed since last checked. If this
	 * causes the queue to go above the threshold, the caller should
	 * continue transmitting.
	 */
	if (vtnet_txq_eof(txq) != 0 && vtnet_txq_below_threshold(txq) == 0) {
		virtqueue_disable_intr(vq);
		return (1);
	}

	return (0);
}

static void
vtnet_txq_free_mbufs(struct vtnet_txq *txq)
{
	struct virtqueue *vq;
	struct vtnet_tx_header *txhdr;
	int last;
#ifdef DEV_NETMAP
	int netmap_bufs = vtnet_netmap_queue_on(txq->vtntx_sc, NR_TX,
						txq->vtntx_id);
#else  /* !DEV_NETMAP */
	int netmap_bufs = 0;
#endif /* !DEV_NETMAP */

	vq = txq->vtntx_vq;
	last = 0;

	while ((txhdr = virtqueue_drain(vq, &last)) != NULL) {
		if (!netmap_bufs) {
			m_freem(txhdr->vth_mbuf);
			uma_zfree(vtnet_tx_header_zone, txhdr);
		}
	}

	KASSERT(virtqueue_empty(vq),
	    ("%s: mbufs remaining in tx queue %p", __func__, txq));
}

/*
 * BMV: Much of this can go away once we finally have offsets in
 * the mbuf packet header. Bug andre@.
 */
static int
vtnet_txq_offload_ctx(struct vtnet_txq *txq, struct mbuf *m,
    int *etype, int *proto, int *start)
{
	struct vtnet_softc *sc;
	struct ether_vlan_header *evh;
	int offset;

	sc = txq->vtntx_sc;

	evh = mtod(m, struct ether_vlan_header *);
	if (evh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		/* BMV: We should handle nested VLAN tags too. */
		*etype = ntohs(evh->evl_proto);
		offset = sizeof(struct ether_vlan_header);
	} else {
		*etype = ntohs(evh->evl_encap_proto);
		offset = sizeof(struct ether_header);
	}

	switch (*etype) {
#if defined(INET)
	case ETHERTYPE_IP: {
		struct ip *ip, iphdr;
		if (__predict_false(m->m_len < offset + sizeof(struct ip))) {
			m_copydata(m, offset, sizeof(struct ip),
			    (caddr_t) &iphdr);
			ip = &iphdr;
		} else
			ip = (struct ip *)(m->m_data + offset);
		*proto = ip->ip_p;
		*start = offset + (ip->ip_hl << 2);
		break;
	}
#endif
#if defined(INET6)
	case ETHERTYPE_IPV6:
		*proto = -1;
		*start = ip6_lasthdr(m, offset, IPPROTO_IPV6, proto);
		/* Assert the network stack sent us a valid packet. */
		KASSERT(*start > offset,
		    ("%s: mbuf %p start %d offset %d proto %d", __func__, m,
		    *start, offset, *proto));
		break;
#endif
	default:
		sc->vtnet_stats.tx_csum_bad_ethtype++;
		return (EINVAL);
	}

	return (0);
}

static int
vtnet_txq_offload_tso(struct vtnet_txq *txq, struct mbuf *m, int eth_type,
    int offset, struct virtio_net_hdr *hdr)
{
	static struct timeval lastecn;
	static int curecn;
	struct vtnet_softc *sc;
	struct tcphdr *tcp, tcphdr;

	sc = txq->vtntx_sc;

	if (__predict_false(m->m_len < offset + sizeof(struct tcphdr))) {
		m_copydata(m, offset, sizeof(struct tcphdr), (caddr_t) &tcphdr);
		tcp = &tcphdr;
	} else
		tcp = (struct tcphdr *)(m->m_data + offset);

	hdr->hdr_len = offset + (tcp->th_off << 2);
	hdr->gso_size = m->m_pkthdr.tso_segsz;
	hdr->gso_type = eth_type == ETHERTYPE_IP ? VIRTIO_NET_HDR_GSO_TCPV4 :
	    VIRTIO_NET_HDR_GSO_TCPV6;

	if (tcp->th_flags & TH_CWR) {
		/*
		 * Drop if VIRTIO_NET_F_HOST_ECN was not negotiated. In FreeBSD,
		 * ECN support is not on a per-interface basis, but globally via
		 * the net.inet.tcp.ecn.enable sysctl knob. The default is off.
		 */
		if ((sc->vtnet_flags & VTNET_FLAG_TSO_ECN) == 0) {
			if (ppsratecheck(&lastecn, &curecn, 1))
				if_printf(sc->vtnet_ifp,
				    "TSO with ECN not negotiated with host\n");
			return (ENOTSUP);
		}
		hdr->gso_type |= VIRTIO_NET_HDR_GSO_ECN;
	}

	txq->vtntx_stats.vtxs_tso++;

	return (0);
}

static struct mbuf *
vtnet_txq_offload(struct vtnet_txq *txq, struct mbuf *m,
    struct virtio_net_hdr *hdr)
{
	struct vtnet_softc *sc;
	int flags, etype, csum_start, proto, error;

	sc = txq->vtntx_sc;
	flags = m->m_pkthdr.csum_flags;

	error = vtnet_txq_offload_ctx(txq, m, &etype, &proto, &csum_start);
	if (error)
		goto drop;

	if ((etype == ETHERTYPE_IP && flags & VTNET_CSUM_OFFLOAD) ||
	    (etype == ETHERTYPE_IPV6 && flags & VTNET_CSUM_OFFLOAD_IPV6)) {
		/*
		 * We could compare the IP protocol vs the CSUM_ flag too,
		 * but that really should not be necessary.
		 */
		hdr->flags |= VIRTIO_NET_HDR_F_NEEDS_CSUM;
		hdr->csum_start = csum_start;
		hdr->csum_offset = m->m_pkthdr.csum_data;
		txq->vtntx_stats.vtxs_csum++;
	}

	if (flags & CSUM_TSO) {
		if (__predict_false(proto != IPPROTO_TCP)) {
			/* Likely failed to correctly parse the mbuf. */
			sc->vtnet_stats.tx_tso_not_tcp++;
			goto drop;
		}

		KASSERT(hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM,
		    ("%s: mbuf %p TSO without checksum offload %#x",
		    __func__, m, flags));

		error = vtnet_txq_offload_tso(txq, m, etype, csum_start, hdr);
		if (error)
			goto drop;
	}

	return (m);

drop:
	m_freem(m);
	return (NULL);
}

static int
vtnet_txq_enqueue_buf(struct vtnet_txq *txq, struct mbuf **m_head,
    struct vtnet_tx_header *txhdr)
{
	struct vtnet_softc *sc;
	struct virtqueue *vq;
	struct sglist *sg;
	struct mbuf *m;
	int error;

	sc = txq->vtntx_sc;
	vq = txq->vtntx_vq;
	sg = txq->vtntx_sg;
	m = *m_head;

	sglist_reset(sg);
	error = sglist_append(sg, &txhdr->vth_uhdr, sc->vtnet_hdr_size);
	KASSERT(error == 0 && sg->sg_nseg == 1,
	    ("%s: error %d adding header to sglist", __func__, error));

	error = sglist_append_mbuf(sg, m);
	if (error) {
		m = m_defrag(m, M_NOWAIT);
		if (m == NULL)
			goto fail;

		*m_head = m;
		sc->vtnet_stats.tx_defragged++;

		error = sglist_append_mbuf(sg, m);
		if (error)
			goto fail;
	}

	txhdr->vth_mbuf = m;
	error = virtqueue_enqueue(vq, txhdr, sg, sg->sg_nseg, 0);

	return (error);

fail:
	sc->vtnet_stats.tx_defrag_failed++;
	m_freem(*m_head);
	*m_head = NULL;

	return (ENOBUFS);
}

static int
vtnet_txq_encap(struct vtnet_txq *txq, struct mbuf **m_head, int flags)
{
	struct vtnet_tx_header *txhdr;
	struct virtio_net_hdr *hdr;
	struct mbuf *m;
	int error;

	m = *m_head;
	M_ASSERTPKTHDR(m);

	txhdr = uma_zalloc(vtnet_tx_header_zone, flags | M_ZERO);
	if (txhdr == NULL) {
		m_freem(m);
		*m_head = NULL;
		return (ENOMEM);
	}

	/*
	 * Always use the non-mergeable header, regardless if the feature
	 * was negotiated. For transmit, num_buffers is always zero. The
	 * vtnet_hdr_size is used to enqueue the correct header size.
	 */
	hdr = &txhdr->vth_uhdr.hdr;

	if (m->m_flags & M_VLANTAG) {
		m = ether_vlanencap(m, m->m_pkthdr.ether_vtag);
		if ((*m_head = m) == NULL) {
			error = ENOBUFS;
			goto fail;
		}
		m->m_flags &= ~M_VLANTAG;
	}

	if (m->m_pkthdr.csum_flags & VTNET_CSUM_ALL_OFFLOAD) {
		m = vtnet_txq_offload(txq, m, hdr);
		if ((*m_head = m) == NULL) {
			error = ENOBUFS;
			goto fail;
		}
	}

	error = vtnet_txq_enqueue_buf(txq, m_head, txhdr);
	if (error == 0)
		return (0);

fail:
	uma_zfree(vtnet_tx_header_zone, txhdr);

	return (error);
}

#ifdef VTNET_LEGACY_TX

static void
vtnet_start_locked(struct vtnet_txq *txq, struct ifnet *ifp)
{
	struct vtnet_softc *sc;
	struct virtqueue *vq;
	struct mbuf *m0;
	int tries, enq;

	sc = txq->vtntx_sc;
	vq = txq->vtntx_vq;
	tries = 0;

	VTNET_TXQ_LOCK_ASSERT(txq);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    sc->vtnet_link_active == 0)
		return;

	vtnet_txq_eof(txq);

again:
	enq = 0;

	while (!IFQ_DRV_IS_EMPTY(&ifp->if_snd)) {
		if (virtqueue_full(vq))
			break;

		IFQ_DRV_DEQUEUE(&ifp->if_snd, m0);
		if (m0 == NULL)
			break;

		if (vtnet_txq_encap(txq, &m0, M_NOWAIT) != 0) {
			if (m0 != NULL)
				IFQ_DRV_PREPEND(&ifp->if_snd, m0);
			break;
		}

		enq++;
		ETHER_BPF_MTAP(ifp, m0);
	}

	if (enq > 0 && vtnet_txq_notify(txq) != 0) {
		if (tries++ < VTNET_NOTIFY_RETRIES)
			goto again;

		txq->vtntx_stats.vtxs_rescheduled++;
		taskqueue_enqueue(txq->vtntx_tq, &txq->vtntx_intrtask);
	}
}

static void
vtnet_start(struct ifnet *ifp)
{
	struct vtnet_softc *sc;
	struct vtnet_txq *txq;

	sc = ifp->if_softc;
	txq = &sc->vtnet_txqs[0];

	VTNET_TXQ_LOCK(txq);
	vtnet_start_locked(txq, ifp);
	VTNET_TXQ_UNLOCK(txq);
}

#else /* !VTNET_LEGACY_TX */

static int
vtnet_txq_mq_start_locked(struct vtnet_txq *txq, struct mbuf *m)
{
	struct vtnet_softc *sc;
	struct virtqueue *vq;
	struct buf_ring *br;
	struct ifnet *ifp;
	int enq, tries, error;

	sc = txq->vtntx_sc;
	vq = txq->vtntx_vq;
	br = txq->vtntx_br;
	ifp = sc->vtnet_ifp;
	tries = 0;
	error = 0;

	VTNET_TXQ_LOCK_ASSERT(txq);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    sc->vtnet_link_active == 0) {
		if (m != NULL)
			error = drbr_enqueue(ifp, br, m);
		return (error);
	}

	if (m != NULL) {
		error = drbr_enqueue(ifp, br, m);
		if (error)
			return (error);
	}

	vtnet_txq_eof(txq);

again:
	enq = 0;

	while ((m = drbr_peek(ifp, br)) != NULL) {
		if (virtqueue_full(vq)) {
			drbr_putback(ifp, br, m);
			break;
		}

		if (vtnet_txq_encap(txq, &m, M_NOWAIT) != 0) {
			if (m != NULL)
				drbr_putback(ifp, br, m);
			else
				drbr_advance(ifp, br);
			break;
		}
		drbr_advance(ifp, br);

		enq++;
		ETHER_BPF_MTAP(ifp, m);
	}

	if (enq > 0 && vtnet_txq_notify(txq) != 0) {
		if (tries++ < VTNET_NOTIFY_RETRIES)
			goto again;

		txq->vtntx_stats.vtxs_rescheduled++;
		taskqueue_enqueue(txq->vtntx_tq, &txq->vtntx_intrtask);
	}

	return (0);
}

static int
vtnet_txq_mq_start(struct ifnet *ifp, struct mbuf *m)
{
	struct vtnet_softc *sc;
	struct vtnet_txq *txq;
	int i, npairs, error;

	sc = ifp->if_softc;
	npairs = sc->vtnet_act_vq_pairs;

	/* check if flowid is set */
	if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE)
		i = m->m_pkthdr.flowid % npairs;
	else
		i = curcpu % npairs;

	txq = &sc->vtnet_txqs[i];

	if (VTNET_TXQ_TRYLOCK(txq) != 0) {
		error = vtnet_txq_mq_start_locked(txq, m);
		VTNET_TXQ_UNLOCK(txq);
	} else {
		error = drbr_enqueue(ifp, txq->vtntx_br, m);
		taskqueue_enqueue(txq->vtntx_tq, &txq->vtntx_defrtask);
	}

	return (error);
}

static void
vtnet_txq_tq_deferred(void *xtxq, int pending)
{
	struct vtnet_softc *sc;
	struct vtnet_txq *txq;

	txq = xtxq;
	sc = txq->vtntx_sc;

	VTNET_TXQ_LOCK(txq);
	if (!drbr_empty(sc->vtnet_ifp, txq->vtntx_br))
		vtnet_txq_mq_start_locked(txq, NULL);
	VTNET_TXQ_UNLOCK(txq);
}

#endif /* VTNET_LEGACY_TX */

static void
vtnet_txq_start(struct vtnet_txq *txq)
{
	struct vtnet_softc *sc;
	struct ifnet *ifp;

	sc = txq->vtntx_sc;
	ifp = sc->vtnet_ifp;

#ifdef VTNET_LEGACY_TX
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		vtnet_start_locked(txq, ifp);
#else
	if (!drbr_empty(ifp, txq->vtntx_br))
		vtnet_txq_mq_start_locked(txq, NULL);
#endif
}

static void
vtnet_txq_tq_intr(void *xtxq, int pending)
{
	struct vtnet_softc *sc;
	struct vtnet_txq *txq;
	struct ifnet *ifp;

	txq = xtxq;
	sc = txq->vtntx_sc;
	ifp = sc->vtnet_ifp;

	VTNET_TXQ_LOCK(txq);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		VTNET_TXQ_UNLOCK(txq);
		return;
	}

	vtnet_txq_eof(txq);
	vtnet_txq_start(txq);

	VTNET_TXQ_UNLOCK(txq);
}

static int
vtnet_txq_eof(struct vtnet_txq *txq)
{
	struct virtqueue *vq;
	struct vtnet_tx_header *txhdr;
	struct mbuf *m;
	int deq;

	vq = txq->vtntx_vq;
	deq = 0;
	VTNET_TXQ_LOCK_ASSERT(txq);

	while ((txhdr = virtqueue_dequeue(vq, NULL)) != NULL) {
		m = txhdr->vth_mbuf;
		deq++;

		txq->vtntx_stats.vtxs_opackets++;
		txq->vtntx_stats.vtxs_obytes += m->m_pkthdr.len;
		if (m->m_flags & M_MCAST)
			txq->vtntx_stats.vtxs_omcasts++;

		m_freem(m);
		uma_zfree(vtnet_tx_header_zone, txhdr);
	}

	if (virtqueue_empty(vq))
		txq->vtntx_watchdog = 0;

	return (deq);
}

static void
vtnet_tx_vq_intr(void *xtxq)
{
	struct vtnet_softc *sc;
	struct vtnet_txq *txq;
	struct ifnet *ifp;

	txq = xtxq;
	sc = txq->vtntx_sc;
	ifp = sc->vtnet_ifp;

	if (__predict_false(txq->vtntx_id >= sc->vtnet_act_vq_pairs)) {
		/*
		 * Ignore this interrupt. Either this is a spurious interrupt
		 * or multiqueue without per-VQ MSIX so every queue needs to
		 * be polled (a brain dead configuration we could try harder
		 * to avoid).
		 */
		vtnet_txq_disable_intr(txq);
		return;
	}

#ifdef DEV_NETMAP
	if (netmap_tx_irq(ifp, txq->vtntx_id) != NM_IRQ_PASS)
		return;
#endif /* DEV_NETMAP */

	VTNET_TXQ_LOCK(txq);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		VTNET_TXQ_UNLOCK(txq);
		return;
	}

	vtnet_txq_eof(txq);
	vtnet_txq_start(txq);

	VTNET_TXQ_UNLOCK(txq);
}

static void
vtnet_tx_start_all(struct vtnet_softc *sc)
{
	struct vtnet_txq *txq;
	int i;

	VTNET_CORE_LOCK_ASSERT(sc);

	for (i = 0; i < sc->vtnet_act_vq_pairs; i++) {
		txq = &sc->vtnet_txqs[i];

		VTNET_TXQ_LOCK(txq);
		vtnet_txq_start(txq);
		VTNET_TXQ_UNLOCK(txq);
	}
}

#ifndef VTNET_LEGACY_TX
static void
vtnet_qflush(struct ifnet *ifp)
{
	struct vtnet_softc *sc;
	struct vtnet_txq *txq;
	struct mbuf *m;
	int i;

	sc = ifp->if_softc;

	for (i = 0; i < sc->vtnet_act_vq_pairs; i++) {
		txq = &sc->vtnet_txqs[i];

		VTNET_TXQ_LOCK(txq);
		while ((m = buf_ring_dequeue_sc(txq->vtntx_br)) != NULL)
			m_freem(m);
		VTNET_TXQ_UNLOCK(txq);
	}

	if_qflush(ifp);
}
#endif

static int
vtnet_watchdog(struct vtnet_txq *txq)
{
	struct ifnet *ifp;

	ifp = txq->vtntx_sc->vtnet_ifp;

	VTNET_TXQ_LOCK(txq);
	if (txq->vtntx_watchdog == 1) {
		/*
		 * Only drain completed frames if the watchdog is about to
		 * expire. If any frames were drained, there may be enough
		 * free descriptors now available to transmit queued frames.
		 * In that case, the timer will immediately be decremented
		 * below, but the timeout is generous enough that should not
		 * be a problem.
		 */
		if (vtnet_txq_eof(txq) != 0)
			vtnet_txq_start(txq);
	}

	if (txq->vtntx_watchdog == 0 || --txq->vtntx_watchdog) {
		VTNET_TXQ_UNLOCK(txq);
		return (0);
	}
	VTNET_TXQ_UNLOCK(txq);

	if_printf(ifp, "watchdog timeout on queue %d\n", txq->vtntx_id);
	return (1);
}

static void
vtnet_accum_stats(struct vtnet_softc *sc, struct vtnet_rxq_stats *rxacc,
    struct vtnet_txq_stats *txacc)
{

	bzero(rxacc, sizeof(struct vtnet_rxq_stats));
	bzero(txacc, sizeof(struct vtnet_txq_stats));

	for (int i = 0; i < sc->vtnet_max_vq_pairs; i++) {
		struct vtnet_rxq_stats *rxst;
		struct vtnet_txq_stats *txst;

		rxst = &sc->vtnet_rxqs[i].vtnrx_stats;
		rxacc->vrxs_ipackets += rxst->vrxs_ipackets;
		rxacc->vrxs_ibytes += rxst->vrxs_ibytes;
		rxacc->vrxs_iqdrops += rxst->vrxs_iqdrops;
		rxacc->vrxs_csum += rxst->vrxs_csum;
		rxacc->vrxs_csum_failed += rxst->vrxs_csum_failed;
		rxacc->vrxs_rescheduled += rxst->vrxs_rescheduled;

		txst = &sc->vtnet_txqs[i].vtntx_stats;
		txacc->vtxs_opackets += txst->vtxs_opackets;
		txacc->vtxs_obytes += txst->vtxs_obytes;
		txacc->vtxs_csum += txst->vtxs_csum;
		txacc->vtxs_tso += txst->vtxs_tso;
		txacc->vtxs_rescheduled += txst->vtxs_rescheduled;
	}
}

static uint64_t
vtnet_get_counter(if_t ifp, ift_counter cnt)
{
	struct vtnet_softc *sc;
	struct vtnet_rxq_stats rxaccum;
	struct vtnet_txq_stats txaccum;

	sc = if_getsoftc(ifp);
	vtnet_accum_stats(sc, &rxaccum, &txaccum);

	switch (cnt) {
	case IFCOUNTER_IPACKETS:
		return (rxaccum.vrxs_ipackets);
	case IFCOUNTER_IQDROPS:
		return (rxaccum.vrxs_iqdrops);
	case IFCOUNTER_IERRORS:
		return (rxaccum.vrxs_ierrors);
	case IFCOUNTER_OPACKETS:
		return (txaccum.vtxs_opackets);
#ifndef VTNET_LEGACY_TX
	case IFCOUNTER_OBYTES:
		return (txaccum.vtxs_obytes);
	case IFCOUNTER_OMCASTS:
		return (txaccum.vtxs_omcasts);
#endif
	default:
		return (if_get_counter_default(ifp, cnt));
	}
}

static void
vtnet_tick(void *xsc)
{
	struct vtnet_softc *sc;
	struct ifnet *ifp;
	int i, timedout;

	sc = xsc;
	ifp = sc->vtnet_ifp;
	timedout = 0;

	VTNET_CORE_LOCK_ASSERT(sc);

	for (i = 0; i < sc->vtnet_act_vq_pairs; i++)
		timedout |= vtnet_watchdog(&sc->vtnet_txqs[i]);

	if (timedout != 0) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		vtnet_init_locked(sc);
	} else
		callout_schedule(&sc->vtnet_tick_ch, hz);
}

static void
vtnet_start_taskqueues(struct vtnet_softc *sc)
{
	device_t dev;
	struct vtnet_rxq *rxq;
	struct vtnet_txq *txq;
	int i, error;

	dev = sc->vtnet_dev;

	/*
	 * Errors here are very difficult to recover from - we cannot
	 * easily fail because, if this is during boot, we will hang
	 * when freeing any successfully started taskqueues because
	 * the scheduler isn't up yet.
	 *
	 * Most drivers just ignore the return value - it only fails
	 * with ENOMEM so an error is not likely.
	 */
	for (i = 0; i < sc->vtnet_max_vq_pairs; i++) {
		rxq = &sc->vtnet_rxqs[i];
		error = taskqueue_start_threads(&rxq->vtnrx_tq, 1, PI_NET,
		    "%s rxq %d", device_get_nameunit(dev), rxq->vtnrx_id);
		if (error) {
			device_printf(dev, "failed to start rx taskq %d\n",
			    rxq->vtnrx_id);
		}

		txq = &sc->vtnet_txqs[i];
		error = taskqueue_start_threads(&txq->vtntx_tq, 1, PI_NET,
		    "%s txq %d", device_get_nameunit(dev), txq->vtntx_id);
		if (error) {
			device_printf(dev, "failed to start tx taskq %d\n",
			    txq->vtntx_id);
		}
	}
}

static void
vtnet_free_taskqueues(struct vtnet_softc *sc)
{
	struct vtnet_rxq *rxq;
	struct vtnet_txq *txq;
	int i;

	for (i = 0; i < sc->vtnet_max_vq_pairs; i++) {
		rxq = &sc->vtnet_rxqs[i];
		if (rxq->vtnrx_tq != NULL) {
			taskqueue_free(rxq->vtnrx_tq);
			rxq->vtnrx_tq = NULL;
		}

		txq = &sc->vtnet_txqs[i];
		if (txq->vtntx_tq != NULL) {
			taskqueue_free(txq->vtntx_tq);
			txq->vtntx_tq = NULL;
		}
	}
}

static void
vtnet_drain_taskqueues(struct vtnet_softc *sc)
{
	struct vtnet_rxq *rxq;
	struct vtnet_txq *txq;
	int i;

	for (i = 0; i < sc->vtnet_max_vq_pairs; i++) {
		rxq = &sc->vtnet_rxqs[i];
		if (rxq->vtnrx_tq != NULL)
			taskqueue_drain(rxq->vtnrx_tq, &rxq->vtnrx_intrtask);

		txq = &sc->vtnet_txqs[i];
		if (txq->vtntx_tq != NULL) {
			taskqueue_drain(txq->vtntx_tq, &txq->vtntx_intrtask);
#ifndef VTNET_LEGACY_TX
			taskqueue_drain(txq->vtntx_tq, &txq->vtntx_defrtask);
#endif
		}
	}
}

static void
vtnet_drain_rxtx_queues(struct vtnet_softc *sc)
{
	struct vtnet_rxq *rxq;
	struct vtnet_txq *txq;
	int i;

	for (i = 0; i < sc->vtnet_act_vq_pairs; i++) {
		rxq = &sc->vtnet_rxqs[i];
		vtnet_rxq_free_mbufs(rxq);

		txq = &sc->vtnet_txqs[i];
		vtnet_txq_free_mbufs(txq);
	}
}

static void
vtnet_stop_rendezvous(struct vtnet_softc *sc)
{
	struct vtnet_rxq *rxq;
	struct vtnet_txq *txq;
	int i;

	/*
	 * Lock and unlock the per-queue mutex so we known the stop
	 * state is visible. Doing only the active queues should be
	 * sufficient, but it does not cost much extra to do all the
	 * queues. Note we hold the core mutex here too.
	 */
	for (i = 0; i < sc->vtnet_max_vq_pairs; i++) {
		rxq = &sc->vtnet_rxqs[i];
		VTNET_RXQ_LOCK(rxq);
		VTNET_RXQ_UNLOCK(rxq);

		txq = &sc->vtnet_txqs[i];
		VTNET_TXQ_LOCK(txq);
		VTNET_TXQ_UNLOCK(txq);
	}
}

static void
vtnet_stop(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;

	dev = sc->vtnet_dev;
	ifp = sc->vtnet_ifp;

	VTNET_CORE_LOCK_ASSERT(sc);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	sc->vtnet_link_active = 0;
	callout_stop(&sc->vtnet_tick_ch);

	/* Only advisory. */
	vtnet_disable_interrupts(sc);

	/*
	 * Stop the host adapter. This resets it to the pre-initialized
	 * state. It will not generate any interrupts until after it is
	 * reinitialized.
	 */
	virtio_stop(dev);
	vtnet_stop_rendezvous(sc);

	/* Free any mbufs left in the virtqueues. */
	vtnet_drain_rxtx_queues(sc);
}

static int
vtnet_virtio_reinit(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;
	uint64_t features;
	int mask, error;

	dev = sc->vtnet_dev;
	ifp = sc->vtnet_ifp;
	features = sc->vtnet_features;

	mask = 0;
#if defined(INET)
	mask |= IFCAP_RXCSUM;
#endif
#if defined (INET6)
	mask |= IFCAP_RXCSUM_IPV6;
#endif

	/*
	 * Re-negotiate with the host, removing any disabled receive
	 * features. Transmit features are disabled only on our side
	 * via if_capenable and if_hwassist.
	 */

	if (ifp->if_capabilities & mask) {
		/*
		 * We require both IPv4 and IPv6 offloading to be enabled
		 * in order to negotiated it: VirtIO does not distinguish
		 * between the two.
		 */
		if ((ifp->if_capenable & mask) != mask)
			features &= ~VIRTIO_NET_F_GUEST_CSUM;
	}

	if (ifp->if_capabilities & IFCAP_LRO) {
		if ((ifp->if_capenable & IFCAP_LRO) == 0)
			features &= ~VTNET_LRO_FEATURES;
	}

	if (ifp->if_capabilities & IFCAP_VLAN_HWFILTER) {
		if ((ifp->if_capenable & IFCAP_VLAN_HWFILTER) == 0)
			features &= ~VIRTIO_NET_F_CTRL_VLAN;
	}

	error = virtio_reinit(dev, features);
	if (error)
		device_printf(dev, "virtio reinit error %d\n", error);

	return (error);
}

static void
vtnet_init_rx_filters(struct vtnet_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->vtnet_ifp;

	if (sc->vtnet_flags & VTNET_FLAG_CTRL_RX) {
		/* Restore promiscuous and all-multicast modes. */
		vtnet_rx_filter(sc);
		/* Restore filtered MAC addresses. */
		vtnet_rx_filter_mac(sc);
	}

	if (ifp->if_capenable & IFCAP_VLAN_HWFILTER)
		vtnet_rx_filter_vlan(sc);
}

static int
vtnet_init_rx_queues(struct vtnet_softc *sc)
{
	device_t dev;
	struct vtnet_rxq *rxq;
	int i, clsize, error;

	dev = sc->vtnet_dev;

	/*
	 * Use the new cluster size if one has been set (via a MTU
	 * change). Otherwise, use the standard 2K clusters.
	 *
	 * BMV: It might make sense to use page sized clusters as
	 * the default (depending on the features negotiated).
	 */
	if (sc->vtnet_rx_new_clsize != 0) {
		clsize = sc->vtnet_rx_new_clsize;
		sc->vtnet_rx_new_clsize = 0;
	} else
		clsize = MCLBYTES;

	sc->vtnet_rx_clsize = clsize;
	sc->vtnet_rx_nmbufs = VTNET_NEEDED_RX_MBUFS(sc, clsize);

	KASSERT(sc->vtnet_flags & VTNET_FLAG_MRG_RXBUFS ||
	    sc->vtnet_rx_nmbufs < sc->vtnet_rx_nsegs,
	    ("%s: too many rx mbufs %d for %d segments", __func__,
	    sc->vtnet_rx_nmbufs, sc->vtnet_rx_nsegs));

	for (i = 0; i < sc->vtnet_act_vq_pairs; i++) {
		rxq = &sc->vtnet_rxqs[i];

		/* Hold the lock to satisfy asserts. */
		VTNET_RXQ_LOCK(rxq);
		error = vtnet_rxq_populate(rxq);
		VTNET_RXQ_UNLOCK(rxq);

		if (error) {
			device_printf(dev,
			    "cannot allocate mbufs for Rx queue %d\n", i);
			return (error);
		}
	}

	return (0);
}

static int
vtnet_init_tx_queues(struct vtnet_softc *sc)
{
	struct vtnet_txq *txq;
	int i;

	for (i = 0; i < sc->vtnet_act_vq_pairs; i++) {
		txq = &sc->vtnet_txqs[i];
		txq->vtntx_watchdog = 0;
	}

	return (0);
}

static int
vtnet_init_rxtx_queues(struct vtnet_softc *sc)
{
	int error;

	error = vtnet_init_rx_queues(sc);
	if (error)
		return (error);

	error = vtnet_init_tx_queues(sc);
	if (error)
		return (error);

	return (0);
}

static void
vtnet_set_active_vq_pairs(struct vtnet_softc *sc)
{
	device_t dev;
	int npairs;

	dev = sc->vtnet_dev;

	if ((sc->vtnet_flags & VTNET_FLAG_MULTIQ) == 0) {
		sc->vtnet_act_vq_pairs = 1;
		return;
	}

	npairs = sc->vtnet_requested_vq_pairs;

	if (vtnet_ctrl_mq_cmd(sc, npairs) != 0) {
		device_printf(dev,
		    "cannot set active queue pairs to %d\n", npairs);
		npairs = 1;
	}

	sc->vtnet_act_vq_pairs = npairs;
}

static int
vtnet_reinit(struct vtnet_softc *sc)
{
	struct ifnet *ifp;
	int error;

	ifp = sc->vtnet_ifp;

	/* Use the current MAC address. */
	bcopy(IF_LLADDR(ifp), sc->vtnet_hwaddr, ETHER_ADDR_LEN);
	vtnet_set_hwaddr(sc);

	vtnet_set_active_vq_pairs(sc);

	ifp->if_hwassist = 0;
	if (ifp->if_capenable & IFCAP_TXCSUM)
		ifp->if_hwassist |= VTNET_CSUM_OFFLOAD;
	if (ifp->if_capenable & IFCAP_TXCSUM_IPV6)
		ifp->if_hwassist |= VTNET_CSUM_OFFLOAD_IPV6;
	if (ifp->if_capenable & IFCAP_TSO4)
		ifp->if_hwassist |= CSUM_IP_TSO;
	if (ifp->if_capenable & IFCAP_TSO6)
		ifp->if_hwassist |= CSUM_IP6_TSO;

	if (sc->vtnet_flags & VTNET_FLAG_CTRL_VQ)
		vtnet_init_rx_filters(sc);

	error = vtnet_init_rxtx_queues(sc);
	if (error)
		return (error);

	vtnet_enable_interrupts(sc);
	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	return (0);
}

static void
vtnet_init_locked(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;

	dev = sc->vtnet_dev;
	ifp = sc->vtnet_ifp;

	VTNET_CORE_LOCK_ASSERT(sc);

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	vtnet_stop(sc);

	/* Reinitialize with the host. */
	if (vtnet_virtio_reinit(sc) != 0)
		goto fail;

	if (vtnet_reinit(sc) != 0)
		goto fail;

	virtio_reinit_complete(dev);

	vtnet_update_link_status(sc);
	callout_reset(&sc->vtnet_tick_ch, hz, vtnet_tick, sc);

	return;

fail:
	vtnet_stop(sc);
}

static void
vtnet_init(void *xsc)
{
	struct vtnet_softc *sc;

	sc = xsc;

	VTNET_CORE_LOCK(sc);
	vtnet_init_locked(sc);
	VTNET_CORE_UNLOCK(sc);
}

static void
vtnet_free_ctrl_vq(struct vtnet_softc *sc)
{
	struct virtqueue *vq;

	vq = sc->vtnet_ctrl_vq;

	/*
	 * The control virtqueue is only polled and therefore it should
	 * already be empty.
	 */
	KASSERT(virtqueue_empty(vq),
	    ("%s: ctrl vq %p not empty", __func__, vq));
}

static void
vtnet_exec_ctrl_cmd(struct vtnet_softc *sc, void *cookie,
    struct sglist *sg, int readable, int writable)
{
	struct virtqueue *vq;

	vq = sc->vtnet_ctrl_vq;

	VTNET_CORE_LOCK_ASSERT(sc);
	KASSERT(sc->vtnet_flags & VTNET_FLAG_CTRL_VQ,
	    ("%s: CTRL_VQ feature not negotiated", __func__));

	if (!virtqueue_empty(vq))
		return;
	if (virtqueue_enqueue(vq, cookie, sg, readable, writable) != 0)
		return;

	/*
	 * Poll for the response, but the command is likely already
	 * done when we return from the notify.
	 */
	virtqueue_notify(vq);
	virtqueue_poll(vq, NULL);
}

static int
vtnet_ctrl_mac_cmd(struct vtnet_softc *sc, uint8_t *hwaddr)
{
	struct virtio_net_ctrl_hdr hdr __aligned(2);
	struct sglist_seg segs[3];
	struct sglist sg;
	uint8_t ack;
	int error;

	hdr.class = VIRTIO_NET_CTRL_MAC;
	hdr.cmd = VIRTIO_NET_CTRL_MAC_ADDR_SET;
	ack = VIRTIO_NET_ERR;

	sglist_init(&sg, 3, segs);
	error = 0;
	error |= sglist_append(&sg, &hdr, sizeof(struct virtio_net_ctrl_hdr));
	error |= sglist_append(&sg, hwaddr, ETHER_ADDR_LEN);
	error |= sglist_append(&sg, &ack, sizeof(uint8_t));
	KASSERT(error == 0 && sg.sg_nseg == 3,
	    ("%s: error %d adding set MAC msg to sglist", __func__, error));

	vtnet_exec_ctrl_cmd(sc, &ack, &sg, sg.sg_nseg - 1, 1);

	return (ack == VIRTIO_NET_OK ? 0 : EIO);
}

static int
vtnet_ctrl_mq_cmd(struct vtnet_softc *sc, uint16_t npairs)
{
	struct sglist_seg segs[3];
	struct sglist sg;
	struct {
		struct virtio_net_ctrl_hdr hdr;
		uint8_t pad1;
		struct virtio_net_ctrl_mq mq;
		uint8_t pad2;
		uint8_t ack;
	} s __aligned(2);
	int error;

	s.hdr.class = VIRTIO_NET_CTRL_MQ;
	s.hdr.cmd = VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET;
	s.mq.virtqueue_pairs = npairs;
	s.ack = VIRTIO_NET_ERR;

	sglist_init(&sg, 3, segs);
	error = 0;
	error |= sglist_append(&sg, &s.hdr, sizeof(struct virtio_net_ctrl_hdr));
	error |= sglist_append(&sg, &s.mq, sizeof(struct virtio_net_ctrl_mq));
	error |= sglist_append(&sg, &s.ack, sizeof(uint8_t));
	KASSERT(error == 0 && sg.sg_nseg == 3,
	    ("%s: error %d adding MQ message to sglist", __func__, error));

	vtnet_exec_ctrl_cmd(sc, &s.ack, &sg, sg.sg_nseg - 1, 1);

	return (s.ack == VIRTIO_NET_OK ? 0 : EIO);
}

static int
vtnet_ctrl_rx_cmd(struct vtnet_softc *sc, int cmd, int on)
{
	struct sglist_seg segs[3];
	struct sglist sg;
	struct {
		struct virtio_net_ctrl_hdr hdr;
		uint8_t pad1;
		uint8_t onoff;
		uint8_t pad2;
		uint8_t ack;
	} s __aligned(2);
	int error;

	KASSERT(sc->vtnet_flags & VTNET_FLAG_CTRL_RX,
	    ("%s: CTRL_RX feature not negotiated", __func__));

	s.hdr.class = VIRTIO_NET_CTRL_RX;
	s.hdr.cmd = cmd;
	s.onoff = !!on;
	s.ack = VIRTIO_NET_ERR;

	sglist_init(&sg, 3, segs);
	error = 0;
	error |= sglist_append(&sg, &s.hdr, sizeof(struct virtio_net_ctrl_hdr));
	error |= sglist_append(&sg, &s.onoff, sizeof(uint8_t));
	error |= sglist_append(&sg, &s.ack, sizeof(uint8_t));
	KASSERT(error == 0 && sg.sg_nseg == 3,
	    ("%s: error %d adding Rx message to sglist", __func__, error));

	vtnet_exec_ctrl_cmd(sc, &s.ack, &sg, sg.sg_nseg - 1, 1);

	return (s.ack == VIRTIO_NET_OK ? 0 : EIO);
}

static int
vtnet_set_promisc(struct vtnet_softc *sc, int on)
{

	return (vtnet_ctrl_rx_cmd(sc, VIRTIO_NET_CTRL_RX_PROMISC, on));
}

static int
vtnet_set_allmulti(struct vtnet_softc *sc, int on)
{

	return (vtnet_ctrl_rx_cmd(sc, VIRTIO_NET_CTRL_RX_ALLMULTI, on));
}

/*
 * The device defaults to promiscuous mode for backwards compatibility.
 * Turn it off at attach time if possible.
 */
static void
vtnet_attach_disable_promisc(struct vtnet_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->vtnet_ifp;

	VTNET_CORE_LOCK(sc);
	if ((sc->vtnet_flags & VTNET_FLAG_CTRL_RX) == 0) {
		ifp->if_flags |= IFF_PROMISC;
	} else if (vtnet_set_promisc(sc, 0) != 0) {
		ifp->if_flags |= IFF_PROMISC;
		device_printf(sc->vtnet_dev,
		    "cannot disable default promiscuous mode\n");
	}
	VTNET_CORE_UNLOCK(sc);
}

static void
vtnet_rx_filter(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;

	dev = sc->vtnet_dev;
	ifp = sc->vtnet_ifp;

	VTNET_CORE_LOCK_ASSERT(sc);

	if (vtnet_set_promisc(sc, ifp->if_flags & IFF_PROMISC) != 0)
		device_printf(dev, "cannot %s promiscuous mode\n",
		    ifp->if_flags & IFF_PROMISC ? "enable" : "disable");

	if (vtnet_set_allmulti(sc, ifp->if_flags & IFF_ALLMULTI) != 0)
		device_printf(dev, "cannot %s all-multicast mode\n",
		    ifp->if_flags & IFF_ALLMULTI ? "enable" : "disable");
}

static void
vtnet_rx_filter_mac(struct vtnet_softc *sc)
{
	struct virtio_net_ctrl_hdr hdr __aligned(2);
	struct vtnet_mac_filter *filter;
	struct sglist_seg segs[4];
	struct sglist sg;
	struct ifnet *ifp;
	struct ifaddr *ifa;
	struct ifmultiaddr *ifma;
	int ucnt, mcnt, promisc, allmulti, error;
	uint8_t ack;

	ifp = sc->vtnet_ifp;
	filter = sc->vtnet_mac_filter;
	ucnt = 0;
	mcnt = 0;
	promisc = 0;
	allmulti = 0;

	VTNET_CORE_LOCK_ASSERT(sc);
	KASSERT(sc->vtnet_flags & VTNET_FLAG_CTRL_RX,
	    ("%s: CTRL_RX feature not negotiated", __func__));

	/* Unicast MAC addresses: */
	if_addr_rlock(ifp);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		else if (memcmp(LLADDR((struct sockaddr_dl *)ifa->ifa_addr),
		    sc->vtnet_hwaddr, ETHER_ADDR_LEN) == 0)
			continue;
		else if (ucnt == VTNET_MAX_MAC_ENTRIES) {
			promisc = 1;
			break;
		}

		bcopy(LLADDR((struct sockaddr_dl *)ifa->ifa_addr),
		    &filter->vmf_unicast.macs[ucnt], ETHER_ADDR_LEN);
		ucnt++;
	}
	if_addr_runlock(ifp);

	if (promisc != 0) {
		filter->vmf_unicast.nentries = 0;
		if_printf(ifp, "more than %d MAC addresses assigned, "
		    "falling back to promiscuous mode\n",
		    VTNET_MAX_MAC_ENTRIES);
	} else
		filter->vmf_unicast.nentries = ucnt;

	/* Multicast MAC addresses: */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		else if (mcnt == VTNET_MAX_MAC_ENTRIES) {
			allmulti = 1;
			break;
		}

		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    &filter->vmf_multicast.macs[mcnt], ETHER_ADDR_LEN);
		mcnt++;
	}
	if_maddr_runlock(ifp);

	if (allmulti != 0) {
		filter->vmf_multicast.nentries = 0;
		if_printf(ifp, "more than %d multicast MAC addresses "
		    "assigned, falling back to all-multicast mode\n",
		    VTNET_MAX_MAC_ENTRIES);
	} else
		filter->vmf_multicast.nentries = mcnt;

	if (promisc != 0 && allmulti != 0)
		goto out;

	hdr.class = VIRTIO_NET_CTRL_MAC;
	hdr.cmd = VIRTIO_NET_CTRL_MAC_TABLE_SET;
	ack = VIRTIO_NET_ERR;

	sglist_init(&sg, 4, segs);
	error = 0;
	error |= sglist_append(&sg, &hdr, sizeof(struct virtio_net_ctrl_hdr));
	error |= sglist_append(&sg, &filter->vmf_unicast,
	    sizeof(uint32_t) + filter->vmf_unicast.nentries * ETHER_ADDR_LEN);
	error |= sglist_append(&sg, &filter->vmf_multicast,
	    sizeof(uint32_t) + filter->vmf_multicast.nentries * ETHER_ADDR_LEN);
	error |= sglist_append(&sg, &ack, sizeof(uint8_t));
	KASSERT(error == 0 && sg.sg_nseg == 4,
	    ("%s: error %d adding MAC filter msg to sglist", __func__, error));

	vtnet_exec_ctrl_cmd(sc, &ack, &sg, sg.sg_nseg - 1, 1);

	if (ack != VIRTIO_NET_OK)
		if_printf(ifp, "error setting host MAC filter table\n");

out:
	if (promisc != 0 && vtnet_set_promisc(sc, 1) != 0)
		if_printf(ifp, "cannot enable promiscuous mode\n");
	if (allmulti != 0 && vtnet_set_allmulti(sc, 1) != 0)
		if_printf(ifp, "cannot enable all-multicast mode\n");
}

static int
vtnet_exec_vlan_filter(struct vtnet_softc *sc, int add, uint16_t tag)
{
	struct sglist_seg segs[3];
	struct sglist sg;
	struct {
		struct virtio_net_ctrl_hdr hdr;
		uint8_t pad1;
		uint16_t tag;
		uint8_t pad2;
		uint8_t ack;
	} s __aligned(2);
	int error;

	s.hdr.class = VIRTIO_NET_CTRL_VLAN;
	s.hdr.cmd = add ? VIRTIO_NET_CTRL_VLAN_ADD : VIRTIO_NET_CTRL_VLAN_DEL;
	s.tag = tag;
	s.ack = VIRTIO_NET_ERR;

	sglist_init(&sg, 3, segs);
	error = 0;
	error |= sglist_append(&sg, &s.hdr, sizeof(struct virtio_net_ctrl_hdr));
	error |= sglist_append(&sg, &s.tag, sizeof(uint16_t));
	error |= sglist_append(&sg, &s.ack, sizeof(uint8_t));
	KASSERT(error == 0 && sg.sg_nseg == 3,
	    ("%s: error %d adding VLAN message to sglist", __func__, error));

	vtnet_exec_ctrl_cmd(sc, &s.ack, &sg, sg.sg_nseg - 1, 1);

	return (s.ack == VIRTIO_NET_OK ? 0 : EIO);
}

static void
vtnet_rx_filter_vlan(struct vtnet_softc *sc)
{
	uint32_t w;
	uint16_t tag;
	int i, bit;

	VTNET_CORE_LOCK_ASSERT(sc);
	KASSERT(sc->vtnet_flags & VTNET_FLAG_VLAN_FILTER,
	    ("%s: VLAN_FILTER feature not negotiated", __func__));

	/* Enable the filter for each configured VLAN. */
	for (i = 0; i < VTNET_VLAN_FILTER_NWORDS; i++) {
		w = sc->vtnet_vlan_filter[i];

		while ((bit = ffs(w) - 1) != -1) {
			w &= ~(1 << bit);
			tag = sizeof(w) * CHAR_BIT * i + bit;

			if (vtnet_exec_vlan_filter(sc, 1, tag) != 0) {
				device_printf(sc->vtnet_dev,
				    "cannot enable VLAN %d filter\n", tag);
			}
		}
	}
}

static void
vtnet_update_vlan_filter(struct vtnet_softc *sc, int add, uint16_t tag)
{
	struct ifnet *ifp;
	int idx, bit;

	ifp = sc->vtnet_ifp;
	idx = (tag >> 5) & 0x7F;
	bit = tag & 0x1F;

	if (tag == 0 || tag > 4095)
		return;

	VTNET_CORE_LOCK(sc);

	if (add)
		sc->vtnet_vlan_filter[idx] |= (1 << bit);
	else
		sc->vtnet_vlan_filter[idx] &= ~(1 << bit);

	if (ifp->if_capenable & IFCAP_VLAN_HWFILTER &&
	    ifp->if_drv_flags & IFF_DRV_RUNNING &&
	    vtnet_exec_vlan_filter(sc, add, tag) != 0) {
		device_printf(sc->vtnet_dev,
		    "cannot %s VLAN %d %s the host filter table\n",
		    add ? "add" : "remove", tag, add ? "to" : "from");
	}

	VTNET_CORE_UNLOCK(sc);
}

static void
vtnet_register_vlan(void *arg, struct ifnet *ifp, uint16_t tag)
{

	if (ifp->if_softc != arg)
		return;

	vtnet_update_vlan_filter(arg, 1, tag);
}

static void
vtnet_unregister_vlan(void *arg, struct ifnet *ifp, uint16_t tag)
{

	if (ifp->if_softc != arg)
		return;

	vtnet_update_vlan_filter(arg, 0, tag);
}

static int
vtnet_is_link_up(struct vtnet_softc *sc)
{
	device_t dev;
	struct ifnet *ifp;
	uint16_t status;

	dev = sc->vtnet_dev;
	ifp = sc->vtnet_ifp;

	if ((ifp->if_capabilities & IFCAP_LINKSTATE) == 0)
		status = VIRTIO_NET_S_LINK_UP;
	else
		status = virtio_read_dev_config_2(dev,
		    offsetof(struct virtio_net_config, status));

	return ((status & VIRTIO_NET_S_LINK_UP) != 0);
}

static void
vtnet_update_link_status(struct vtnet_softc *sc)
{
	struct ifnet *ifp;
	int link;

	ifp = sc->vtnet_ifp;

	VTNET_CORE_LOCK_ASSERT(sc);
	link = vtnet_is_link_up(sc);

	/* Notify if the link status has changed. */
	if (link != 0 && sc->vtnet_link_active == 0) {
		sc->vtnet_link_active = 1;
		if_link_state_change(ifp, LINK_STATE_UP);
	} else if (link == 0 && sc->vtnet_link_active != 0) {
		sc->vtnet_link_active = 0;
		if_link_state_change(ifp, LINK_STATE_DOWN);
	}
}

static int
vtnet_ifmedia_upd(struct ifnet *ifp)
{
	struct vtnet_softc *sc;
	struct ifmedia *ifm;

	sc = ifp->if_softc;
	ifm = &sc->vtnet_media;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	return (0);
}

static void
vtnet_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct vtnet_softc *sc;

	sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	VTNET_CORE_LOCK(sc);
	if (vtnet_is_link_up(sc) != 0) {
		ifmr->ifm_status |= IFM_ACTIVE;
		ifmr->ifm_active |= VTNET_MEDIATYPE;
	} else
		ifmr->ifm_active |= IFM_NONE;
	VTNET_CORE_UNLOCK(sc);
}

static void
vtnet_set_hwaddr(struct vtnet_softc *sc)
{
	device_t dev;
	int i;

	dev = sc->vtnet_dev;

	if (sc->vtnet_flags & VTNET_FLAG_CTRL_MAC) {
		if (vtnet_ctrl_mac_cmd(sc, sc->vtnet_hwaddr) != 0)
			device_printf(dev, "unable to set MAC address\n");
	} else if (sc->vtnet_flags & VTNET_FLAG_MAC) {
		for (i = 0; i < ETHER_ADDR_LEN; i++) {
			virtio_write_dev_config_1(dev,
			    offsetof(struct virtio_net_config, mac) + i,
			    sc->vtnet_hwaddr[i]);
		}
	}
}

static void
vtnet_get_hwaddr(struct vtnet_softc *sc)
{
	device_t dev;
	int i;

	dev = sc->vtnet_dev;

	if ((sc->vtnet_flags & VTNET_FLAG_MAC) == 0) {
		/*
		 * Generate a random locally administered unicast address.
		 *
		 * It would be nice to generate the same MAC address across
		 * reboots, but it seems all the hosts currently available
		 * support the MAC feature, so this isn't too important.
		 */
		sc->vtnet_hwaddr[0] = 0xB2;
		arc4rand(&sc->vtnet_hwaddr[1], ETHER_ADDR_LEN - 1, 0);
		vtnet_set_hwaddr(sc);
		return;
	}

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		sc->vtnet_hwaddr[i] = virtio_read_dev_config_1(dev,
		    offsetof(struct virtio_net_config, mac) + i);
	}
}

static void
vtnet_vlan_tag_remove(struct mbuf *m)
{
	struct ether_vlan_header *evh;

	evh = mtod(m, struct ether_vlan_header *);
	m->m_pkthdr.ether_vtag = ntohs(evh->evl_tag);
	m->m_flags |= M_VLANTAG;

	/* Strip the 802.1Q header. */
	bcopy((char *) evh, (char *) evh + ETHER_VLAN_ENCAP_LEN,
	    ETHER_HDR_LEN - ETHER_TYPE_LEN);
	m_adj(m, ETHER_VLAN_ENCAP_LEN);
}

static void
vtnet_set_rx_process_limit(struct vtnet_softc *sc)
{
	int limit;

	limit = vtnet_tunable_int(sc, "rx_process_limit",
	    vtnet_rx_process_limit);
	if (limit < 0)
		limit = INT_MAX;
	sc->vtnet_rx_process_limit = limit;
}

static void
vtnet_set_tx_intr_threshold(struct vtnet_softc *sc)
{
	int size, thresh;

	size = virtqueue_size(sc->vtnet_txqs[0].vtntx_vq);

	/*
	 * The Tx interrupt is disabled until the queue free count falls
	 * below our threshold. Completed frames are drained from the Tx
	 * virtqueue before transmitting new frames and in the watchdog
	 * callout, so the frequency of Tx interrupts is greatly reduced,
	 * at the cost of not freeing mbufs as quickly as they otherwise
	 * would be.
	 *
	 * N.B. We assume all the Tx queues are the same size.
	 */
	thresh = size / 4;

	/*
	 * Without indirect descriptors, leave enough room for the most
	 * segments we handle.
	 */
	if ((sc->vtnet_flags & VTNET_FLAG_INDIRECT) == 0 &&
	    thresh < sc->vtnet_tx_nsegs)
		thresh = sc->vtnet_tx_nsegs;

	sc->vtnet_tx_intr_thresh = thresh;
}

static void
vtnet_setup_rxq_sysctl(struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *child, struct vtnet_rxq *rxq)
{
	struct sysctl_oid *node;
	struct sysctl_oid_list *list;
	struct vtnet_rxq_stats *stats;
	char namebuf[16];

	snprintf(namebuf, sizeof(namebuf), "rxq%d", rxq->vtnrx_id);
	node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, namebuf,
	    CTLFLAG_RD, NULL, "Receive Queue");
	list = SYSCTL_CHILDREN(node);

	stats = &rxq->vtnrx_stats;

	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "ipackets", CTLFLAG_RD,
	    &stats->vrxs_ipackets, "Receive packets");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "ibytes", CTLFLAG_RD,
	    &stats->vrxs_ibytes, "Receive bytes");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "iqdrops", CTLFLAG_RD,
	    &stats->vrxs_iqdrops, "Receive drops");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "ierrors", CTLFLAG_RD,
	    &stats->vrxs_ierrors, "Receive errors");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "csum", CTLFLAG_RD,
	    &stats->vrxs_csum, "Receive checksum offloaded");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "csum_failed", CTLFLAG_RD,
	    &stats->vrxs_csum_failed, "Receive checksum offload failed");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "rescheduled", CTLFLAG_RD,
	    &stats->vrxs_rescheduled,
	    "Receive interrupt handler rescheduled");
}

static void
vtnet_setup_txq_sysctl(struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *child, struct vtnet_txq *txq)
{
	struct sysctl_oid *node;
	struct sysctl_oid_list *list;
	struct vtnet_txq_stats *stats;
	char namebuf[16];

	snprintf(namebuf, sizeof(namebuf), "txq%d", txq->vtntx_id);
	node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, namebuf,
	    CTLFLAG_RD, NULL, "Transmit Queue");
	list = SYSCTL_CHILDREN(node);

	stats = &txq->vtntx_stats;

	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "opackets", CTLFLAG_RD,
	    &stats->vtxs_opackets, "Transmit packets");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "obytes", CTLFLAG_RD,
	    &stats->vtxs_obytes, "Transmit bytes");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "omcasts", CTLFLAG_RD,
	    &stats->vtxs_omcasts, "Transmit multicasts");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "csum", CTLFLAG_RD,
	    &stats->vtxs_csum, "Transmit checksum offloaded");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "tso", CTLFLAG_RD,
	    &stats->vtxs_tso, "Transmit segmentation offloaded");
	SYSCTL_ADD_UQUAD(ctx, list, OID_AUTO, "rescheduled", CTLFLAG_RD,
	    &stats->vtxs_rescheduled,
	    "Transmit interrupt handler rescheduled");
}

static void
vtnet_setup_queue_sysctl(struct vtnet_softc *sc)
{
	device_t dev;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;
	int i;

	dev = sc->vtnet_dev;
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	for (i = 0; i < sc->vtnet_max_vq_pairs; i++) {
		vtnet_setup_rxq_sysctl(ctx, child, &sc->vtnet_rxqs[i]);
		vtnet_setup_txq_sysctl(ctx, child, &sc->vtnet_txqs[i]);
	}
}

static void
vtnet_setup_stat_sysctl(struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *child, struct vtnet_softc *sc)
{
	struct vtnet_statistics *stats;
	struct vtnet_rxq_stats rxaccum;
	struct vtnet_txq_stats txaccum;

	vtnet_accum_stats(sc, &rxaccum, &txaccum);

	stats = &sc->vtnet_stats;
	stats->rx_csum_offloaded = rxaccum.vrxs_csum;
	stats->rx_csum_failed = rxaccum.vrxs_csum_failed;
	stats->rx_task_rescheduled = rxaccum.vrxs_rescheduled;
	stats->tx_csum_offloaded = txaccum.vtxs_csum;
	stats->tx_tso_offloaded = txaccum.vtxs_tso;
	stats->tx_task_rescheduled = txaccum.vtxs_rescheduled;

	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "mbuf_alloc_failed",
	    CTLFLAG_RD, &stats->mbuf_alloc_failed,
	    "Mbuf cluster allocation failures");

	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_frame_too_large",
	    CTLFLAG_RD, &stats->rx_frame_too_large,
	    "Received frame larger than the mbuf chain");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_enq_replacement_failed",
	    CTLFLAG_RD, &stats->rx_enq_replacement_failed,
	    "Enqueuing the replacement receive mbuf failed");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_mergeable_failed",
	    CTLFLAG_RD, &stats->rx_mergeable_failed,
	    "Mergeable buffers receive failures");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_csum_bad_ethtype",
	    CTLFLAG_RD, &stats->rx_csum_bad_ethtype,
	    "Received checksum offloaded buffer with unsupported "
	    "Ethernet type");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_csum_bad_ipproto",
	    CTLFLAG_RD, &stats->rx_csum_bad_ipproto,
	    "Received checksum offloaded buffer with incorrect IP protocol");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_csum_bad_offset",
	    CTLFLAG_RD, &stats->rx_csum_bad_offset,
	    "Received checksum offloaded buffer with incorrect offset");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_csum_bad_proto",
	    CTLFLAG_RD, &stats->rx_csum_bad_proto,
	    "Received checksum offloaded buffer with incorrect protocol");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_csum_failed",
	    CTLFLAG_RD, &stats->rx_csum_failed,
	    "Received buffer checksum offload failed");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_csum_offloaded",
	    CTLFLAG_RD, &stats->rx_csum_offloaded,
	    "Received buffer checksum offload succeeded");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_task_rescheduled",
	    CTLFLAG_RD, &stats->rx_task_rescheduled,
	    "Times the receive interrupt task rescheduled itself");

	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "tx_csum_bad_ethtype",
	    CTLFLAG_RD, &stats->tx_csum_bad_ethtype,
	    "Aborted transmit of checksum offloaded buffer with unknown "
	    "Ethernet type");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "tx_tso_bad_ethtype",
	    CTLFLAG_RD, &stats->tx_tso_bad_ethtype,
	    "Aborted transmit of TSO buffer with unknown Ethernet type");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "tx_tso_not_tcp",
	    CTLFLAG_RD, &stats->tx_tso_not_tcp,
	    "Aborted transmit of TSO buffer with non TCP protocol");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "tx_defragged",
	    CTLFLAG_RD, &stats->tx_defragged,
	    "Transmit mbufs defragged");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "tx_defrag_failed",
	    CTLFLAG_RD, &stats->tx_defrag_failed,
	    "Aborted transmit of buffer because defrag failed");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "tx_csum_offloaded",
	    CTLFLAG_RD, &stats->tx_csum_offloaded,
	    "Offloaded checksum of transmitted buffer");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "tx_tso_offloaded",
	    CTLFLAG_RD, &stats->tx_tso_offloaded,
	    "Segmentation offload of transmitted buffer");
	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "tx_task_rescheduled",
	    CTLFLAG_RD, &stats->tx_task_rescheduled,
	    "Times the transmit interrupt task rescheduled itself");
}

static void
vtnet_setup_sysctl(struct vtnet_softc *sc)
{
	device_t dev;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;

	dev = sc->vtnet_dev;
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "max_vq_pairs",
	    CTLFLAG_RD, &sc->vtnet_max_vq_pairs, 0,
	    "Maximum number of supported virtqueue pairs");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "requested_vq_pairs",
	    CTLFLAG_RD, &sc->vtnet_requested_vq_pairs, 0,
	    "Requested number of virtqueue pairs");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "act_vq_pairs",
	    CTLFLAG_RD, &sc->vtnet_act_vq_pairs, 0,
	    "Number of active virtqueue pairs");

	vtnet_setup_stat_sysctl(ctx, child, sc);
}

static int
vtnet_rxq_enable_intr(struct vtnet_rxq *rxq)
{

	return (virtqueue_enable_intr(rxq->vtnrx_vq));
}

static void
vtnet_rxq_disable_intr(struct vtnet_rxq *rxq)
{

	virtqueue_disable_intr(rxq->vtnrx_vq);
}

static int
vtnet_txq_enable_intr(struct vtnet_txq *txq)
{
	struct virtqueue *vq;

	vq = txq->vtntx_vq;

	if (vtnet_txq_below_threshold(txq) != 0)
		return (virtqueue_postpone_intr(vq, VQ_POSTPONE_LONG));

	/*
	 * The free count is above our threshold. Keep the Tx interrupt
	 * disabled until the queue is fuller.
	 */
	return (0);
}

static void
vtnet_txq_disable_intr(struct vtnet_txq *txq)
{

	virtqueue_disable_intr(txq->vtntx_vq);
}

static void
vtnet_enable_rx_interrupts(struct vtnet_softc *sc)
{
	int i;

	for (i = 0; i < sc->vtnet_act_vq_pairs; i++)
		vtnet_rxq_enable_intr(&sc->vtnet_rxqs[i]);
}

static void
vtnet_enable_tx_interrupts(struct vtnet_softc *sc)
{
	int i;

	for (i = 0; i < sc->vtnet_act_vq_pairs; i++)
		vtnet_txq_enable_intr(&sc->vtnet_txqs[i]);
}

static void
vtnet_enable_interrupts(struct vtnet_softc *sc)
{

	vtnet_enable_rx_interrupts(sc);
	vtnet_enable_tx_interrupts(sc);
}

static void
vtnet_disable_rx_interrupts(struct vtnet_softc *sc)
{
	int i;

	for (i = 0; i < sc->vtnet_act_vq_pairs; i++)
		vtnet_rxq_disable_intr(&sc->vtnet_rxqs[i]);
}

static void
vtnet_disable_tx_interrupts(struct vtnet_softc *sc)
{
	int i;

	for (i = 0; i < sc->vtnet_act_vq_pairs; i++)
		vtnet_txq_disable_intr(&sc->vtnet_txqs[i]);
}

static void
vtnet_disable_interrupts(struct vtnet_softc *sc)
{

	vtnet_disable_rx_interrupts(sc);
	vtnet_disable_tx_interrupts(sc);
}

static int
vtnet_tunable_int(struct vtnet_softc *sc, const char *knob, int def)
{
	char path[64];

	snprintf(path, sizeof(path),
	    "hw.vtnet.%d.%s", device_get_unit(sc->vtnet_dev), knob);
	TUNABLE_INT_FETCH(path, &def);

	return (def);
}

#ifdef NETDUMP
static void
vtnet_netdump_init(struct ifnet *ifp, int *nrxr, int *ncl, int *clsize)
{
	struct vtnet_softc *sc;

	sc = if_getsoftc(ifp);

	VTNET_CORE_LOCK(sc);
	*nrxr = sc->vtnet_max_vq_pairs;
	*ncl = NETDUMP_MAX_IN_FLIGHT;
	*clsize = sc->vtnet_rx_clsize;
	VTNET_CORE_UNLOCK(sc);

	/*
	 * We need to allocate from this zone in the transmit path, so ensure
	 * that we have at least one item per header available.
	 * XXX add a separate zone like we do for mbufs? otherwise we may alloc
	 * buckets
	 */
	uma_zone_reserve(vtnet_tx_header_zone, NETDUMP_MAX_IN_FLIGHT * 2);
	uma_prealloc(vtnet_tx_header_zone, NETDUMP_MAX_IN_FLIGHT * 2);
}

static void
vtnet_netdump_event(struct ifnet *ifp __unused, enum netdump_ev event __unused)
{
}

static int
vtnet_netdump_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct vtnet_softc *sc;
	struct vtnet_txq *txq;
	int error;

	sc = if_getsoftc(ifp);
	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return (EBUSY);

	txq = &sc->vtnet_txqs[0];
	error = vtnet_txq_encap(txq, &m, M_NOWAIT | M_USE_RESERVE);
	if (error == 0)
		(void)vtnet_txq_notify(txq);
	return (error);
}

static int
vtnet_netdump_poll(struct ifnet *ifp, int count)
{
	struct vtnet_softc *sc;
	int i;

	sc = if_getsoftc(ifp);
	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return (EBUSY);

	(void)vtnet_txq_eof(&sc->vtnet_txqs[0]);
	for (i = 0; i < sc->vtnet_max_vq_pairs; i++)
		(void)vtnet_rxq_eof(&sc->vtnet_rxqs[i]);
	return (0);
}
#endif /* NETDUMP */

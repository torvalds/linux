/*-
 * Copyright (c) 2016, Vincenzo Maffione
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
 *
 * $FreeBSD$
 */

/* Driver for ptnet paravirtualized network device. */

#include <sys/cdefs.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>
#include <sys/smp.h>
#include <sys/time.h>
#include <machine/smp.h>

#include <vm/uma.h>
#include <vm/vm.h>
#include <vm/pmap.h>

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

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/selinfo.h>
#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <net/netmap_virt.h>
#include <dev/netmap/netmap_mem2.h>
#include <dev/virtio/network/virtio_net.h>

#ifndef INET
#error "INET not defined, cannot support offloadings"
#endif

#if __FreeBSD_version >= 1100000
static uint64_t	ptnet_get_counter(if_t, ift_counter);
#else
typedef struct ifnet *if_t;
#define if_getsoftc(_ifp)   (_ifp)->if_softc
#endif

//#define PTNETMAP_STATS
//#define DEBUG
#ifdef DEBUG
#define DBG(x) x
#else   /* !DEBUG */
#define DBG(x)
#endif  /* !DEBUG */

extern int ptnet_vnet_hdr; /* Tunable parameter */

struct ptnet_softc;

struct ptnet_queue_stats {
	uint64_t	packets; /* if_[io]packets */
	uint64_t	bytes;	 /* if_[io]bytes */
	uint64_t	errors;	 /* if_[io]errors */
	uint64_t	iqdrops; /* if_iqdrops */
	uint64_t	mcasts;  /* if_[io]mcasts */
#ifdef PTNETMAP_STATS
	uint64_t	intrs;
	uint64_t	kicks;
#endif /* PTNETMAP_STATS */
};

struct ptnet_queue {
	struct ptnet_softc		*sc;
	struct				resource *irq;
	void				*cookie;
	int				kring_id;
	struct nm_csb_atok		*atok;
	struct nm_csb_ktoa		*ktoa;
	unsigned int			kick;
	struct mtx			lock;
	struct buf_ring			*bufring; /* for TX queues */
	struct ptnet_queue_stats	stats;
#ifdef PTNETMAP_STATS
	struct ptnet_queue_stats	last_stats;
#endif /* PTNETMAP_STATS */
	struct taskqueue		*taskq;
	struct task			task;
	char				lock_name[16];
};

#define PTNET_Q_LOCK(_pq)	mtx_lock(&(_pq)->lock)
#define PTNET_Q_TRYLOCK(_pq)	mtx_trylock(&(_pq)->lock)
#define PTNET_Q_UNLOCK(_pq)	mtx_unlock(&(_pq)->lock)

struct ptnet_softc {
	device_t		dev;
	if_t			ifp;
	struct ifmedia		media;
	struct mtx		lock;
	char			lock_name[16];
	char			hwaddr[ETHER_ADDR_LEN];

	/* Mirror of PTFEAT register. */
	uint32_t		ptfeatures;
	unsigned int		vnet_hdr_len;

	/* PCI BARs support. */
	struct resource		*iomem;
	struct resource		*msix_mem;

	unsigned int		num_rings;
	unsigned int		num_tx_rings;
	struct ptnet_queue	*queues;
	struct ptnet_queue	*rxqueues;
	struct nm_csb_atok	*csb_gh;
	struct nm_csb_ktoa	*csb_hg;

	unsigned int		min_tx_space;

	struct netmap_pt_guest_adapter *ptna;

	struct callout		tick;
#ifdef PTNETMAP_STATS
	struct timeval		last_ts;
#endif /* PTNETMAP_STATS */
};

#define PTNET_CORE_LOCK(_sc)	mtx_lock(&(_sc)->lock)
#define PTNET_CORE_UNLOCK(_sc)	mtx_unlock(&(_sc)->lock)

static int	ptnet_probe(device_t);
static int	ptnet_attach(device_t);
static int	ptnet_detach(device_t);
static int	ptnet_suspend(device_t);
static int	ptnet_resume(device_t);
static int	ptnet_shutdown(device_t);

static void	ptnet_init(void *opaque);
static int	ptnet_ioctl(if_t ifp, u_long cmd, caddr_t data);
static int	ptnet_init_locked(struct ptnet_softc *sc);
static int	ptnet_stop(struct ptnet_softc *sc);
static int	ptnet_transmit(if_t ifp, struct mbuf *m);
static int	ptnet_drain_transmit_queue(struct ptnet_queue *pq,
					   unsigned int budget,
					   bool may_resched);
static void	ptnet_qflush(if_t ifp);
static void	ptnet_tx_task(void *context, int pending);

static int	ptnet_media_change(if_t ifp);
static void	ptnet_media_status(if_t ifp, struct ifmediareq *ifmr);
#ifdef PTNETMAP_STATS
static void	ptnet_tick(void *opaque);
#endif

static int	ptnet_irqs_init(struct ptnet_softc *sc);
static void	ptnet_irqs_fini(struct ptnet_softc *sc);

static uint32_t ptnet_nm_ptctl(struct ptnet_softc *sc, uint32_t cmd);
static int      ptnet_nm_config(struct netmap_adapter *na,
				struct nm_config_info *info);
static void	ptnet_update_vnet_hdr(struct ptnet_softc *sc);
static int	ptnet_nm_register(struct netmap_adapter *na, int onoff);
static int	ptnet_nm_txsync(struct netmap_kring *kring, int flags);
static int	ptnet_nm_rxsync(struct netmap_kring *kring, int flags);
static void	ptnet_nm_intr(struct netmap_adapter *na, int onoff);

static void	ptnet_tx_intr(void *opaque);
static void	ptnet_rx_intr(void *opaque);

static unsigned	ptnet_rx_discard(struct netmap_kring *kring,
				 unsigned int head);
static int	ptnet_rx_eof(struct ptnet_queue *pq, unsigned int budget,
			     bool may_resched);
static void	ptnet_rx_task(void *context, int pending);

#ifdef DEVICE_POLLING
static poll_handler_t ptnet_poll;
#endif

static device_method_t ptnet_methods[] = {
	DEVMETHOD(device_probe,			ptnet_probe),
	DEVMETHOD(device_attach,		ptnet_attach),
	DEVMETHOD(device_detach,		ptnet_detach),
	DEVMETHOD(device_suspend,		ptnet_suspend),
	DEVMETHOD(device_resume,		ptnet_resume),
	DEVMETHOD(device_shutdown,		ptnet_shutdown),
	DEVMETHOD_END
};

static driver_t ptnet_driver = {
	"ptnet",
	ptnet_methods,
	sizeof(struct ptnet_softc)
};

/* We use (SI_ORDER_MIDDLE+2) here, see DEV_MODULE_ORDERED() invocation. */
static devclass_t ptnet_devclass;
DRIVER_MODULE_ORDERED(ptnet, pci, ptnet_driver, ptnet_devclass,
		      NULL, NULL, SI_ORDER_MIDDLE + 2);

static int
ptnet_probe(device_t dev)
{
	if (pci_get_vendor(dev) != PTNETMAP_PCI_VENDOR_ID ||
		pci_get_device(dev) != PTNETMAP_PCI_NETIF_ID) {
		return (ENXIO);
	}

	device_set_desc(dev, "ptnet network adapter");

	return (BUS_PROBE_DEFAULT);
}

static inline void ptnet_kick(struct ptnet_queue *pq)
{
#ifdef PTNETMAP_STATS
	pq->stats.kicks ++;
#endif /* PTNETMAP_STATS */
	bus_write_4(pq->sc->iomem, pq->kick, 0);
}

#define PTNET_BUF_RING_SIZE	4096
#define PTNET_RX_BUDGET		512
#define PTNET_RX_BATCH		1
#define PTNET_TX_BUDGET		512
#define PTNET_TX_BATCH		64
#define PTNET_HDR_SIZE		sizeof(struct virtio_net_hdr_mrg_rxbuf)
#define PTNET_MAX_PKT_SIZE	65536

#define PTNET_CSUM_OFFLOAD	(CSUM_TCP | CSUM_UDP | CSUM_SCTP)
#define PTNET_CSUM_OFFLOAD_IPV6	(CSUM_TCP_IPV6 | CSUM_UDP_IPV6 |\
				 CSUM_SCTP_IPV6)
#define PTNET_ALL_OFFLOAD	(CSUM_TSO | PTNET_CSUM_OFFLOAD |\
				 PTNET_CSUM_OFFLOAD_IPV6)

static int
ptnet_attach(device_t dev)
{
	uint32_t ptfeatures = 0;
	unsigned int num_rx_rings, num_tx_rings;
	struct netmap_adapter na_arg;
	unsigned int nifp_offset;
	struct ptnet_softc *sc;
	if_t ifp;
	uint32_t macreg;
	int err, rid;
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Setup PCI resources. */
	pci_enable_busmaster(dev);

	rid = PCIR_BAR(PTNETMAP_IO_PCI_BAR);
	sc->iomem = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
					   RF_ACTIVE);
	if (sc->iomem == NULL) {
		device_printf(dev, "Failed to map I/O BAR\n");
		return (ENXIO);
	}

	/* Negotiate features with the hypervisor. */
	if (ptnet_vnet_hdr) {
		ptfeatures |= PTNETMAP_F_VNET_HDR;
	}
	bus_write_4(sc->iomem, PTNET_IO_PTFEAT, ptfeatures); /* wanted */
	ptfeatures = bus_read_4(sc->iomem, PTNET_IO_PTFEAT); /* acked */
	sc->ptfeatures = ptfeatures;

	num_tx_rings = bus_read_4(sc->iomem, PTNET_IO_NUM_TX_RINGS);
	num_rx_rings = bus_read_4(sc->iomem, PTNET_IO_NUM_RX_RINGS);
	sc->num_rings = num_tx_rings + num_rx_rings;
	sc->num_tx_rings = num_tx_rings;

	if (sc->num_rings * sizeof(struct nm_csb_atok) > PAGE_SIZE) {
		device_printf(dev, "CSB cannot handle that many rings (%u)\n",
				sc->num_rings);
		err = ENOMEM;
		goto err_path;
	}

	/* Allocate CSB and carry out CSB allocation protocol. */
	sc->csb_gh = contigmalloc(2*PAGE_SIZE, M_DEVBUF, M_NOWAIT | M_ZERO,
				  (size_t)0, -1UL, PAGE_SIZE, 0);
	if (sc->csb_gh == NULL) {
		device_printf(dev, "Failed to allocate CSB\n");
		err = ENOMEM;
		goto err_path;
	}
	sc->csb_hg = (struct nm_csb_ktoa *)(((char *)sc->csb_gh) + PAGE_SIZE);

	{
		/*
		 * We use uint64_t rather than vm_paddr_t since we
		 * need 64 bit addresses even on 32 bit platforms.
		 */
		uint64_t paddr = vtophys(sc->csb_gh);

		/* CSB allocation protocol: write to BAH first, then
		 * to BAL (for both GH and HG sections). */
		bus_write_4(sc->iomem, PTNET_IO_CSB_GH_BAH,
				(paddr >> 32) & 0xffffffff);
		bus_write_4(sc->iomem, PTNET_IO_CSB_GH_BAL,
				paddr & 0xffffffff);
		paddr = vtophys(sc->csb_hg);
		bus_write_4(sc->iomem, PTNET_IO_CSB_HG_BAH,
				(paddr >> 32) & 0xffffffff);
		bus_write_4(sc->iomem, PTNET_IO_CSB_HG_BAL,
				paddr & 0xffffffff);
	}

	/* Allocate and initialize per-queue data structures. */
	sc->queues = malloc(sizeof(struct ptnet_queue) * sc->num_rings,
			    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->queues == NULL) {
		err = ENOMEM;
		goto err_path;
	}
	sc->rxqueues = sc->queues + num_tx_rings;

	for (i = 0; i < sc->num_rings; i++) {
		struct ptnet_queue *pq = sc->queues + i;

		pq->sc = sc;
		pq->kring_id = i;
		pq->kick = PTNET_IO_KICK_BASE + 4 * i;
		pq->atok = sc->csb_gh + i;
		pq->ktoa = sc->csb_hg + i;
		snprintf(pq->lock_name, sizeof(pq->lock_name), "%s-%d",
			 device_get_nameunit(dev), i);
		mtx_init(&pq->lock, pq->lock_name, NULL, MTX_DEF);
		if (i >= num_tx_rings) {
			/* RX queue: fix kring_id. */
			pq->kring_id -= num_tx_rings;
		} else {
			/* TX queue: allocate buf_ring. */
			pq->bufring = buf_ring_alloc(PTNET_BUF_RING_SIZE,
						M_DEVBUF, M_NOWAIT, &pq->lock);
			if (pq->bufring == NULL) {
				err = ENOMEM;
				goto err_path;
			}
		}
	}

	sc->min_tx_space = 64; /* Safe initial value. */

	err = ptnet_irqs_init(sc);
	if (err) {
		goto err_path;
	}

	/* Setup Ethernet interface. */
	sc->ifp = ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "Failed to allocate ifnet\n");
		err = ENOMEM;
		goto err_path;
	}

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_baudrate = IF_Gbps(10);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST | IFF_SIMPLEX;
	ifp->if_init = ptnet_init;
	ifp->if_ioctl = ptnet_ioctl;
#if __FreeBSD_version >= 1100000
	ifp->if_get_counter = ptnet_get_counter;
#endif
	ifp->if_transmit = ptnet_transmit;
	ifp->if_qflush = ptnet_qflush;

	ifmedia_init(&sc->media, IFM_IMASK, ptnet_media_change,
		     ptnet_media_status);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_T | IFM_FDX, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_10G_T | IFM_FDX);

	macreg = bus_read_4(sc->iomem, PTNET_IO_MAC_HI);
	sc->hwaddr[0] = (macreg >> 8) & 0xff;
	sc->hwaddr[1] = macreg & 0xff;
	macreg = bus_read_4(sc->iomem, PTNET_IO_MAC_LO);
	sc->hwaddr[2] = (macreg >> 24) & 0xff;
	sc->hwaddr[3] = (macreg >> 16) & 0xff;
	sc->hwaddr[4] = (macreg >> 8) & 0xff;
	sc->hwaddr[5] = macreg & 0xff;

	ether_ifattach(ifp, sc->hwaddr);

	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_JUMBO_MTU | IFCAP_VLAN_MTU;

	if (sc->ptfeatures & PTNETMAP_F_VNET_HDR) {
		/* Similarly to what the vtnet driver does, we can emulate
		 * VLAN offloadings by inserting and removing the 802.1Q
		 * header during transmit and receive. We are then able
		 * to do checksum offloading of VLAN frames. */
		ifp->if_capabilities |= IFCAP_HWCSUM | IFCAP_HWCSUM_IPV6
					| IFCAP_VLAN_HWCSUM
					| IFCAP_TSO | IFCAP_LRO
					| IFCAP_VLAN_HWTSO
					| IFCAP_VLAN_HWTAGGING;
	}

	ifp->if_capenable = ifp->if_capabilities;
#ifdef DEVICE_POLLING
	/* Don't enable polling by default. */
	ifp->if_capabilities |= IFCAP_POLLING;
#endif
	snprintf(sc->lock_name, sizeof(sc->lock_name),
		 "%s", device_get_nameunit(dev));
	mtx_init(&sc->lock, sc->lock_name, "ptnet core lock", MTX_DEF);
	callout_init_mtx(&sc->tick, &sc->lock, 0);

	/* Prepare a netmap_adapter struct instance to do netmap_attach(). */
	nifp_offset = bus_read_4(sc->iomem, PTNET_IO_NIFP_OFS);
	memset(&na_arg, 0, sizeof(na_arg));
	na_arg.ifp = ifp;
	na_arg.num_tx_desc = bus_read_4(sc->iomem, PTNET_IO_NUM_TX_SLOTS);
	na_arg.num_rx_desc = bus_read_4(sc->iomem, PTNET_IO_NUM_RX_SLOTS);
	na_arg.num_tx_rings = num_tx_rings;
	na_arg.num_rx_rings = num_rx_rings;
	na_arg.nm_config = ptnet_nm_config;
	na_arg.nm_krings_create = ptnet_nm_krings_create;
	na_arg.nm_krings_delete = ptnet_nm_krings_delete;
	na_arg.nm_dtor = ptnet_nm_dtor;
	na_arg.nm_intr = ptnet_nm_intr;
	na_arg.nm_register = ptnet_nm_register;
	na_arg.nm_txsync = ptnet_nm_txsync;
	na_arg.nm_rxsync = ptnet_nm_rxsync;

	netmap_pt_guest_attach(&na_arg, nifp_offset,
                                bus_read_4(sc->iomem, PTNET_IO_HOSTMEMID));

	/* Now a netmap adapter for this ifp has been allocated, and it
	 * can be accessed through NA(ifp). We also have to initialize the CSB
	 * pointer. */
	sc->ptna = (struct netmap_pt_guest_adapter *)NA(ifp);

	/* If virtio-net header was negotiated, set the virt_hdr_len field in
	 * the netmap adapter, to inform users that this netmap adapter requires
	 * the application to deal with the headers. */
	ptnet_update_vnet_hdr(sc);

	device_printf(dev, "%s() completed\n", __func__);

	return (0);

err_path:
	ptnet_detach(dev);
	return err;
}

/* Stop host sync-kloop if it was running. */
static void
ptnet_device_shutdown(struct ptnet_softc *sc)
{
	ptnet_nm_ptctl(sc, PTNETMAP_PTCTL_DELETE);
	bus_write_4(sc->iomem, PTNET_IO_CSB_GH_BAH, 0);
	bus_write_4(sc->iomem, PTNET_IO_CSB_GH_BAL, 0);
	bus_write_4(sc->iomem, PTNET_IO_CSB_HG_BAH, 0);
	bus_write_4(sc->iomem, PTNET_IO_CSB_HG_BAL, 0);
}

static int
ptnet_detach(device_t dev)
{
	struct ptnet_softc *sc = device_get_softc(dev);
	int i;

	ptnet_device_shutdown(sc);

#ifdef DEVICE_POLLING
	if (sc->ifp->if_capenable & IFCAP_POLLING) {
		ether_poll_deregister(sc->ifp);
	}
#endif
	callout_drain(&sc->tick);

	if (sc->queues) {
		/* Drain taskqueues before calling if_detach. */
		for (i = 0; i < sc->num_rings; i++) {
			struct ptnet_queue *pq = sc->queues + i;

			if (pq->taskq) {
				taskqueue_drain(pq->taskq, &pq->task);
			}
		}
	}

	if (sc->ifp) {
		ether_ifdetach(sc->ifp);

		/* Uninitialize netmap adapters for this device. */
		netmap_detach(sc->ifp);

		ifmedia_removeall(&sc->media);
		if_free(sc->ifp);
		sc->ifp = NULL;
	}

	ptnet_irqs_fini(sc);

	if (sc->csb_gh) {
		contigfree(sc->csb_gh, 2*PAGE_SIZE, M_DEVBUF);
		sc->csb_gh = NULL;
		sc->csb_hg = NULL;
	}

	if (sc->queues) {
		for (i = 0; i < sc->num_rings; i++) {
			struct ptnet_queue *pq = sc->queues + i;

			if (mtx_initialized(&pq->lock)) {
				mtx_destroy(&pq->lock);
			}
			if (pq->bufring != NULL) {
				buf_ring_free(pq->bufring, M_DEVBUF);
			}
		}
		free(sc->queues, M_DEVBUF);
		sc->queues = NULL;
	}

	if (sc->iomem) {
		bus_release_resource(dev, SYS_RES_IOPORT,
				     PCIR_BAR(PTNETMAP_IO_PCI_BAR), sc->iomem);
		sc->iomem = NULL;
	}

	mtx_destroy(&sc->lock);

	device_printf(dev, "%s() completed\n", __func__);

	return (0);
}

static int
ptnet_suspend(device_t dev)
{
	struct ptnet_softc *sc = device_get_softc(dev);

	(void)sc;

	return (0);
}

static int
ptnet_resume(device_t dev)
{
	struct ptnet_softc *sc = device_get_softc(dev);

	(void)sc;

	return (0);
}

static int
ptnet_shutdown(device_t dev)
{
	struct ptnet_softc *sc = device_get_softc(dev);

	ptnet_device_shutdown(sc);

	return (0);
}

static int
ptnet_irqs_init(struct ptnet_softc *sc)
{
	int rid = PCIR_BAR(PTNETMAP_MSIX_PCI_BAR);
	int nvecs = sc->num_rings;
	device_t dev = sc->dev;
	int err = ENOSPC;
	int cpu_cur;
	int i;

	if (pci_find_cap(dev, PCIY_MSIX, NULL) != 0)  {
		device_printf(dev, "Could not find MSI-X capability\n");
		return (ENXIO);
	}

	sc->msix_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					      &rid, RF_ACTIVE);
	if (sc->msix_mem == NULL) {
		device_printf(dev, "Failed to allocate MSIX PCI BAR\n");
		return (ENXIO);
	}

	if (pci_msix_count(dev) < nvecs) {
		device_printf(dev, "Not enough MSI-X vectors\n");
		goto err_path;
	}

	err = pci_alloc_msix(dev, &nvecs);
	if (err) {
		device_printf(dev, "Failed to allocate MSI-X vectors\n");
		goto err_path;
	}

	for (i = 0; i < nvecs; i++) {
		struct ptnet_queue *pq = sc->queues + i;

		rid = i + 1;
		pq->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
						 RF_ACTIVE);
		if (pq->irq == NULL) {
			device_printf(dev, "Failed to allocate interrupt "
					   "for queue #%d\n", i);
			err = ENOSPC;
			goto err_path;
		}
	}

	cpu_cur = CPU_FIRST();
	for (i = 0; i < nvecs; i++) {
		struct ptnet_queue *pq = sc->queues + i;
		void (*handler)(void *) = ptnet_tx_intr;

		if (i >= sc->num_tx_rings) {
			handler = ptnet_rx_intr;
		}
		err = bus_setup_intr(dev, pq->irq, INTR_TYPE_NET | INTR_MPSAFE,
				     NULL /* intr_filter */, handler,
				     pq, &pq->cookie);
		if (err) {
			device_printf(dev, "Failed to register intr handler "
					   "for queue #%d\n", i);
			goto err_path;
		}

		bus_describe_intr(dev, pq->irq, pq->cookie, "q%d", i);
#if 0
		bus_bind_intr(sc->dev, pq->irq, cpu_cur);
#endif
		cpu_cur = CPU_NEXT(cpu_cur);
	}

	device_printf(dev, "Allocated %d MSI-X vectors\n", nvecs);

	cpu_cur = CPU_FIRST();
	for (i = 0; i < nvecs; i++) {
		struct ptnet_queue *pq = sc->queues + i;
		static void (*handler)(void *context, int pending);

		handler = (i < sc->num_tx_rings) ? ptnet_tx_task : ptnet_rx_task;

		TASK_INIT(&pq->task, 0, handler, pq);
		pq->taskq = taskqueue_create_fast("ptnet_queue", M_NOWAIT,
					taskqueue_thread_enqueue, &pq->taskq);
		taskqueue_start_threads(&pq->taskq, 1, PI_NET, "%s-pq-%d",
					device_get_nameunit(sc->dev), cpu_cur);
		cpu_cur = CPU_NEXT(cpu_cur);
	}

	return 0;
err_path:
	ptnet_irqs_fini(sc);
	return err;
}

static void
ptnet_irqs_fini(struct ptnet_softc *sc)
{
	device_t dev = sc->dev;
	int i;

	for (i = 0; i < sc->num_rings; i++) {
		struct ptnet_queue *pq = sc->queues + i;

		if (pq->taskq) {
			taskqueue_free(pq->taskq);
			pq->taskq = NULL;
		}

		if (pq->cookie) {
			bus_teardown_intr(dev, pq->irq, pq->cookie);
			pq->cookie = NULL;
		}

		if (pq->irq) {
			bus_release_resource(dev, SYS_RES_IRQ, i + 1, pq->irq);
			pq->irq = NULL;
		}
	}

	if (sc->msix_mem) {
		pci_release_msi(dev);

		bus_release_resource(dev, SYS_RES_MEMORY,
				     PCIR_BAR(PTNETMAP_MSIX_PCI_BAR),
				     sc->msix_mem);
		sc->msix_mem = NULL;
	}
}

static void
ptnet_init(void *opaque)
{
	struct ptnet_softc *sc = opaque;

	PTNET_CORE_LOCK(sc);
	ptnet_init_locked(sc);
	PTNET_CORE_UNLOCK(sc);
}

static int
ptnet_ioctl(if_t ifp, u_long cmd, caddr_t data)
{
	struct ptnet_softc *sc = if_getsoftc(ifp);
	device_t dev = sc->dev;
	struct ifreq *ifr = (struct ifreq *)data;
	int mask __unused, err = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		device_printf(dev, "SIOCSIFFLAGS %x\n", ifp->if_flags);
		PTNET_CORE_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			/* Network stack wants the iff to be up. */
			err = ptnet_init_locked(sc);
		} else {
			/* Network stack wants the iff to be down. */
			err = ptnet_stop(sc);
		}
		/* We don't need to do nothing to support IFF_PROMISC,
		 * since that is managed by the backend port. */
		PTNET_CORE_UNLOCK(sc);
		break;

	case SIOCSIFCAP:
		device_printf(dev, "SIOCSIFCAP %x %x\n",
			      ifr->ifr_reqcap, ifp->if_capenable);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
#ifdef DEVICE_POLLING
		if (mask & IFCAP_POLLING) {
			struct ptnet_queue *pq;
			int i;

			if (ifr->ifr_reqcap & IFCAP_POLLING) {
				err = ether_poll_register(ptnet_poll, ifp);
				if (err) {
					break;
				}
				/* Stop queues and sync with taskqueues. */
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				for (i = 0; i < sc->num_rings; i++) {
					pq = sc-> queues + i;
					/* Make sure the worker sees the
					 * IFF_DRV_RUNNING down. */
					PTNET_Q_LOCK(pq);
					pq->atok->appl_need_kick = 0;
					PTNET_Q_UNLOCK(pq);
					/* Wait for rescheduling to finish. */
					if (pq->taskq) {
						taskqueue_drain(pq->taskq,
								&pq->task);
					}
				}
				ifp->if_drv_flags |= IFF_DRV_RUNNING;
			} else {
				err = ether_poll_deregister(ifp);
				for (i = 0; i < sc->num_rings; i++) {
					pq = sc-> queues + i;
					PTNET_Q_LOCK(pq);
					pq->atok->appl_need_kick = 1;
					PTNET_Q_UNLOCK(pq);
				}
			}
		}
#endif  /* DEVICE_POLLING */
		ifp->if_capenable = ifr->ifr_reqcap;
		break;

	case SIOCSIFMTU:
		/* We support any reasonable MTU. */
		if (ifr->ifr_mtu < ETHERMIN ||
				ifr->ifr_mtu > PTNET_MAX_PKT_SIZE) {
			err = EINVAL;
		} else {
			PTNET_CORE_LOCK(sc);
			ifp->if_mtu = ifr->ifr_mtu;
			PTNET_CORE_UNLOCK(sc);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		err = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
		break;

	default:
		err = ether_ioctl(ifp, cmd, data);
		break;
	}

	return err;
}

static int
ptnet_init_locked(struct ptnet_softc *sc)
{
	if_t ifp = sc->ifp;
	struct netmap_adapter *na_dr = &sc->ptna->dr.up;
	struct netmap_adapter *na_nm = &sc->ptna->hwup.up;
	unsigned int nm_buf_size;
	int ret;

	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		return 0; /* nothing to do */
	}

	device_printf(sc->dev, "%s\n", __func__);

	/* Translate offload capabilities according to if_capenable. */
	ifp->if_hwassist = 0;
	if (ifp->if_capenable & IFCAP_TXCSUM)
		ifp->if_hwassist |= PTNET_CSUM_OFFLOAD;
	if (ifp->if_capenable & IFCAP_TXCSUM_IPV6)
		ifp->if_hwassist |= PTNET_CSUM_OFFLOAD_IPV6;
	if (ifp->if_capenable & IFCAP_TSO4)
		ifp->if_hwassist |= CSUM_IP_TSO;
	if (ifp->if_capenable & IFCAP_TSO6)
		ifp->if_hwassist |= CSUM_IP6_TSO;

	/*
	 * Prepare the interface for netmap mode access.
	 */
	netmap_update_config(na_dr);

	ret = netmap_mem_finalize(na_dr->nm_mem, na_dr);
	if (ret) {
		device_printf(sc->dev, "netmap_mem_finalize() failed\n");
		return ret;
	}

	if (sc->ptna->backend_users == 0) {
		ret = ptnet_nm_krings_create(na_nm);
		if (ret) {
			device_printf(sc->dev, "ptnet_nm_krings_create() "
					       "failed\n");
			goto err_mem_finalize;
		}

		ret = netmap_mem_rings_create(na_dr);
		if (ret) {
			device_printf(sc->dev, "netmap_mem_rings_create() "
					       "failed\n");
			goto err_rings_create;
		}

		ret = netmap_mem_get_lut(na_dr->nm_mem, &na_dr->na_lut);
		if (ret) {
			device_printf(sc->dev, "netmap_mem_get_lut() "
					       "failed\n");
			goto err_get_lut;
		}
	}

	ret = ptnet_nm_register(na_dr, 1 /* on */);
	if (ret) {
		goto err_register;
	}

	nm_buf_size = NETMAP_BUF_SIZE(na_dr);

	KASSERT(nm_buf_size > 0, ("Invalid netmap buffer size"));
	sc->min_tx_space = PTNET_MAX_PKT_SIZE / nm_buf_size + 2;
	device_printf(sc->dev, "%s: min_tx_space = %u\n", __func__,
		      sc->min_tx_space);
#ifdef PTNETMAP_STATS
	callout_reset(&sc->tick, hz, ptnet_tick, sc);
#endif

	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	return 0;

err_register:
	memset(&na_dr->na_lut, 0, sizeof(na_dr->na_lut));
err_get_lut:
	netmap_mem_rings_delete(na_dr);
err_rings_create:
	ptnet_nm_krings_delete(na_nm);
err_mem_finalize:
	netmap_mem_deref(na_dr->nm_mem, na_dr);

	return ret;
}

/* To be called under core lock. */
static int
ptnet_stop(struct ptnet_softc *sc)
{
	if_t ifp = sc->ifp;
	struct netmap_adapter *na_dr = &sc->ptna->dr.up;
	struct netmap_adapter *na_nm = &sc->ptna->hwup.up;
	int i;

	device_printf(sc->dev, "%s\n", __func__);

	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		return 0; /* nothing to do */
	}

	/* Clear the driver-ready flag, and synchronize with all the queues,
	 * so that after this loop we are sure nobody is working anymore with
	 * the device. This scheme is taken from the vtnet driver. */
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	callout_stop(&sc->tick);
	for (i = 0; i < sc->num_rings; i++) {
		PTNET_Q_LOCK(sc->queues + i);
		PTNET_Q_UNLOCK(sc->queues + i);
	}

	ptnet_nm_register(na_dr, 0 /* off */);

	if (sc->ptna->backend_users == 0) {
		netmap_mem_rings_delete(na_dr);
		ptnet_nm_krings_delete(na_nm);
	}
	netmap_mem_deref(na_dr->nm_mem, na_dr);

	return 0;
}

static void
ptnet_qflush(if_t ifp)
{
	struct ptnet_softc *sc = if_getsoftc(ifp);
	int i;

	/* Flush all the bufrings and do the interface flush. */
	for (i = 0; i < sc->num_rings; i++) {
		struct ptnet_queue *pq = sc->queues + i;
		struct mbuf *m;

		PTNET_Q_LOCK(pq);
		if (pq->bufring) {
			while ((m = buf_ring_dequeue_sc(pq->bufring))) {
				m_freem(m);
			}
		}
		PTNET_Q_UNLOCK(pq);
	}

	if_qflush(ifp);
}

static int
ptnet_media_change(if_t ifp)
{
	struct ptnet_softc *sc = if_getsoftc(ifp);
	struct ifmedia *ifm = &sc->media;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER) {
		return EINVAL;
	}

	return 0;
}

#if __FreeBSD_version >= 1100000
static uint64_t
ptnet_get_counter(if_t ifp, ift_counter cnt)
{
	struct ptnet_softc *sc = if_getsoftc(ifp);
	struct ptnet_queue_stats stats[2];
	int i;

	/* Accumulate statistics over the queues. */
	memset(stats, 0, sizeof(stats));
	for (i = 0; i < sc->num_rings; i++) {
		struct ptnet_queue *pq = sc->queues + i;
		int idx = (i < sc->num_tx_rings) ? 0 : 1;

		stats[idx].packets	+= pq->stats.packets;
		stats[idx].bytes	+= pq->stats.bytes;
		stats[idx].errors	+= pq->stats.errors;
		stats[idx].iqdrops	+= pq->stats.iqdrops;
		stats[idx].mcasts	+= pq->stats.mcasts;
	}

	switch (cnt) {
	case IFCOUNTER_IPACKETS:
		return (stats[1].packets);
	case IFCOUNTER_IQDROPS:
		return (stats[1].iqdrops);
	case IFCOUNTER_IERRORS:
		return (stats[1].errors);
	case IFCOUNTER_OPACKETS:
		return (stats[0].packets);
	case IFCOUNTER_OBYTES:
		return (stats[0].bytes);
	case IFCOUNTER_OMCASTS:
		return (stats[0].mcasts);
	default:
		return (if_get_counter_default(ifp, cnt));
	}
}
#endif


#ifdef PTNETMAP_STATS
/* Called under core lock. */
static void
ptnet_tick(void *opaque)
{
	struct ptnet_softc *sc = opaque;
	int i;

	for (i = 0; i < sc->num_rings; i++) {
		struct ptnet_queue *pq = sc->queues + i;
		struct ptnet_queue_stats cur = pq->stats;
		struct timeval now;
		unsigned int delta;

		microtime(&now);
		delta = now.tv_usec - sc->last_ts.tv_usec +
			(now.tv_sec - sc->last_ts.tv_sec) * 1000000;
		delta /= 1000; /* in milliseconds */

		if (delta == 0)
			continue;

		device_printf(sc->dev, "#%d[%u ms]:pkts %lu, kicks %lu, "
			      "intr %lu\n", i, delta,
			      (cur.packets - pq->last_stats.packets),
			      (cur.kicks - pq->last_stats.kicks),
			      (cur.intrs - pq->last_stats.intrs));
		pq->last_stats = cur;
	}
	microtime(&sc->last_ts);
	callout_schedule(&sc->tick, hz);
}
#endif /* PTNETMAP_STATS */

static void
ptnet_media_status(if_t ifp, struct ifmediareq *ifmr)
{
	/* We are always active, as the backend netmap port is
	 * always open in netmap mode. */
	ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE;
	ifmr->ifm_active = IFM_ETHER | IFM_10G_T | IFM_FDX;
}

static uint32_t
ptnet_nm_ptctl(struct ptnet_softc *sc, uint32_t cmd)
{
	/*
	 * Write a command and read back error status,
	 * with zero meaning success.
	 */
	bus_write_4(sc->iomem, PTNET_IO_PTCTL, cmd);
	return bus_read_4(sc->iomem, PTNET_IO_PTCTL);
}

static int
ptnet_nm_config(struct netmap_adapter *na, struct nm_config_info *info)
{
	struct ptnet_softc *sc = if_getsoftc(na->ifp);

	info->num_tx_rings = bus_read_4(sc->iomem, PTNET_IO_NUM_TX_RINGS);
	info->num_rx_rings = bus_read_4(sc->iomem, PTNET_IO_NUM_RX_RINGS);
	info->num_tx_descs = bus_read_4(sc->iomem, PTNET_IO_NUM_TX_SLOTS);
	info->num_rx_descs = bus_read_4(sc->iomem, PTNET_IO_NUM_RX_SLOTS);
	info->rx_buf_maxsize = NETMAP_BUF_SIZE(na);

	device_printf(sc->dev, "txr %u, rxr %u, txd %u, rxd %u, rxbufsz %u\n",
			info->num_tx_rings, info->num_rx_rings,
			info->num_tx_descs, info->num_rx_descs,
			info->rx_buf_maxsize);

	return 0;
}

static void
ptnet_sync_from_csb(struct ptnet_softc *sc, struct netmap_adapter *na)
{
	int i;

	/* Sync krings from the host, reading from
	 * CSB. */
	for (i = 0; i < sc->num_rings; i++) {
		struct nm_csb_atok *atok = sc->queues[i].atok;
		struct nm_csb_ktoa *ktoa = sc->queues[i].ktoa;
		struct netmap_kring *kring;

		if (i < na->num_tx_rings) {
			kring = na->tx_rings[i];
		} else {
			kring = na->rx_rings[i - na->num_tx_rings];
		}
		kring->rhead = kring->ring->head = atok->head;
		kring->rcur = kring->ring->cur = atok->cur;
		kring->nr_hwcur = ktoa->hwcur;
		kring->nr_hwtail = kring->rtail =
			kring->ring->tail = ktoa->hwtail;

		nm_prdis("%d,%d: csb {hc %u h %u c %u ht %u}", t, i,
		   ktoa->hwcur, atok->head, atok->cur,
		   ktoa->hwtail);
		nm_prdis("%d,%d: kring {hc %u rh %u rc %u h %u c %u ht %u rt %u t %u}",
		   t, i, kring->nr_hwcur, kring->rhead, kring->rcur,
		   kring->ring->head, kring->ring->cur, kring->nr_hwtail,
		   kring->rtail, kring->ring->tail);
	}
}

static void
ptnet_update_vnet_hdr(struct ptnet_softc *sc)
{
	unsigned int wanted_hdr_len = ptnet_vnet_hdr ? PTNET_HDR_SIZE : 0;

	bus_write_4(sc->iomem, PTNET_IO_VNET_HDR_LEN, wanted_hdr_len);
	sc->vnet_hdr_len = bus_read_4(sc->iomem, PTNET_IO_VNET_HDR_LEN);
	sc->ptna->hwup.up.virt_hdr_len = sc->vnet_hdr_len;
}

static int
ptnet_nm_register(struct netmap_adapter *na, int onoff)
{
	/* device-specific */
	if_t ifp = na->ifp;
	struct ptnet_softc *sc = if_getsoftc(ifp);
	int native = (na == &sc->ptna->hwup.up);
	struct ptnet_queue *pq;
	int ret = 0;
	int i;

	if (!onoff) {
		sc->ptna->backend_users--;
	}

	/* If this is the last netmap client, guest interrupt enable flags may
	 * be in arbitrary state. Since these flags are going to be used also
	 * by the netdevice driver, we have to make sure to start with
	 * notifications enabled. Also, schedule NAPI to flush pending packets
	 * in the RX rings, since we will not receive further interrupts
	 * until these will be processed. */
	if (native && !onoff && na->active_fds == 0) {
		nm_prinf("Exit netmap mode, re-enable interrupts");
		for (i = 0; i < sc->num_rings; i++) {
			pq = sc->queues + i;
			pq->atok->appl_need_kick = 1;
		}
	}

	if (onoff) {
		if (sc->ptna->backend_users == 0) {
			/* Initialize notification enable fields in the CSB. */
			for (i = 0; i < sc->num_rings; i++) {
				pq = sc->queues + i;
				pq->ktoa->kern_need_kick = 1;
				pq->atok->appl_need_kick =
					(!(ifp->if_capenable & IFCAP_POLLING)
						&& i >= sc->num_tx_rings);
			}

			/* Set the virtio-net header length. */
			ptnet_update_vnet_hdr(sc);

			/* Make sure the host adapter passed through is ready
			 * for txsync/rxsync. */
			ret = ptnet_nm_ptctl(sc, PTNETMAP_PTCTL_CREATE);
			if (ret) {
				return ret;
			}

			/* Align the guest krings and rings to the state stored
			 * in the CSB. */
			ptnet_sync_from_csb(sc, na);
		}

		/* If not native, don't call nm_set_native_flags, since we don't want
		 * to replace if_transmit method, nor set NAF_NETMAP_ON */
		if (native) {
			netmap_krings_mode_commit(na, onoff);
			nm_set_native_flags(na);
		}

	} else {
		if (native) {
			nm_clear_native_flags(na);
			netmap_krings_mode_commit(na, onoff);
		}

		if (sc->ptna->backend_users == 0) {
			ret = ptnet_nm_ptctl(sc, PTNETMAP_PTCTL_DELETE);
		}
	}

	if (onoff) {
		sc->ptna->backend_users++;
	}

	return ret;
}

static int
ptnet_nm_txsync(struct netmap_kring *kring, int flags)
{
	struct ptnet_softc *sc = if_getsoftc(kring->na->ifp);
	struct ptnet_queue *pq = sc->queues + kring->ring_id;
	bool notify;

	notify = netmap_pt_guest_txsync(pq->atok, pq->ktoa, kring, flags);
	if (notify) {
		ptnet_kick(pq);
	}

	return 0;
}

static int
ptnet_nm_rxsync(struct netmap_kring *kring, int flags)
{
	struct ptnet_softc *sc = if_getsoftc(kring->na->ifp);
	struct ptnet_queue *pq = sc->rxqueues + kring->ring_id;
	bool notify;

	notify = netmap_pt_guest_rxsync(pq->atok, pq->ktoa, kring, flags);
	if (notify) {
		ptnet_kick(pq);
	}

	return 0;
}

static void
ptnet_nm_intr(struct netmap_adapter *na, int onoff)
{
	struct ptnet_softc *sc = if_getsoftc(na->ifp);
	int i;

	for (i = 0; i < sc->num_rings; i++) {
		struct ptnet_queue *pq = sc->queues + i;
		pq->atok->appl_need_kick = onoff;
	}
}

static void
ptnet_tx_intr(void *opaque)
{
	struct ptnet_queue *pq = opaque;
	struct ptnet_softc *sc = pq->sc;

	DBG(device_printf(sc->dev, "Tx interrupt #%d\n", pq->kring_id));
#ifdef PTNETMAP_STATS
	pq->stats.intrs ++;
#endif /* PTNETMAP_STATS */

	if (netmap_tx_irq(sc->ifp, pq->kring_id) != NM_IRQ_PASS) {
		return;
	}

	/* Schedule the tasqueue to flush process transmissions requests.
	 * However, vtnet, if_em and if_igb just call ptnet_transmit() here,
	 * at least when using MSI-X interrupts. The if_em driver, instead
	 * schedule taskqueue when using legacy interrupts. */
	taskqueue_enqueue(pq->taskq, &pq->task);
}

static void
ptnet_rx_intr(void *opaque)
{
	struct ptnet_queue *pq = opaque;
	struct ptnet_softc *sc = pq->sc;
	unsigned int unused;

	DBG(device_printf(sc->dev, "Rx interrupt #%d\n", pq->kring_id));
#ifdef PTNETMAP_STATS
	pq->stats.intrs ++;
#endif /* PTNETMAP_STATS */

	if (netmap_rx_irq(sc->ifp, pq->kring_id, &unused) != NM_IRQ_PASS) {
		return;
	}

	/* Like vtnet, if_igb and if_em drivers when using MSI-X interrupts,
	 * receive-side processing is executed directly in the interrupt
	 * service routine. Alternatively, we may schedule the taskqueue. */
	ptnet_rx_eof(pq, PTNET_RX_BUDGET, true);
}

/* The following offloadings-related functions are taken from the vtnet
 * driver, but the same functionality is required for the ptnet driver.
 * As a temporary solution, I copied this code from vtnet and I started
 * to generalize it (taking away driver-specific statistic accounting),
 * making as little modifications as possible.
 * In the future we need to share these functions between vtnet and ptnet.
 */
static int
ptnet_tx_offload_ctx(struct mbuf *m, int *etype, int *proto, int *start)
{
	struct ether_vlan_header *evh;
	int offset;

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
		/* Here we should increment the tx_csum_bad_ethtype counter. */
		return (EINVAL);
	}

	return (0);
}

static int
ptnet_tx_offload_tso(if_t ifp, struct mbuf *m, int eth_type,
		     int offset, bool allow_ecn, struct virtio_net_hdr *hdr)
{
	static struct timeval lastecn;
	static int curecn;
	struct tcphdr *tcp, tcphdr;

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
		if (!allow_ecn) {
			if (ppsratecheck(&lastecn, &curecn, 1))
				if_printf(ifp,
				    "TSO with ECN not negotiated with host\n");
			return (ENOTSUP);
		}
		hdr->gso_type |= VIRTIO_NET_HDR_GSO_ECN;
	}

	/* Here we should increment tx_tso counter. */

	return (0);
}

static struct mbuf *
ptnet_tx_offload(if_t ifp, struct mbuf *m, bool allow_ecn,
		 struct virtio_net_hdr *hdr)
{
	int flags, etype, csum_start, proto, error;

	flags = m->m_pkthdr.csum_flags;

	error = ptnet_tx_offload_ctx(m, &etype, &proto, &csum_start);
	if (error)
		goto drop;

	if ((etype == ETHERTYPE_IP && flags & PTNET_CSUM_OFFLOAD) ||
	    (etype == ETHERTYPE_IPV6 && flags & PTNET_CSUM_OFFLOAD_IPV6)) {
		/*
		 * We could compare the IP protocol vs the CSUM_ flag too,
		 * but that really should not be necessary.
		 */
		hdr->flags |= VIRTIO_NET_HDR_F_NEEDS_CSUM;
		hdr->csum_start = csum_start;
		hdr->csum_offset = m->m_pkthdr.csum_data;
		/* Here we should increment the tx_csum counter. */
	}

	if (flags & CSUM_TSO) {
		if (__predict_false(proto != IPPROTO_TCP)) {
			/* Likely failed to correctly parse the mbuf.
			 * Here we should increment the tx_tso_not_tcp
			 * counter. */
			goto drop;
		}

		KASSERT(hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM,
		    ("%s: mbuf %p TSO without checksum offload %#x",
		    __func__, m, flags));

		error = ptnet_tx_offload_tso(ifp, m, etype, csum_start,
					     allow_ecn, hdr);
		if (error)
			goto drop;
	}

	return (m);

drop:
	m_freem(m);
	return (NULL);
}

static void
ptnet_vlan_tag_remove(struct mbuf *m)
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

/*
 * Use the checksum offset in the VirtIO header to set the
 * correct CSUM_* flags.
 */
static int
ptnet_rx_csum_by_offset(struct mbuf *m, uint16_t eth_type, int ip_start,
			struct virtio_net_hdr *hdr)
{
#if defined(INET) || defined(INET6)
	int offset = hdr->csum_start + hdr->csum_offset;
#endif

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
		/* Here we should increment the rx_csum_bad_ethtype counter. */
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
		/* Here we should increment the rx_csum_bad_offset counter. */
		return (1);
	}

	return (0);
}

static int
ptnet_rx_csum_by_parse(struct mbuf *m, uint16_t eth_type, int ip_start,
		       struct virtio_net_hdr *hdr)
{
	int offset, proto;

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
		/* Here we should increment the rx_csum_bad_ethtype counter. */
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
		if_printf(ifp, "cksum offload of unsupported "
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
ptnet_rx_csum(struct mbuf *m, struct virtio_net_hdr *hdr)
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
		error = ptnet_rx_csum_by_offset(m, eth_type, offset, hdr);
	else
		error = ptnet_rx_csum_by_parse(m, eth_type, offset, hdr);

	return (error);
}
/* End of offloading-related functions to be shared with vtnet. */

static void
ptnet_ring_update(struct ptnet_queue *pq, struct netmap_kring *kring,
		  unsigned int head, unsigned int sync_flags)
{
	struct netmap_ring *ring = kring->ring;
	struct nm_csb_atok *atok = pq->atok;
	struct nm_csb_ktoa *ktoa = pq->ktoa;

	/* Some packets have been pushed to the netmap ring. We have
	 * to tell the host to process the new packets, updating cur
	 * and head in the CSB. */
	ring->head = ring->cur = head;

	/* Mimic nm_txsync_prologue/nm_rxsync_prologue. */
	kring->rcur = kring->rhead = head;

	nm_sync_kloop_appl_write(atok, kring->rcur, kring->rhead);

	/* Kick the host if needed. */
	if (NM_ACCESS_ONCE(ktoa->kern_need_kick)) {
		atok->sync_flags = sync_flags;
		ptnet_kick(pq);
	}
}

#define PTNET_TX_NOSPACE(_h, _k, _min)	\
	((((_h) < (_k)->rtail) ? 0 : (_k)->nkr_num_slots) + \
		(_k)->rtail - (_h)) < (_min)

/* This function may be called by the network stack, or by
 * by the taskqueue thread. */
static int
ptnet_drain_transmit_queue(struct ptnet_queue *pq, unsigned int budget,
			   bool may_resched)
{
	struct ptnet_softc *sc = pq->sc;
	bool have_vnet_hdr = sc->vnet_hdr_len;
	struct netmap_adapter *na = &sc->ptna->dr.up;
	if_t ifp = sc->ifp;
	unsigned int batch_count = 0;
	struct nm_csb_atok *atok;
	struct nm_csb_ktoa *ktoa;
	struct netmap_kring *kring;
	struct netmap_ring *ring;
	struct netmap_slot *slot;
	unsigned int count = 0;
	unsigned int minspace;
	unsigned int head;
	unsigned int lim;
	struct mbuf *mhead;
	struct mbuf *mf;
	int nmbuf_bytes;
	uint8_t *nmbuf;

	if (!PTNET_Q_TRYLOCK(pq)) {
		/* We failed to acquire the lock, schedule the taskqueue. */
		nm_prlim(1, "Deferring TX work");
		if (may_resched) {
			taskqueue_enqueue(pq->taskq, &pq->task);
		}

		return 0;
	}

	if (unlikely(!(ifp->if_drv_flags & IFF_DRV_RUNNING))) {
		PTNET_Q_UNLOCK(pq);
		nm_prlim(1, "Interface is down");
		return ENETDOWN;
	}

	atok = pq->atok;
	ktoa = pq->ktoa;
	kring = na->tx_rings[pq->kring_id];
	ring = kring->ring;
	lim = kring->nkr_num_slots - 1;
	head = ring->head;
	minspace = sc->min_tx_space;

	while (count < budget) {
		if (PTNET_TX_NOSPACE(head, kring, minspace)) {
			/* We ran out of slot, let's see if the host has
			 * freed up some, by reading hwcur and hwtail from
			 * the CSB. */
			ptnet_sync_tail(ktoa, kring);

			if (PTNET_TX_NOSPACE(head, kring, minspace)) {
				/* Still no slots available. Reactivate the
				 * interrupts so that we can be notified
				 * when some free slots are made available by
				 * the host. */
				atok->appl_need_kick = 1;

				/* Double check. We need a full barrier to
				 * prevent the store to atok->appl_need_kick
				 * to be reordered with the load from
				 * ktoa->hwcur and ktoa->hwtail (store-load
				 * barrier). */
				nm_stld_barrier();
				ptnet_sync_tail(ktoa, kring);
				if (likely(PTNET_TX_NOSPACE(head, kring,
							    minspace))) {
					break;
				}

				nm_prlim(1, "Found more slots by doublecheck");
				/* More slots were freed before reactivating
				 * the interrupts. */
				atok->appl_need_kick = 0;
			}
		}

		mhead = drbr_peek(ifp, pq->bufring);
		if (!mhead) {
			break;
		}

		/* Initialize transmission state variables. */
		slot = ring->slot + head;
		nmbuf = NMB(na, slot);
		nmbuf_bytes = 0;

		/* If needed, prepare the virtio-net header at the beginning
		 * of the first slot. */
		if (have_vnet_hdr) {
			struct virtio_net_hdr *vh =
					(struct virtio_net_hdr *)nmbuf;

			/* For performance, we could replace this memset() with
			 * two 8-bytes-wide writes. */
			memset(nmbuf, 0, PTNET_HDR_SIZE);
			if (mhead->m_pkthdr.csum_flags & PTNET_ALL_OFFLOAD) {
				mhead = ptnet_tx_offload(ifp, mhead, false,
							 vh);
				if (unlikely(!mhead)) {
					/* Packet dropped because errors
					 * occurred while preparing the vnet
					 * header. Let's go ahead with the next
					 * packet. */
					pq->stats.errors ++;
					drbr_advance(ifp, pq->bufring);
					continue;
				}
			}
			nm_prdis(1, "%s: [csum_flags %lX] vnet hdr: flags %x "
			      "csum_start %u csum_ofs %u hdr_len = %u "
			      "gso_size %u gso_type %x", __func__,
			      mhead->m_pkthdr.csum_flags, vh->flags,
			      vh->csum_start, vh->csum_offset, vh->hdr_len,
			      vh->gso_size, vh->gso_type);

			nmbuf += PTNET_HDR_SIZE;
			nmbuf_bytes += PTNET_HDR_SIZE;
		}

		for (mf = mhead; mf; mf = mf->m_next) {
			uint8_t *mdata = mf->m_data;
			int mlen = mf->m_len;

			for (;;) {
				int copy = NETMAP_BUF_SIZE(na) - nmbuf_bytes;

				if (mlen < copy) {
					copy = mlen;
				}
				memcpy(nmbuf, mdata, copy);

				mdata += copy;
				mlen -= copy;
				nmbuf += copy;
				nmbuf_bytes += copy;

				if (!mlen) {
					break;
				}

				slot->len = nmbuf_bytes;
				slot->flags = NS_MOREFRAG;

				head = nm_next(head, lim);
				KASSERT(head != ring->tail,
					("Unexpectedly run out of TX space"));
				slot = ring->slot + head;
				nmbuf = NMB(na, slot);
				nmbuf_bytes = 0;
			}
		}

		/* Complete last slot and update head. */
		slot->len = nmbuf_bytes;
		slot->flags = 0;
		head = nm_next(head, lim);

		/* Consume the packet just processed. */
		drbr_advance(ifp, pq->bufring);

		/* Copy the packet to listeners. */
		ETHER_BPF_MTAP(ifp, mhead);

		pq->stats.packets ++;
		pq->stats.bytes += mhead->m_pkthdr.len;
		if (mhead->m_flags & M_MCAST) {
			pq->stats.mcasts ++;
		}

		m_freem(mhead);

		count ++;
		if (++batch_count == PTNET_TX_BATCH) {
			ptnet_ring_update(pq, kring, head, NAF_FORCE_RECLAIM);
			batch_count = 0;
		}
	}

	if (batch_count) {
		ptnet_ring_update(pq, kring, head, NAF_FORCE_RECLAIM);
	}

	if (count >= budget && may_resched) {
		DBG(nm_prlim(1, "out of budget: resched, %d mbufs pending\n",
					drbr_inuse(ifp, pq->bufring)));
		taskqueue_enqueue(pq->taskq, &pq->task);
	}

	PTNET_Q_UNLOCK(pq);

	return count;
}

static int
ptnet_transmit(if_t ifp, struct mbuf *m)
{
	struct ptnet_softc *sc = if_getsoftc(ifp);
	struct ptnet_queue *pq;
	unsigned int queue_idx;
	int err;

	DBG(device_printf(sc->dev, "transmit %p\n", m));

	/* Insert 802.1Q header if needed. */
	if (m->m_flags & M_VLANTAG) {
		m = ether_vlanencap(m, m->m_pkthdr.ether_vtag);
		if (m == NULL) {
			return ENOBUFS;
		}
		m->m_flags &= ~M_VLANTAG;
	}

	/* Get the flow-id if available. */
	queue_idx = (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE) ?
		    m->m_pkthdr.flowid : curcpu;

	if (unlikely(queue_idx >= sc->num_tx_rings)) {
		queue_idx %= sc->num_tx_rings;
	}

	pq = sc->queues + queue_idx;

	err = drbr_enqueue(ifp, pq->bufring, m);
	if (err) {
		/* ENOBUFS when the bufring is full */
		nm_prlim(1, "%s: drbr_enqueue() failed %d\n",
			__func__, err);
		pq->stats.errors ++;
		return err;
	}

	if (ifp->if_capenable & IFCAP_POLLING) {
		/* If polling is on, the transmit queues will be
		 * drained by the poller. */
		return 0;
	}

	err = ptnet_drain_transmit_queue(pq, PTNET_TX_BUDGET, true);

	return (err < 0) ? err : 0;
}

static unsigned int
ptnet_rx_discard(struct netmap_kring *kring, unsigned int head)
{
	struct netmap_ring *ring = kring->ring;
	struct netmap_slot *slot = ring->slot + head;

	for (;;) {
		head = nm_next(head, kring->nkr_num_slots - 1);
		if (!(slot->flags & NS_MOREFRAG) || head == ring->tail) {
			break;
		}
		slot = ring->slot + head;
	}

	return head;
}

static inline struct mbuf *
ptnet_rx_slot(struct mbuf *mtail, uint8_t *nmbuf, unsigned int nmbuf_len)
{
	uint8_t *mdata = mtod(mtail, uint8_t *) + mtail->m_len;

	do {
		unsigned int copy;

		if (mtail->m_len == MCLBYTES) {
			struct mbuf *mf;

			mf = m_getcl(M_NOWAIT, MT_DATA, 0);
			if (unlikely(!mf)) {
				return NULL;
			}

			mtail->m_next = mf;
			mtail = mf;
			mdata = mtod(mtail, uint8_t *);
			mtail->m_len = 0;
		}

		copy = MCLBYTES - mtail->m_len;
		if (nmbuf_len < copy) {
			copy = nmbuf_len;
		}

		memcpy(mdata, nmbuf, copy);

		nmbuf += copy;
		nmbuf_len -= copy;
		mdata += copy;
		mtail->m_len += copy;
	} while (nmbuf_len);

	return mtail;
}

static int
ptnet_rx_eof(struct ptnet_queue *pq, unsigned int budget, bool may_resched)
{
	struct ptnet_softc *sc = pq->sc;
	bool have_vnet_hdr = sc->vnet_hdr_len;
	struct nm_csb_atok *atok = pq->atok;
	struct nm_csb_ktoa *ktoa = pq->ktoa;
	struct netmap_adapter *na = &sc->ptna->dr.up;
	struct netmap_kring *kring = na->rx_rings[pq->kring_id];
	struct netmap_ring *ring = kring->ring;
	unsigned int const lim = kring->nkr_num_slots - 1;
	unsigned int batch_count = 0;
	if_t ifp = sc->ifp;
	unsigned int count = 0;
	uint32_t head;

	PTNET_Q_LOCK(pq);

	if (unlikely(!(ifp->if_drv_flags & IFF_DRV_RUNNING))) {
		goto unlock;
	}

	kring->nr_kflags &= ~NKR_PENDINTR;

	head = ring->head;
	while (count < budget) {
		uint32_t prev_head = head;
		struct mbuf *mhead, *mtail;
		struct virtio_net_hdr *vh;
		struct netmap_slot *slot;
		unsigned int nmbuf_len;
		uint8_t *nmbuf;
		int deliver = 1; /* the mbuf to the network stack. */
host_sync:
		if (head == ring->tail) {
			/* We ran out of slot, let's see if the host has
			 * added some, by reading hwcur and hwtail from
			 * the CSB. */
			ptnet_sync_tail(ktoa, kring);

			if (head == ring->tail) {
				/* Still no slots available. Reactivate
				 * interrupts as they were disabled by the
				 * host thread right before issuing the
				 * last interrupt. */
				atok->appl_need_kick = 1;

				/* Double check for more completed RX slots.
				 * We need a full barrier to prevent the store
				 * to atok->appl_need_kick to be reordered with
				 * the load from ktoa->hwcur and ktoa->hwtail
				 * (store-load barrier). */
				nm_stld_barrier();
				ptnet_sync_tail(ktoa, kring);
				if (likely(head == ring->tail)) {
					break;
				}
				atok->appl_need_kick = 0;
			}
		}

		/* Initialize ring state variables, possibly grabbing the
		 * virtio-net header. */
		slot = ring->slot + head;
		nmbuf = NMB(na, slot);
		nmbuf_len = slot->len;

		vh = (struct virtio_net_hdr *)nmbuf;
		if (have_vnet_hdr) {
			if (unlikely(nmbuf_len < PTNET_HDR_SIZE)) {
				/* There is no good reason why host should
				 * put the header in multiple netmap slots.
				 * If this is the case, discard. */
				nm_prlim(1, "Fragmented vnet-hdr: dropping");
				head = ptnet_rx_discard(kring, head);
				pq->stats.iqdrops ++;
				deliver = 0;
				goto skip;
			}
			nm_prdis(1, "%s: vnet hdr: flags %x csum_start %u "
			      "csum_ofs %u hdr_len = %u gso_size %u "
			      "gso_type %x", __func__, vh->flags,
			      vh->csum_start, vh->csum_offset, vh->hdr_len,
			      vh->gso_size, vh->gso_type);
			nmbuf += PTNET_HDR_SIZE;
			nmbuf_len -= PTNET_HDR_SIZE;
		}

		/* Allocate the head of a new mbuf chain.
		 * We use m_getcl() to allocate an mbuf with standard cluster
		 * size (MCLBYTES). In the future we could use m_getjcl()
		 * to choose different sizes. */
		mhead = mtail = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (unlikely(mhead == NULL)) {
			device_printf(sc->dev, "%s: failed to allocate mbuf "
				      "head\n", __func__);
			pq->stats.errors ++;
			break;
		}

		/* Initialize the mbuf state variables. */
		mhead->m_pkthdr.len = nmbuf_len;
		mtail->m_len = 0;

		/* Scan all the netmap slots containing the current packet. */
		for (;;) {
			DBG(device_printf(sc->dev, "%s: h %u t %u rcv frag "
					  "len %u, flags %u\n", __func__,
					  head, ring->tail, slot->len,
					  slot->flags));

			mtail = ptnet_rx_slot(mtail, nmbuf, nmbuf_len);
			if (unlikely(!mtail)) {
				/* Ouch. We ran out of memory while processing
				 * a packet. We have to restore the previous
				 * head position, free the mbuf chain, and
				 * schedule the taskqueue to give the packet
				 * another chance. */
				device_printf(sc->dev, "%s: failed to allocate"
					" mbuf frag, reset head %u --> %u\n",
					__func__, head, prev_head);
				head = prev_head;
				m_freem(mhead);
				pq->stats.errors ++;
				if (may_resched) {
					taskqueue_enqueue(pq->taskq,
							  &pq->task);
				}
				goto escape;
			}

			/* We have to increment head irrespective of the
			 * NS_MOREFRAG being set or not. */
			head = nm_next(head, lim);

			if (!(slot->flags & NS_MOREFRAG)) {
				break;
			}

			if (unlikely(head == ring->tail)) {
				/* The very last slot prepared by the host has
				 * the NS_MOREFRAG set. Drop it and continue
				 * the outer cycle (to do the double-check). */
				nm_prlim(1, "Incomplete packet: dropping");
				m_freem(mhead);
				pq->stats.iqdrops ++;
				goto host_sync;
			}

			slot = ring->slot + head;
			nmbuf = NMB(na, slot);
			nmbuf_len = slot->len;
			mhead->m_pkthdr.len += nmbuf_len;
		}

		mhead->m_pkthdr.rcvif = ifp;
		mhead->m_pkthdr.csum_flags = 0;

		/* Store the queue idx in the packet header. */
		mhead->m_pkthdr.flowid = pq->kring_id;
		M_HASHTYPE_SET(mhead, M_HASHTYPE_OPAQUE);

		if (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) {
			struct ether_header *eh;

			eh = mtod(mhead, struct ether_header *);
			if (eh->ether_type == htons(ETHERTYPE_VLAN)) {
				ptnet_vlan_tag_remove(mhead);
				/*
				 * With the 802.1Q header removed, update the
				 * checksum starting location accordingly.
				 */
				if (vh->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM)
					vh->csum_start -= ETHER_VLAN_ENCAP_LEN;
			}
		}

		if (have_vnet_hdr && (vh->flags & (VIRTIO_NET_HDR_F_NEEDS_CSUM
					| VIRTIO_NET_HDR_F_DATA_VALID))) {
			if (unlikely(ptnet_rx_csum(mhead, vh))) {
				m_freem(mhead);
				nm_prlim(1, "Csum offload error: dropping");
				pq->stats.iqdrops ++;
				deliver = 0;
			}
		}

skip:
		count ++;
		if (++batch_count >= PTNET_RX_BATCH) {
			/* Some packets have been (or will be) pushed to the network
			 * stack. We need to update the CSB to tell the host about
			 * the new ring->cur and ring->head (RX buffer refill). */
			ptnet_ring_update(pq, kring, head, NAF_FORCE_READ);
			batch_count = 0;
		}

		if (likely(deliver))  {
			pq->stats.packets ++;
			pq->stats.bytes += mhead->m_pkthdr.len;

			PTNET_Q_UNLOCK(pq);
			(*ifp->if_input)(ifp, mhead);
			PTNET_Q_LOCK(pq);
			/* The ring->head index (and related indices) are
			 * updated under pq lock by ptnet_ring_update().
			 * Since we dropped the lock to call if_input(), we
			 * must reload ring->head and restart processing the
			 * ring from there. */
			head = ring->head;

			if (unlikely(!(ifp->if_drv_flags & IFF_DRV_RUNNING))) {
				/* The interface has gone down while we didn't
				 * have the lock. Stop any processing and exit. */
				goto unlock;
			}
		}
	}
escape:
	if (batch_count) {
		ptnet_ring_update(pq, kring, head, NAF_FORCE_READ);

	}

	if (count >= budget && may_resched) {
		/* If we ran out of budget or the double-check found new
		 * slots to process, schedule the taskqueue. */
		DBG(nm_prlim(1, "out of budget: resched h %u t %u\n",
					head, ring->tail));
		taskqueue_enqueue(pq->taskq, &pq->task);
	}
unlock:
	PTNET_Q_UNLOCK(pq);

	return count;
}

static void
ptnet_rx_task(void *context, int pending)
{
	struct ptnet_queue *pq = context;

	DBG(nm_prlim(1, "%s: pq #%u\n", __func__, pq->kring_id));
	ptnet_rx_eof(pq, PTNET_RX_BUDGET, true);
}

static void
ptnet_tx_task(void *context, int pending)
{
	struct ptnet_queue *pq = context;

	DBG(nm_prlim(1, "%s: pq #%u\n", __func__, pq->kring_id));
	ptnet_drain_transmit_queue(pq, PTNET_TX_BUDGET, true);
}

#ifdef DEVICE_POLLING
/* We don't need to handle differently POLL_AND_CHECK_STATUS and
 * POLL_ONLY, since we don't have an Interrupt Status Register. */
static int
ptnet_poll(if_t ifp, enum poll_cmd cmd, int budget)
{
	struct ptnet_softc *sc = if_getsoftc(ifp);
	unsigned int queue_budget;
	unsigned int count = 0;
	bool borrow = false;
	int i;

	KASSERT(sc->num_rings > 0, ("Found no queues in while polling ptnet"));
	queue_budget = MAX(budget / sc->num_rings, 1);
	nm_prlim(1, "Per-queue budget is %d", queue_budget);

	while (budget) {
		unsigned int rcnt = 0;

		for (i = 0; i < sc->num_rings; i++) {
			struct ptnet_queue *pq = sc->queues + i;

			if (borrow) {
				queue_budget = MIN(queue_budget, budget);
				if (queue_budget == 0) {
					break;
				}
			}

			if (i < sc->num_tx_rings) {
				rcnt += ptnet_drain_transmit_queue(pq,
						   queue_budget, false);
			} else {
				rcnt += ptnet_rx_eof(pq, queue_budget,
						      false);
			}
		}

		if (!rcnt) {
			/* A scan of the queues gave no result, we can
			 * stop here. */
			break;
		}

		if (rcnt > budget) {
			/* This may happen when initial budget < sc->num_rings,
			 * since one packet budget is given to each queue
			 * anyway. Just pretend we didn't eat "so much". */
			rcnt = budget;
		}
		count += rcnt;
		budget -= rcnt;
		borrow = true;
	}


	return count;
}
#endif /* DEVICE_POLLING */

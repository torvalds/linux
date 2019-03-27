/*-
 * Copyright (c) 2013 Tsubai Masanari
 * Copyright (c) 2013 Bryan Venteicher <bryanv@FreeBSD.org>
 * Copyright (c) 2018 Patrick Kelsey
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
 *
 * $OpenBSD: src/sys/dev/pci/if_vmx.c,v 1.11 2013/06/22 00:28:10 uebayasi Exp $
 */

/* Driver for VMware vmxnet3 virtual ethernet devices. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/smp.h>
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
#include <net/iflib.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "ifdi_if.h"

#include "if_vmxreg.h"
#include "if_vmxvar.h"

#include "opt_inet.h"
#include "opt_inet6.h"


#define VMXNET3_VMWARE_VENDOR_ID	0x15AD
#define VMXNET3_VMWARE_DEVICE_ID	0x07B0

static pci_vendor_info_t vmxnet3_vendor_info_array[] =
{
	PVID(VMXNET3_VMWARE_VENDOR_ID, VMXNET3_VMWARE_DEVICE_ID, "VMware VMXNET3 Ethernet Adapter"),
	/* required last entry */
	PVID_END
};

static void	*vmxnet3_register(device_t);
static int	vmxnet3_attach_pre(if_ctx_t);
static int	vmxnet3_msix_intr_assign(if_ctx_t, int);
static void	vmxnet3_free_irqs(struct vmxnet3_softc *);
static int	vmxnet3_attach_post(if_ctx_t);
static int	vmxnet3_detach(if_ctx_t);
static int	vmxnet3_shutdown(if_ctx_t);
static int	vmxnet3_suspend(if_ctx_t);
static int	vmxnet3_resume(if_ctx_t);

static int	vmxnet3_alloc_resources(struct vmxnet3_softc *);
static void	vmxnet3_free_resources(struct vmxnet3_softc *);
static int	vmxnet3_check_version(struct vmxnet3_softc *);
static void	vmxnet3_set_interrupt_idx(struct vmxnet3_softc *);

static int	vmxnet3_queues_shared_alloc(struct vmxnet3_softc *);
static void	vmxnet3_init_txq(struct vmxnet3_softc *, int);
static int	vmxnet3_tx_queues_alloc(if_ctx_t, caddr_t *, uint64_t *, int, int);
static void	vmxnet3_init_rxq(struct vmxnet3_softc *, int, int);
static int	vmxnet3_rx_queues_alloc(if_ctx_t, caddr_t *, uint64_t *, int, int);
static void	vmxnet3_queues_free(if_ctx_t);

static int	vmxnet3_alloc_shared_data(struct vmxnet3_softc *);
static void	vmxnet3_free_shared_data(struct vmxnet3_softc *);
static int	vmxnet3_alloc_mcast_table(struct vmxnet3_softc *);
static void	vmxnet3_free_mcast_table(struct vmxnet3_softc *);
static void	vmxnet3_init_shared_data(struct vmxnet3_softc *);
static void	vmxnet3_reinit_rss_shared_data(struct vmxnet3_softc *);
static void	vmxnet3_reinit_shared_data(struct vmxnet3_softc *);
static int	vmxnet3_alloc_data(struct vmxnet3_softc *);
static void	vmxnet3_free_data(struct vmxnet3_softc *);

static void	vmxnet3_evintr(struct vmxnet3_softc *);
static int	vmxnet3_isc_txd_encap(void *, if_pkt_info_t);
static void	vmxnet3_isc_txd_flush(void *, uint16_t, qidx_t);
static int	vmxnet3_isc_txd_credits_update(void *, uint16_t, bool);
static int	vmxnet3_isc_rxd_available(void *, uint16_t, qidx_t, qidx_t);
static int	vmxnet3_isc_rxd_pkt_get(void *, if_rxd_info_t);
static void	vmxnet3_isc_rxd_refill(void *, if_rxd_update_t);
static void	vmxnet3_isc_rxd_flush(void *, uint16_t, uint8_t, qidx_t);
static int	vmxnet3_legacy_intr(void *);
static int	vmxnet3_rxq_intr(void *);
static int	vmxnet3_event_intr(void *);

static void	vmxnet3_stop(if_ctx_t);

static void	vmxnet3_txinit(struct vmxnet3_softc *, struct vmxnet3_txqueue *);
static void	vmxnet3_rxinit(struct vmxnet3_softc *, struct vmxnet3_rxqueue *);
static void	vmxnet3_reinit_queues(struct vmxnet3_softc *);
static int	vmxnet3_enable_device(struct vmxnet3_softc *);
static void	vmxnet3_reinit_rxfilters(struct vmxnet3_softc *);
static void	vmxnet3_init(if_ctx_t);
static void	vmxnet3_multi_set(if_ctx_t);
static int	vmxnet3_mtu_set(if_ctx_t, uint32_t);
static void	vmxnet3_media_status(if_ctx_t, struct ifmediareq *);
static int	vmxnet3_media_change(if_ctx_t);
static int	vmxnet3_promisc_set(if_ctx_t, int);
static uint64_t	vmxnet3_get_counter(if_ctx_t, ift_counter);
static void	vmxnet3_update_admin_status(if_ctx_t);
static void	vmxnet3_txq_timer(if_ctx_t, uint16_t);

static void	vmxnet3_update_vlan_filter(struct vmxnet3_softc *, int,
		    uint16_t);
static void	vmxnet3_vlan_register(if_ctx_t, uint16_t);
static void	vmxnet3_vlan_unregister(if_ctx_t, uint16_t);
static void	vmxnet3_set_rxfilter(struct vmxnet3_softc *, int);

static void	vmxnet3_refresh_host_stats(struct vmxnet3_softc *);
static int	vmxnet3_link_is_up(struct vmxnet3_softc *);
static void	vmxnet3_link_status(struct vmxnet3_softc *);
static void	vmxnet3_set_lladdr(struct vmxnet3_softc *);
static void	vmxnet3_get_lladdr(struct vmxnet3_softc *);

static void	vmxnet3_setup_txq_sysctl(struct vmxnet3_txqueue *,
		    struct sysctl_ctx_list *, struct sysctl_oid_list *);
static void	vmxnet3_setup_rxq_sysctl(struct vmxnet3_rxqueue *,
		    struct sysctl_ctx_list *, struct sysctl_oid_list *);
static void	vmxnet3_setup_queue_sysctl(struct vmxnet3_softc *,
		    struct sysctl_ctx_list *, struct sysctl_oid_list *);
static void	vmxnet3_setup_sysctl(struct vmxnet3_softc *);

static void	vmxnet3_write_bar0(struct vmxnet3_softc *, bus_size_t,
		    uint32_t);
static uint32_t	vmxnet3_read_bar1(struct vmxnet3_softc *, bus_size_t);
static void	vmxnet3_write_bar1(struct vmxnet3_softc *, bus_size_t,
		    uint32_t);
static void	vmxnet3_write_cmd(struct vmxnet3_softc *, uint32_t);
static uint32_t	vmxnet3_read_cmd(struct vmxnet3_softc *, uint32_t);

static int	vmxnet3_tx_queue_intr_enable(if_ctx_t, uint16_t);
static int	vmxnet3_rx_queue_intr_enable(if_ctx_t, uint16_t);
static void	vmxnet3_link_intr_enable(if_ctx_t);
static void	vmxnet3_enable_intr(struct vmxnet3_softc *, int);
static void	vmxnet3_disable_intr(struct vmxnet3_softc *, int);
static void	vmxnet3_intr_enable_all(if_ctx_t);
static void	vmxnet3_intr_disable_all(if_ctx_t);

typedef enum {
	VMXNET3_BARRIER_RD,
	VMXNET3_BARRIER_WR,
	VMXNET3_BARRIER_RDWR,
} vmxnet3_barrier_t;

static void	vmxnet3_barrier(struct vmxnet3_softc *, vmxnet3_barrier_t);


static device_method_t vmxnet3_methods[] = {
	/* Device interface */
	DEVMETHOD(device_register, vmxnet3_register),
	DEVMETHOD(device_probe, iflib_device_probe),
	DEVMETHOD(device_attach, iflib_device_attach),
	DEVMETHOD(device_detach, iflib_device_detach),
	DEVMETHOD(device_shutdown, iflib_device_shutdown),
	DEVMETHOD(device_suspend, iflib_device_suspend),
	DEVMETHOD(device_resume, iflib_device_resume),
	DEVMETHOD_END
};

static driver_t vmxnet3_driver = {
	"vmx", vmxnet3_methods, sizeof(struct vmxnet3_softc)
};

static devclass_t vmxnet3_devclass;
DRIVER_MODULE(vmx, pci, vmxnet3_driver, vmxnet3_devclass, 0, 0);
IFLIB_PNP_INFO(pci, vmx, vmxnet3_vendor_info_array);
MODULE_VERSION(vmx, 2);

MODULE_DEPEND(vmx, pci, 1, 1, 1);
MODULE_DEPEND(vmx, ether, 1, 1, 1);
MODULE_DEPEND(vmx, iflib, 1, 1, 1);

static device_method_t vmxnet3_iflib_methods[] = {
	DEVMETHOD(ifdi_tx_queues_alloc, vmxnet3_tx_queues_alloc),
	DEVMETHOD(ifdi_rx_queues_alloc, vmxnet3_rx_queues_alloc),
	DEVMETHOD(ifdi_queues_free, vmxnet3_queues_free),

	DEVMETHOD(ifdi_attach_pre, vmxnet3_attach_pre),
	DEVMETHOD(ifdi_attach_post, vmxnet3_attach_post),
	DEVMETHOD(ifdi_detach, vmxnet3_detach),

	DEVMETHOD(ifdi_init, vmxnet3_init),
	DEVMETHOD(ifdi_stop, vmxnet3_stop),
	DEVMETHOD(ifdi_multi_set, vmxnet3_multi_set),
	DEVMETHOD(ifdi_mtu_set, vmxnet3_mtu_set),
	DEVMETHOD(ifdi_media_status, vmxnet3_media_status),
	DEVMETHOD(ifdi_media_change, vmxnet3_media_change),
	DEVMETHOD(ifdi_promisc_set, vmxnet3_promisc_set),
	DEVMETHOD(ifdi_get_counter, vmxnet3_get_counter),
	DEVMETHOD(ifdi_update_admin_status, vmxnet3_update_admin_status),
	DEVMETHOD(ifdi_timer, vmxnet3_txq_timer),

	DEVMETHOD(ifdi_tx_queue_intr_enable, vmxnet3_tx_queue_intr_enable),
	DEVMETHOD(ifdi_rx_queue_intr_enable, vmxnet3_rx_queue_intr_enable),
	DEVMETHOD(ifdi_link_intr_enable, vmxnet3_link_intr_enable),
	DEVMETHOD(ifdi_intr_enable, vmxnet3_intr_enable_all),
	DEVMETHOD(ifdi_intr_disable, vmxnet3_intr_disable_all),
	DEVMETHOD(ifdi_msix_intr_assign, vmxnet3_msix_intr_assign),

	DEVMETHOD(ifdi_vlan_register, vmxnet3_vlan_register),
	DEVMETHOD(ifdi_vlan_unregister, vmxnet3_vlan_unregister),

	DEVMETHOD(ifdi_shutdown, vmxnet3_shutdown),
	DEVMETHOD(ifdi_suspend, vmxnet3_suspend),
	DEVMETHOD(ifdi_resume, vmxnet3_resume),

	DEVMETHOD_END
};

static driver_t vmxnet3_iflib_driver = {
	"vmx", vmxnet3_iflib_methods, sizeof(struct vmxnet3_softc)
};

struct if_txrx vmxnet3_txrx = {
	.ift_txd_encap = vmxnet3_isc_txd_encap,
	.ift_txd_flush = vmxnet3_isc_txd_flush,
	.ift_txd_credits_update = vmxnet3_isc_txd_credits_update,
	.ift_rxd_available = vmxnet3_isc_rxd_available,
	.ift_rxd_pkt_get = vmxnet3_isc_rxd_pkt_get,
	.ift_rxd_refill = vmxnet3_isc_rxd_refill,
	.ift_rxd_flush = vmxnet3_isc_rxd_flush,
	.ift_legacy_intr = vmxnet3_legacy_intr
};

static struct if_shared_ctx vmxnet3_sctx_init = {
	.isc_magic = IFLIB_MAGIC,
	.isc_q_align = 512,

	.isc_tx_maxsize = VMXNET3_TX_MAXSIZE,
	.isc_tx_maxsegsize = VMXNET3_TX_MAXSEGSIZE,
	.isc_tso_maxsize = VMXNET3_TSO_MAXSIZE + sizeof(struct ether_vlan_header),
	.isc_tso_maxsegsize = VMXNET3_TX_MAXSEGSIZE,

	/*
	 * These values are used to configure the busdma tag used for
	 * receive descriptors.  Each receive descriptor only points to one
	 * buffer.
	 */
	.isc_rx_maxsize = VMXNET3_RX_MAXSEGSIZE, /* One buf per descriptor */
	.isc_rx_nsegments = 1,  /* One mapping per descriptor */
	.isc_rx_maxsegsize = VMXNET3_RX_MAXSEGSIZE,

	.isc_admin_intrcnt = 1,
	.isc_vendor_info = vmxnet3_vendor_info_array,
	.isc_driver_version = "2",
	.isc_driver = &vmxnet3_iflib_driver,
	.isc_flags = IFLIB_HAS_RXCQ | IFLIB_HAS_TXCQ,

	/*
	 * Number of receive queues per receive queue set, with associated
	 * descriptor settings for each.
	 */
	.isc_nrxqs = 3,
	.isc_nfl = 2, /* one free list for each receive command queue */
	.isc_nrxd_min = {VMXNET3_MIN_RX_NDESC, VMXNET3_MIN_RX_NDESC, VMXNET3_MIN_RX_NDESC},
	.isc_nrxd_max = {VMXNET3_MAX_RX_NDESC, VMXNET3_MAX_RX_NDESC, VMXNET3_MAX_RX_NDESC},
	.isc_nrxd_default = {VMXNET3_DEF_RX_NDESC, VMXNET3_DEF_RX_NDESC, VMXNET3_DEF_RX_NDESC},

	/*
	 * Number of transmit queues per transmit queue set, with associated
	 * descriptor settings for each.
	 */
	.isc_ntxqs = 2,
	.isc_ntxd_min = {VMXNET3_MIN_TX_NDESC, VMXNET3_MIN_TX_NDESC},
	.isc_ntxd_max = {VMXNET3_MAX_TX_NDESC, VMXNET3_MAX_TX_NDESC},
	.isc_ntxd_default = {VMXNET3_DEF_TX_NDESC, VMXNET3_DEF_TX_NDESC},
};

static void *
vmxnet3_register(device_t dev)
{
	return (&vmxnet3_sctx_init);
}

static int
vmxnet3_attach_pre(if_ctx_t ctx)
{
	device_t dev;
	if_softc_ctx_t scctx;
	struct vmxnet3_softc *sc;
	uint32_t intr_config;
	int error;

	dev = iflib_get_dev(ctx);
	sc = iflib_get_softc(ctx);
	sc->vmx_dev = dev;
	sc->vmx_ctx = ctx;
	sc->vmx_sctx = iflib_get_sctx(ctx);
	sc->vmx_scctx = iflib_get_softc_ctx(ctx);
	sc->vmx_ifp = iflib_get_ifp(ctx);
	sc->vmx_media = iflib_get_media(ctx);
	scctx = sc->vmx_scctx;

	scctx->isc_tx_nsegments = VMXNET3_TX_MAXSEGS;
	scctx->isc_tx_tso_segments_max = VMXNET3_TX_MAXSEGS;
	/* isc_tx_tso_size_max doesn't include possible vlan header */
	scctx->isc_tx_tso_size_max = VMXNET3_TSO_MAXSIZE;
	scctx->isc_tx_tso_segsize_max = VMXNET3_TX_MAXSEGSIZE;
	scctx->isc_txrx = &vmxnet3_txrx;

	/* If 0, the iflib tunable was not set, so set to the default */
	if (scctx->isc_nrxqsets == 0)
		scctx->isc_nrxqsets = VMXNET3_DEF_RX_QUEUES;
	scctx->isc_nrxqsets_max = min(VMXNET3_MAX_RX_QUEUES, mp_ncpus);

	/* If 0, the iflib tunable was not set, so set to the default */
	if (scctx->isc_ntxqsets == 0)
		scctx->isc_ntxqsets = VMXNET3_DEF_TX_QUEUES;
	scctx->isc_ntxqsets_max = min(VMXNET3_MAX_TX_QUEUES, mp_ncpus);

	/*
	 * Enforce that the transmit completion queue descriptor count is
	 * the same as the transmit command queue descriptor count.
	 */
	scctx->isc_ntxd[0] = scctx->isc_ntxd[1];
	scctx->isc_txqsizes[0] =
	    sizeof(struct vmxnet3_txcompdesc) * scctx->isc_ntxd[0];
	scctx->isc_txqsizes[1] =
	    sizeof(struct vmxnet3_txdesc) * scctx->isc_ntxd[1];

	/*
	 * Enforce that the receive completion queue descriptor count is the
	 * sum of the receive command queue descriptor counts, and that the
	 * second receive command queue descriptor count is the same as the
	 * first one.
	 */
	scctx->isc_nrxd[2] = scctx->isc_nrxd[1];
	scctx->isc_nrxd[0] = scctx->isc_nrxd[1] + scctx->isc_nrxd[2];
	scctx->isc_rxqsizes[0] =
	    sizeof(struct vmxnet3_rxcompdesc) * scctx->isc_nrxd[0];
	scctx->isc_rxqsizes[1] =
	    sizeof(struct vmxnet3_rxdesc) * scctx->isc_nrxd[1];
	scctx->isc_rxqsizes[2] =
	    sizeof(struct vmxnet3_rxdesc) * scctx->isc_nrxd[2];

	scctx->isc_rss_table_size = UPT1_RSS_MAX_IND_TABLE_SIZE;

	/* Map PCI BARs */
	error = vmxnet3_alloc_resources(sc);
	if (error)
		goto fail;

	/* Check device versions */
	error = vmxnet3_check_version(sc);
	if (error)
		goto fail;

	/* 
	 * The interrupt mode can be set in the hypervisor configuration via
	 * the parameter ethernet<N>.intrMode.
	 */
	intr_config = vmxnet3_read_cmd(sc, VMXNET3_CMD_GET_INTRCFG);
	sc->vmx_intr_mask_mode = (intr_config >> 2) & 0x03;

	/*
	 * Configure the softc context to attempt to configure the interrupt
	 * mode now indicated by intr_config.  iflib will follow the usual
	 * fallback path MSI-X -> MSI -> LEGACY, starting at the configured
	 * starting mode.
	 */
	switch (intr_config & 0x03) {
	case VMXNET3_IT_AUTO:
	case VMXNET3_IT_MSIX:
		scctx->isc_msix_bar = pci_msix_table_bar(dev);
		break;
	case VMXNET3_IT_MSI:
		scctx->isc_msix_bar = -1;
		scctx->isc_disable_msix = 1;
		break;
	case VMXNET3_IT_LEGACY:
		scctx->isc_msix_bar = 0;
		break;
	}

	scctx->isc_tx_csum_flags = VMXNET3_CSUM_ALL_OFFLOAD;
	scctx->isc_capabilities = scctx->isc_capenable =
	    IFCAP_TXCSUM | IFCAP_TXCSUM_IPV6 |
	    IFCAP_TSO4 | IFCAP_TSO6 |
	    IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6 |
	    IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING |
	    IFCAP_VLAN_HWCSUM | IFCAP_VLAN_HWTSO |
	    IFCAP_JUMBO_MTU;

	/* These capabilities are not enabled by default. */
	scctx->isc_capabilities |= IFCAP_LRO | IFCAP_VLAN_HWFILTER;

	vmxnet3_get_lladdr(sc);
	iflib_set_mac(ctx, sc->vmx_lladdr);

	return (0);
fail:
	/*
	 * We must completely clean up anything allocated above as iflib
	 * will not invoke any other driver entry points as a result of this
	 * failure.
	 */
	vmxnet3_free_resources(sc);

	return (error);
}

static int
vmxnet3_msix_intr_assign(if_ctx_t ctx, int msix)
{
	struct vmxnet3_softc *sc;
	if_softc_ctx_t scctx;
	struct vmxnet3_rxqueue *rxq;
	int error;
	int i;
	char irq_name[16];

	sc = iflib_get_softc(ctx);
	scctx = sc->vmx_scctx;
	
	for (i = 0; i < scctx->isc_nrxqsets; i++) {
		snprintf(irq_name, sizeof(irq_name), "rxq%d", i);

		rxq = &sc->vmx_rxq[i];
		error = iflib_irq_alloc_generic(ctx, &rxq->vxrxq_irq, i + 1,
		    IFLIB_INTR_RX, vmxnet3_rxq_intr, rxq, i, irq_name);
		if (error) {
			device_printf(iflib_get_dev(ctx),
			    "Failed to register rxq %d interrupt handler\n", i);
			return (error);
		}
	}

	for (i = 0; i < scctx->isc_ntxqsets; i++) {
		snprintf(irq_name, sizeof(irq_name), "txq%d", i);

		/*
		 * Don't provide the corresponding rxq irq for reference -
		 * we want the transmit task to be attached to a task queue
		 * that is different from the one used by the corresponding
		 * rxq irq.  That is because the TX doorbell writes are very
		 * expensive as virtualized MMIO operations, so we want to
		 * be able to defer them to another core when possible so
		 * that they don't steal receive processing cycles during
		 * stack turnarounds like TCP ACK generation.  The other
		 * piece to this approach is enabling the iflib abdicate
		 * option (currently via an interface-specific
		 * tunable/sysctl).
		 */
		iflib_softirq_alloc_generic(ctx, NULL, IFLIB_INTR_TX, NULL, i,
		    irq_name);
	}

	error = iflib_irq_alloc_generic(ctx, &sc->vmx_event_intr_irq,
	    scctx->isc_nrxqsets + 1, IFLIB_INTR_ADMIN, vmxnet3_event_intr, sc, 0,
	    "event");
	if (error) {
		device_printf(iflib_get_dev(ctx),
		    "Failed to register event interrupt handler\n");
		return (error);
	}

	return (0);
}

static void
vmxnet3_free_irqs(struct vmxnet3_softc *sc)
{
	if_softc_ctx_t scctx;
	struct vmxnet3_rxqueue *rxq;
	int i;

	scctx = sc->vmx_scctx;

	for (i = 0; i < scctx->isc_nrxqsets; i++) {
		rxq = &sc->vmx_rxq[i];
		iflib_irq_free(sc->vmx_ctx, &rxq->vxrxq_irq);
	}

	iflib_irq_free(sc->vmx_ctx, &sc->vmx_event_intr_irq);
}

static int
vmxnet3_attach_post(if_ctx_t ctx)
{
	device_t dev;
	if_softc_ctx_t scctx;
	struct vmxnet3_softc *sc;
	int error;

	dev = iflib_get_dev(ctx);
	scctx = iflib_get_softc_ctx(ctx);
	sc = iflib_get_softc(ctx);

	if (scctx->isc_nrxqsets > 1)
		sc->vmx_flags |= VMXNET3_FLAG_RSS;

	error = vmxnet3_alloc_data(sc);
	if (error)
		goto fail;

	vmxnet3_set_interrupt_idx(sc);
	vmxnet3_setup_sysctl(sc);

	ifmedia_add(sc->vmx_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(sc->vmx_media, IFM_ETHER | IFM_AUTO);

fail:
	return (error);
}

static int
vmxnet3_detach(if_ctx_t ctx)
{
	struct vmxnet3_softc *sc;

	sc = iflib_get_softc(ctx);

	vmxnet3_free_irqs(sc);
	vmxnet3_free_data(sc);
	vmxnet3_free_resources(sc);

	return (0);
}

static int
vmxnet3_shutdown(if_ctx_t ctx)
{

	return (0);
}

static int
vmxnet3_suspend(if_ctx_t ctx)
{

	return (0);
}

static int
vmxnet3_resume(if_ctx_t ctx)
{

	return (0);
}

static int
vmxnet3_alloc_resources(struct vmxnet3_softc *sc)
{
	device_t dev;
	int rid;

	dev = sc->vmx_dev;

	rid = PCIR_BAR(0);
	sc->vmx_res0 = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->vmx_res0 == NULL) {
		device_printf(dev,
		    "could not map BAR0 memory\n");
		return (ENXIO);
	}

	sc->vmx_iot0 = rman_get_bustag(sc->vmx_res0);
	sc->vmx_ioh0 = rman_get_bushandle(sc->vmx_res0);

	rid = PCIR_BAR(1);
	sc->vmx_res1 = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->vmx_res1 == NULL) {
		device_printf(dev,
		    "could not map BAR1 memory\n");
		return (ENXIO);
	}

	sc->vmx_iot1 = rman_get_bustag(sc->vmx_res1);
	sc->vmx_ioh1 = rman_get_bushandle(sc->vmx_res1);

	return (0);
}

static void
vmxnet3_free_resources(struct vmxnet3_softc *sc)
{
	device_t dev;

	dev = sc->vmx_dev;

	if (sc->vmx_res0 != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->vmx_res0), sc->vmx_res0);
		sc->vmx_res0 = NULL;
	}

	if (sc->vmx_res1 != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->vmx_res1), sc->vmx_res1);
		sc->vmx_res1 = NULL;
	}
}

static int
vmxnet3_check_version(struct vmxnet3_softc *sc)
{
	device_t dev;
	uint32_t version;

	dev = sc->vmx_dev;

	version = vmxnet3_read_bar1(sc, VMXNET3_BAR1_VRRS);
	if ((version & 0x01) == 0) {
		device_printf(dev, "unsupported hardware version %#x\n",
		    version);
		return (ENOTSUP);
	}
	vmxnet3_write_bar1(sc, VMXNET3_BAR1_VRRS, 1);

	version = vmxnet3_read_bar1(sc, VMXNET3_BAR1_UVRS);
	if ((version & 0x01) == 0) {
		device_printf(dev, "unsupported UPT version %#x\n", version);
		return (ENOTSUP);
	}
	vmxnet3_write_bar1(sc, VMXNET3_BAR1_UVRS, 1);

	return (0);
}

static void
vmxnet3_set_interrupt_idx(struct vmxnet3_softc *sc)
{
	if_softc_ctx_t scctx;
	struct vmxnet3_txqueue *txq;
	struct vmxnet3_txq_shared *txs;
	struct vmxnet3_rxqueue *rxq;
	struct vmxnet3_rxq_shared *rxs;
	int intr_idx;
	int i;

	scctx = sc->vmx_scctx;

	/*
	 * There is always one interrupt per receive queue, assigned
	 * starting with the first interrupt.  When there is only one
	 * interrupt available, the event interrupt shares the receive queue
	 * interrupt, otherwise it uses the interrupt following the last
	 * receive queue interrupt.  Transmit queues are not assigned
	 * interrupts, so they are given indexes beyond the indexes that
	 * correspond to the real interrupts.
	 */

	/* The event interrupt is always the last vector. */
	sc->vmx_event_intr_idx = scctx->isc_vectors - 1;

	intr_idx = 0;
	for (i = 0; i < scctx->isc_nrxqsets; i++, intr_idx++) {
		rxq = &sc->vmx_rxq[i];
		rxs = rxq->vxrxq_rs;
		rxq->vxrxq_intr_idx = intr_idx;
		rxs->intr_idx = rxq->vxrxq_intr_idx;
	}

	/*
	 * Assign the tx queues interrupt indexes above what we are actually
	 * using.  These interrupts will never be enabled.
	 */
	intr_idx = scctx->isc_vectors;
	for (i = 0; i < scctx->isc_ntxqsets; i++, intr_idx++) {
		txq = &sc->vmx_txq[i];
		txs = txq->vxtxq_ts;
		txq->vxtxq_intr_idx = intr_idx;
		txs->intr_idx = txq->vxtxq_intr_idx;
	}
}

static int
vmxnet3_queues_shared_alloc(struct vmxnet3_softc *sc)
{
	if_softc_ctx_t scctx;
	int size;
	int error;
	
	scctx = sc->vmx_scctx;

	/*
	 * The txq and rxq shared data areas must be allocated contiguously
	 * as vmxnet3_driver_shared contains only a single address member
	 * for the shared queue data area.
	 */
	size = scctx->isc_ntxqsets * sizeof(struct vmxnet3_txq_shared) +
	    scctx->isc_nrxqsets * sizeof(struct vmxnet3_rxq_shared);
	error = iflib_dma_alloc_align(sc->vmx_ctx, size, 128, &sc->vmx_qs_dma, 0);
	if (error) {
		device_printf(sc->vmx_dev, "cannot alloc queue shared memory\n");
		return (error);
	}

	return (0);
}

static void
vmxnet3_init_txq(struct vmxnet3_softc *sc, int q)
{
	struct vmxnet3_txqueue *txq;
	struct vmxnet3_comp_ring *txc;
	struct vmxnet3_txring *txr;
	if_softc_ctx_t scctx;
	
	txq = &sc->vmx_txq[q];
	txc = &txq->vxtxq_comp_ring;
	txr = &txq->vxtxq_cmd_ring;
	scctx = sc->vmx_scctx;

	snprintf(txq->vxtxq_name, sizeof(txq->vxtxq_name), "%s-tx%d",
	    device_get_nameunit(sc->vmx_dev), q);

	txq->vxtxq_sc = sc;
	txq->vxtxq_id = q;
	txc->vxcr_ndesc = scctx->isc_ntxd[0];
	txr->vxtxr_ndesc = scctx->isc_ntxd[1];
}

static int
vmxnet3_tx_queues_alloc(if_ctx_t ctx, caddr_t *vaddrs, uint64_t *paddrs,
    int ntxqs, int ntxqsets)
{
	struct vmxnet3_softc *sc;
	int q;
	int error;
	caddr_t kva;
	
	sc = iflib_get_softc(ctx);

	/* Allocate the array of transmit queues */
	sc->vmx_txq = malloc(sizeof(struct vmxnet3_txqueue) *
	    ntxqsets, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->vmx_txq == NULL)
		return (ENOMEM);

	/* Initialize driver state for each transmit queue */
	for (q = 0; q < ntxqsets; q++)
		vmxnet3_init_txq(sc, q);

	/*
	 * Allocate queue state that is shared with the device.  This check
	 * and call is performed in both vmxnet3_tx_queues_alloc() and
	 * vmxnet3_rx_queues_alloc() so that we don't have to care which
	 * order iflib invokes those routines in.
	 */
	if (sc->vmx_qs_dma.idi_size == 0) {
		error = vmxnet3_queues_shared_alloc(sc);
		if (error)
			return (error);
	}

	kva = sc->vmx_qs_dma.idi_vaddr;
	for (q = 0; q < ntxqsets; q++) {
		sc->vmx_txq[q].vxtxq_ts = (struct vmxnet3_txq_shared *) kva;
		kva += sizeof(struct vmxnet3_txq_shared);
	}

	/* Record descriptor ring vaddrs and paddrs */
	for (q = 0; q < ntxqsets; q++) {
		struct vmxnet3_txqueue *txq;
		struct vmxnet3_txring *txr;
		struct vmxnet3_comp_ring *txc;

		txq = &sc->vmx_txq[q];
		txc = &txq->vxtxq_comp_ring;
		txr = &txq->vxtxq_cmd_ring;

		/* Completion ring */
		txc->vxcr_u.txcd =
		    (struct vmxnet3_txcompdesc *) vaddrs[q * ntxqs + 0];
		txc->vxcr_paddr = paddrs[q * ntxqs + 0];

		/* Command ring */
		txr->vxtxr_txd =
		    (struct vmxnet3_txdesc *) vaddrs[q * ntxqs + 1];
		txr->vxtxr_paddr = paddrs[q * ntxqs + 1];
	}

	return (0);
}

static void
vmxnet3_init_rxq(struct vmxnet3_softc *sc, int q, int nrxqs)
{
	struct vmxnet3_rxqueue *rxq;
	struct vmxnet3_comp_ring *rxc;
	struct vmxnet3_rxring *rxr;
	if_softc_ctx_t scctx;
	int i;

	rxq = &sc->vmx_rxq[q];
	rxc = &rxq->vxrxq_comp_ring;
	scctx = sc->vmx_scctx;

	snprintf(rxq->vxrxq_name, sizeof(rxq->vxrxq_name), "%s-rx%d",
	    device_get_nameunit(sc->vmx_dev), q);

	rxq->vxrxq_sc = sc;
	rxq->vxrxq_id = q;

	/*
	 * First rxq is the completion queue, so there are nrxqs - 1 command
	 * rings starting at iflib queue id 1.
	 */
	rxc->vxcr_ndesc = scctx->isc_nrxd[0];
	for (i = 0; i < nrxqs - 1; i++) {
		rxr = &rxq->vxrxq_cmd_ring[i];
		rxr->vxrxr_ndesc = scctx->isc_nrxd[i + 1];
	}
}

static int
vmxnet3_rx_queues_alloc(if_ctx_t ctx, caddr_t *vaddrs, uint64_t *paddrs,
    int nrxqs, int nrxqsets)
{
	struct vmxnet3_softc *sc;
	if_softc_ctx_t scctx;
	int q;
	int i;
	int error;
	caddr_t kva;
	
	sc = iflib_get_softc(ctx);
	scctx = sc->vmx_scctx;

	/* Allocate the array of receive queues */
	sc->vmx_rxq = malloc(sizeof(struct vmxnet3_rxqueue) *
	    nrxqsets, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->vmx_rxq == NULL)
		return (ENOMEM);

	/* Initialize driver state for each receive queue */
	for (q = 0; q < nrxqsets; q++)
		vmxnet3_init_rxq(sc, q, nrxqs);

	/*
	 * Allocate queue state that is shared with the device.  This check
	 * and call is performed in both vmxnet3_tx_queues_alloc() and
	 * vmxnet3_rx_queues_alloc() so that we don't have to care which
	 * order iflib invokes those routines in.
	 */
	if (sc->vmx_qs_dma.idi_size == 0) {
		error = vmxnet3_queues_shared_alloc(sc);
		if (error)
			return (error);
	}

	kva = sc->vmx_qs_dma.idi_vaddr +
	    scctx->isc_ntxqsets * sizeof(struct vmxnet3_txq_shared);
	for (q = 0; q < nrxqsets; q++) {
		sc->vmx_rxq[q].vxrxq_rs = (struct vmxnet3_rxq_shared *) kva;
		kva += sizeof(struct vmxnet3_rxq_shared);
	}

	/* Record descriptor ring vaddrs and paddrs */
	for (q = 0; q < nrxqsets; q++) {
		struct vmxnet3_rxqueue *rxq;
		struct vmxnet3_rxring *rxr;
		struct vmxnet3_comp_ring *rxc;

		rxq = &sc->vmx_rxq[q];
		rxc = &rxq->vxrxq_comp_ring;

		/* Completion ring */
		rxc->vxcr_u.rxcd =
		    (struct vmxnet3_rxcompdesc *) vaddrs[q * nrxqs + 0];
		rxc->vxcr_paddr = paddrs[q * nrxqs + 0];

		/* Command ring(s) */
		for (i = 0; i < nrxqs - 1; i++) {
			rxr = &rxq->vxrxq_cmd_ring[i];

			rxr->vxrxr_rxd =
			    (struct vmxnet3_rxdesc *) vaddrs[q * nrxqs + 1 + i];
			rxr->vxrxr_paddr = paddrs[q * nrxqs + 1 + i];
		}
	}

	return (0);
}

static void
vmxnet3_queues_free(if_ctx_t ctx)
{
	struct vmxnet3_softc *sc;

	sc = iflib_get_softc(ctx);

	/* Free queue state area that is shared with the device */
	if (sc->vmx_qs_dma.idi_size != 0) {
		iflib_dma_free(&sc->vmx_qs_dma);
		sc->vmx_qs_dma.idi_size = 0;
	}

	/* Free array of receive queues */
	if (sc->vmx_rxq != NULL) {
		free(sc->vmx_rxq, M_DEVBUF);
		sc->vmx_rxq = NULL;
	}

	/* Free array of transmit queues */
	if (sc->vmx_txq != NULL) {
		free(sc->vmx_txq, M_DEVBUF);
		sc->vmx_txq = NULL;
	}
}

static int
vmxnet3_alloc_shared_data(struct vmxnet3_softc *sc)
{
	device_t dev;
	size_t size;
	int error;

	dev = sc->vmx_dev;

	/* Top level state structure shared with the device */
	size = sizeof(struct vmxnet3_driver_shared);
	error = iflib_dma_alloc_align(sc->vmx_ctx, size, 1, &sc->vmx_ds_dma, 0);
	if (error) {
		device_printf(dev, "cannot alloc shared memory\n");
		return (error);
	}
	sc->vmx_ds = (struct vmxnet3_driver_shared *) sc->vmx_ds_dma.idi_vaddr;

	/* RSS table state shared with the device */
	if (sc->vmx_flags & VMXNET3_FLAG_RSS) {
		size = sizeof(struct vmxnet3_rss_shared);
		error = iflib_dma_alloc_align(sc->vmx_ctx, size, 128,
		    &sc->vmx_rss_dma, 0);
		if (error) {
			device_printf(dev, "cannot alloc rss shared memory\n");
			return (error);
		}
		sc->vmx_rss =
		    (struct vmxnet3_rss_shared *) sc->vmx_rss_dma.idi_vaddr;
	}

	return (0);
}

static void
vmxnet3_free_shared_data(struct vmxnet3_softc *sc)
{

	/* Free RSS table state shared with the device */
	if (sc->vmx_rss != NULL) {
		iflib_dma_free(&sc->vmx_rss_dma);
		sc->vmx_rss = NULL;
	}

	/* Free top level state structure shared with the device */
	if (sc->vmx_ds != NULL) {
		iflib_dma_free(&sc->vmx_ds_dma);
		sc->vmx_ds = NULL;
	}
}

static int
vmxnet3_alloc_mcast_table(struct vmxnet3_softc *sc)
{
	int error;

	/* Multicast table state shared with the device */
	error = iflib_dma_alloc_align(sc->vmx_ctx,
	    VMXNET3_MULTICAST_MAX * ETHER_ADDR_LEN, 32, &sc->vmx_mcast_dma, 0);
	if (error)
		device_printf(sc->vmx_dev, "unable to alloc multicast table\n");
	else
		sc->vmx_mcast = sc->vmx_mcast_dma.idi_vaddr;

	return (error);
}

static void
vmxnet3_free_mcast_table(struct vmxnet3_softc *sc)
{

	/* Free multicast table state shared with the device */
	if (sc->vmx_mcast != NULL) {
		iflib_dma_free(&sc->vmx_mcast_dma);
		sc->vmx_mcast = NULL;
	}
}

static void
vmxnet3_init_shared_data(struct vmxnet3_softc *sc)
{
	struct vmxnet3_driver_shared *ds;
	if_shared_ctx_t sctx;
	if_softc_ctx_t scctx;
	struct vmxnet3_txqueue *txq;
	struct vmxnet3_txq_shared *txs;
	struct vmxnet3_rxqueue *rxq;
	struct vmxnet3_rxq_shared *rxs;
	int i;

	ds = sc->vmx_ds;
	sctx = sc->vmx_sctx;
	scctx = sc->vmx_scctx;

	/*
	 * Initialize fields of the shared data that remains the same across
	 * reinits. Note the shared data is zero'd when allocated.
	 */

	ds->magic = VMXNET3_REV1_MAGIC;

	/* DriverInfo */
	ds->version = VMXNET3_DRIVER_VERSION;
	ds->guest = VMXNET3_GOS_FREEBSD |
#ifdef __LP64__
	    VMXNET3_GOS_64BIT;
#else
	    VMXNET3_GOS_32BIT;
#endif
	ds->vmxnet3_revision = 1;
	ds->upt_version = 1;

	/* Misc. conf */
	ds->driver_data = vtophys(sc);
	ds->driver_data_len = sizeof(struct vmxnet3_softc);
	ds->queue_shared = sc->vmx_qs_dma.idi_paddr;
	ds->queue_shared_len = sc->vmx_qs_dma.idi_size;
	ds->nrxsg_max = IFLIB_MAX_RX_SEGS;

	/* RSS conf */
	if (sc->vmx_flags & VMXNET3_FLAG_RSS) {
		ds->rss.version = 1;
		ds->rss.paddr = sc->vmx_rss_dma.idi_paddr;
		ds->rss.len = sc->vmx_rss_dma.idi_size;
	}

	/* Interrupt control. */
	ds->automask = sc->vmx_intr_mask_mode == VMXNET3_IMM_AUTO;
	/*
	 * Total number of interrupt indexes we are using in the shared
	 * config data, even though we don't actually allocate interrupt
	 * resources for the tx queues.  Some versions of the device will
	 * fail to initialize successfully if interrupt indexes are used in
	 * the shared config that exceed the number of interrupts configured
	 * here.
	 */
	ds->nintr = (scctx->isc_vectors == 1) ?
	    2 : (scctx->isc_nrxqsets + scctx->isc_ntxqsets + 1);
	ds->evintr = sc->vmx_event_intr_idx;
	ds->ictrl = VMXNET3_ICTRL_DISABLE_ALL;

	for (i = 0; i < ds->nintr; i++)
		ds->modlevel[i] = UPT1_IMOD_ADAPTIVE;

	/* Receive filter. */
	ds->mcast_table = sc->vmx_mcast_dma.idi_paddr;
	ds->mcast_tablelen = sc->vmx_mcast_dma.idi_size;

	/* Tx queues */
	for (i = 0; i < scctx->isc_ntxqsets; i++) {
		txq = &sc->vmx_txq[i];
		txs = txq->vxtxq_ts;

		txs->cmd_ring = txq->vxtxq_cmd_ring.vxtxr_paddr;
		txs->cmd_ring_len = txq->vxtxq_cmd_ring.vxtxr_ndesc;
		txs->comp_ring = txq->vxtxq_comp_ring.vxcr_paddr;
		txs->comp_ring_len = txq->vxtxq_comp_ring.vxcr_ndesc;
		txs->driver_data = vtophys(txq);
		txs->driver_data_len = sizeof(struct vmxnet3_txqueue);
	}

	/* Rx queues */
	for (i = 0; i < scctx->isc_nrxqsets; i++) {
		rxq = &sc->vmx_rxq[i];
		rxs = rxq->vxrxq_rs;

		rxs->cmd_ring[0] = rxq->vxrxq_cmd_ring[0].vxrxr_paddr;
		rxs->cmd_ring_len[0] = rxq->vxrxq_cmd_ring[0].vxrxr_ndesc;
		rxs->cmd_ring[1] = rxq->vxrxq_cmd_ring[1].vxrxr_paddr;
		rxs->cmd_ring_len[1] = rxq->vxrxq_cmd_ring[1].vxrxr_ndesc;
		rxs->comp_ring = rxq->vxrxq_comp_ring.vxcr_paddr;
		rxs->comp_ring_len = rxq->vxrxq_comp_ring.vxcr_ndesc;
		rxs->driver_data = vtophys(rxq);
		rxs->driver_data_len = sizeof(struct vmxnet3_rxqueue);
	}
}

static void
vmxnet3_reinit_rss_shared_data(struct vmxnet3_softc *sc)
{
	/*
	 * Use the same key as the Linux driver until FreeBSD can do
	 * RSS (presumably Toeplitz) in software.
	 */
	static const uint8_t rss_key[UPT1_RSS_MAX_KEY_SIZE] = {
	    0x3b, 0x56, 0xd1, 0x56, 0x13, 0x4a, 0xe7, 0xac,
	    0xe8, 0x79, 0x09, 0x75, 0xe8, 0x65, 0x79, 0x28,
	    0x35, 0x12, 0xb9, 0x56, 0x7c, 0x76, 0x4b, 0x70,
	    0xd8, 0x56, 0xa3, 0x18, 0x9b, 0x0a, 0xee, 0xf3,
	    0x96, 0xa6, 0x9f, 0x8f, 0x9e, 0x8c, 0x90, 0xc9,
	};

	struct vmxnet3_driver_shared *ds;
	if_softc_ctx_t scctx;
	struct vmxnet3_rss_shared *rss;
	int i;
	
	ds = sc->vmx_ds;
	scctx = sc->vmx_scctx;
	rss = sc->vmx_rss;

	rss->hash_type =
	    UPT1_RSS_HASH_TYPE_IPV4 | UPT1_RSS_HASH_TYPE_TCP_IPV4 |
	    UPT1_RSS_HASH_TYPE_IPV6 | UPT1_RSS_HASH_TYPE_TCP_IPV6;
	rss->hash_func = UPT1_RSS_HASH_FUNC_TOEPLITZ;
	rss->hash_key_size = UPT1_RSS_MAX_KEY_SIZE;
	rss->ind_table_size = UPT1_RSS_MAX_IND_TABLE_SIZE;
	memcpy(rss->hash_key, rss_key, UPT1_RSS_MAX_KEY_SIZE);

	for (i = 0; i < UPT1_RSS_MAX_IND_TABLE_SIZE; i++)
		rss->ind_table[i] = i % scctx->isc_nrxqsets;
}

static void
vmxnet3_reinit_shared_data(struct vmxnet3_softc *sc)
{
	struct ifnet *ifp;
	struct vmxnet3_driver_shared *ds;
	if_softc_ctx_t scctx;
	
	ifp = sc->vmx_ifp;
	ds = sc->vmx_ds;
	scctx = sc->vmx_scctx;
	
	ds->mtu = ifp->if_mtu;
	ds->ntxqueue = scctx->isc_ntxqsets;
	ds->nrxqueue = scctx->isc_nrxqsets;

	ds->upt_features = 0;
	if (ifp->if_capenable & (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6))
		ds->upt_features |= UPT1_F_CSUM;
	if (ifp->if_capenable & IFCAP_VLAN_HWTAGGING)
		ds->upt_features |= UPT1_F_VLAN;
	if (ifp->if_capenable & IFCAP_LRO)
		ds->upt_features |= UPT1_F_LRO;

	if (sc->vmx_flags & VMXNET3_FLAG_RSS) {
		ds->upt_features |= UPT1_F_RSS;
		vmxnet3_reinit_rss_shared_data(sc);
	}

	vmxnet3_write_bar1(sc, VMXNET3_BAR1_DSL, sc->vmx_ds_dma.idi_paddr);
	vmxnet3_write_bar1(sc, VMXNET3_BAR1_DSH,
	    (uint64_t) sc->vmx_ds_dma.idi_paddr >> 32);
}

static int
vmxnet3_alloc_data(struct vmxnet3_softc *sc)
{
	int error;

	error = vmxnet3_alloc_shared_data(sc);
	if (error)
		return (error);

	error = vmxnet3_alloc_mcast_table(sc);
	if (error)
		return (error);

	vmxnet3_init_shared_data(sc);

	return (0);
}

static void
vmxnet3_free_data(struct vmxnet3_softc *sc)
{

	vmxnet3_free_mcast_table(sc);
	vmxnet3_free_shared_data(sc);
}

static void
vmxnet3_evintr(struct vmxnet3_softc *sc)
{
	device_t dev;
	struct vmxnet3_txq_shared *ts;
	struct vmxnet3_rxq_shared *rs;
	uint32_t event;

	dev = sc->vmx_dev;

	/* Clear events. */
	event = sc->vmx_ds->event;
	vmxnet3_write_bar1(sc, VMXNET3_BAR1_EVENT, event);

	if (event & VMXNET3_EVENT_LINK)
		vmxnet3_link_status(sc);

	if (event & (VMXNET3_EVENT_TQERROR | VMXNET3_EVENT_RQERROR)) {
		vmxnet3_read_cmd(sc, VMXNET3_CMD_GET_STATUS);
		ts = sc->vmx_txq[0].vxtxq_ts;
		if (ts->stopped != 0)
			device_printf(dev, "Tx queue error %#x\n", ts->error);
		rs = sc->vmx_rxq[0].vxrxq_rs;
		if (rs->stopped != 0)
			device_printf(dev, "Rx queue error %#x\n", rs->error);

		/* XXX - rely on liflib watchdog to reset us? */
		device_printf(dev, "Rx/Tx queue error event ... "
		    "waiting for iflib watchdog reset\n");
	}

	if (event & VMXNET3_EVENT_DIC)
		device_printf(dev, "device implementation change event\n");
	if (event & VMXNET3_EVENT_DEBUG)
		device_printf(dev, "debug event\n");
}

static int
vmxnet3_isc_txd_encap(void *vsc, if_pkt_info_t pi)
{
	struct vmxnet3_softc *sc;
	struct vmxnet3_txqueue *txq;
	struct vmxnet3_txring *txr;
	struct vmxnet3_txdesc *txd, *sop;
	bus_dma_segment_t *segs;
	int nsegs;
	int pidx;
	int hdrlen;
	int i;
	int gen;

	sc = vsc;
	txq = &sc->vmx_txq[pi->ipi_qsidx];
	txr = &txq->vxtxq_cmd_ring;
	segs = pi->ipi_segs;
	nsegs = pi->ipi_nsegs;
	pidx = pi->ipi_pidx;

	KASSERT(nsegs <= VMXNET3_TX_MAXSEGS,
	    ("%s: packet with too many segments %d", __func__, nsegs));

	sop = &txr->vxtxr_txd[pidx];
	gen = txr->vxtxr_gen ^ 1;	/* Owned by cpu (yet) */

	for (i = 0; i < nsegs; i++) {
		txd = &txr->vxtxr_txd[pidx];

		txd->addr = segs[i].ds_addr;
		txd->len = segs[i].ds_len;
		txd->gen = gen;
		txd->dtype = 0;
		txd->offload_mode = VMXNET3_OM_NONE;
		txd->offload_pos = 0;
		txd->hlen = 0;
		txd->eop = 0;
		txd->compreq = 0;
		txd->vtag_mode = 0;
		txd->vtag = 0;

		if (++pidx == txr->vxtxr_ndesc) {
			pidx = 0;
			txr->vxtxr_gen ^= 1;
		}
		gen = txr->vxtxr_gen;
	}
	txd->eop = 1;
	txd->compreq = !!(pi->ipi_flags & IPI_TX_INTR);
	pi->ipi_new_pidx = pidx;

	/*
	 * VLAN
	 */
	if (pi->ipi_mflags & M_VLANTAG) {
		sop->vtag_mode = 1;
		sop->vtag = pi->ipi_vtag;
	}

	/*
	 * TSO and checksum offloads
	 */
	hdrlen = pi->ipi_ehdrlen + pi->ipi_ip_hlen;
	if (pi->ipi_csum_flags & CSUM_TSO) {
		sop->offload_mode = VMXNET3_OM_TSO;
		sop->hlen = hdrlen;
		sop->offload_pos = pi->ipi_tso_segsz;
	} else if (pi->ipi_csum_flags & (VMXNET3_CSUM_OFFLOAD |
	    VMXNET3_CSUM_OFFLOAD_IPV6)) {
		sop->offload_mode = VMXNET3_OM_CSUM;
		sop->hlen = hdrlen;
		sop->offload_pos = hdrlen +
		    ((pi->ipi_ipproto == IPPROTO_TCP) ?
			offsetof(struct tcphdr, th_sum) :
			offsetof(struct udphdr, uh_sum));
	}

	/* Finally, change the ownership. */
	vmxnet3_barrier(sc, VMXNET3_BARRIER_WR);
	sop->gen ^= 1;

	return (0);
}

static void
vmxnet3_isc_txd_flush(void *vsc, uint16_t txqid, qidx_t pidx)
{
	struct vmxnet3_softc *sc;
	struct vmxnet3_txqueue *txq;

	sc = vsc;
	txq = &sc->vmx_txq[txqid];

	/*
	 * pidx is what we last set ipi_new_pidx to in
	 * vmxnet3_isc_txd_encap()
	 */

	/*
	 * Avoid expensive register updates if the flush request is
	 * redundant.
	 */
	if (txq->vxtxq_last_flush == pidx)
		return;
	txq->vxtxq_last_flush = pidx;
	vmxnet3_write_bar0(sc, VMXNET3_BAR0_TXH(txq->vxtxq_id), pidx);
}

static int
vmxnet3_isc_txd_credits_update(void *vsc, uint16_t txqid, bool clear)
{
	struct vmxnet3_softc *sc;
	struct vmxnet3_txqueue *txq;
	struct vmxnet3_comp_ring *txc;
	struct vmxnet3_txcompdesc *txcd;
	struct vmxnet3_txring *txr;
	int processed;
	
	sc = vsc;
	txq = &sc->vmx_txq[txqid];
	txc = &txq->vxtxq_comp_ring;
	txr = &txq->vxtxq_cmd_ring;

	/*
	 * If clear is true, we need to report the number of TX command ring
	 * descriptors that have been processed by the device.  If clear is
	 * false, we just need to report whether or not at least one TX
	 * command ring descriptor has been processed by the device.
	 */
	processed = 0;
	for (;;) {
		txcd = &txc->vxcr_u.txcd[txc->vxcr_next];
		if (txcd->gen != txc->vxcr_gen)
			break;
		else if (!clear)
			return (1);
		vmxnet3_barrier(sc, VMXNET3_BARRIER_RD);

		if (++txc->vxcr_next == txc->vxcr_ndesc) {
			txc->vxcr_next = 0;
			txc->vxcr_gen ^= 1;
		}

		if (txcd->eop_idx < txr->vxtxr_next)
			processed += txr->vxtxr_ndesc -
			    (txr->vxtxr_next - txcd->eop_idx) + 1;
		else
			processed += txcd->eop_idx - txr->vxtxr_next + 1;
		txr->vxtxr_next = (txcd->eop_idx + 1) % txr->vxtxr_ndesc;
	}

	return (processed);
}

static int
vmxnet3_isc_rxd_available(void *vsc, uint16_t rxqid, qidx_t idx, qidx_t budget)
{
	struct vmxnet3_softc *sc;
	struct vmxnet3_rxqueue *rxq;
	struct vmxnet3_comp_ring *rxc;
	struct vmxnet3_rxcompdesc *rxcd;
	int avail;
	int completed_gen;
#ifdef INVARIANTS
	int expect_sop = 1;
#endif	
	sc = vsc;
	rxq = &sc->vmx_rxq[rxqid];
	rxc = &rxq->vxrxq_comp_ring;

	avail = 0;
	completed_gen = rxc->vxcr_gen;
	for (;;) {
		rxcd = &rxc->vxcr_u.rxcd[idx];
		if (rxcd->gen != completed_gen)
			break;
		vmxnet3_barrier(sc, VMXNET3_BARRIER_RD);

#ifdef INVARIANTS
		if (expect_sop)
			KASSERT(rxcd->sop, ("%s: expected sop", __func__));
		else
			KASSERT(!rxcd->sop, ("%s: unexpected sop", __func__));
		expect_sop = rxcd->eop;
#endif
		if (rxcd->eop && (rxcd->len != 0))
			avail++;
		if (avail > budget)
			break;
		if (++idx == rxc->vxcr_ndesc) {
			idx = 0;
			completed_gen ^= 1;
		}
	}
	
	return (avail);
}

static int
vmxnet3_isc_rxd_pkt_get(void *vsc, if_rxd_info_t ri)
{
	struct vmxnet3_softc *sc;
	if_softc_ctx_t scctx;
	struct vmxnet3_rxqueue *rxq;
	struct vmxnet3_comp_ring *rxc;
	struct vmxnet3_rxcompdesc *rxcd;
	struct vmxnet3_rxring *rxr;
	struct vmxnet3_rxdesc *rxd;
	if_rxd_frag_t frag;
	int cqidx;
	uint16_t total_len;
	uint8_t nfrags;
	uint8_t flid;

	sc = vsc;
	scctx = sc->vmx_scctx;
	rxq = &sc->vmx_rxq[ri->iri_qsidx];
	rxc = &rxq->vxrxq_comp_ring;

	/*
	 * Get a single packet starting at the given index in the completion
	 * queue.  That we have been called indicates that
	 * vmxnet3_isc_rxd_available() has already verified that either
	 * there is a complete packet available starting at the given index,
	 * or there are one or more zero length packets starting at the
	 * given index followed by a complete packet, so no verification of
	 * ownership of the descriptors (and no associated read barrier) is
	 * required here.
	 */
	cqidx = ri->iri_cidx;
	rxcd = &rxc->vxcr_u.rxcd[cqidx];
	while (rxcd->len == 0) {
		KASSERT(rxcd->sop && rxcd->eop,
		    ("%s: zero-length packet without both sop and eop set",
			__func__));
		if (++cqidx == rxc->vxcr_ndesc) {
			cqidx = 0;
			rxc->vxcr_gen ^= 1;
		}
		rxcd = &rxc->vxcr_u.rxcd[cqidx];
	}
	KASSERT(rxcd->sop, ("%s: expected sop", __func__));

	/*
	 * RSS and flow ID
	 */
	ri->iri_flowid = rxcd->rss_hash;
	switch (rxcd->rss_type) {
	case VMXNET3_RCD_RSS_TYPE_NONE:
		ri->iri_flowid = ri->iri_qsidx;
		ri->iri_rsstype = M_HASHTYPE_NONE;
		break;
	case VMXNET3_RCD_RSS_TYPE_IPV4:
		ri->iri_rsstype = M_HASHTYPE_RSS_IPV4;
		break;
	case VMXNET3_RCD_RSS_TYPE_TCPIPV4:
		ri->iri_rsstype = M_HASHTYPE_RSS_TCP_IPV4;
		break;
	case VMXNET3_RCD_RSS_TYPE_IPV6:
		ri->iri_rsstype = M_HASHTYPE_RSS_IPV6;
		break;
	case VMXNET3_RCD_RSS_TYPE_TCPIPV6:
		ri->iri_rsstype = M_HASHTYPE_RSS_TCP_IPV6;
		break;
	default:
		ri->iri_rsstype = M_HASHTYPE_OPAQUE_HASH;
		break;
	}

	/* VLAN */
	if (rxcd->vlan) {
		ri->iri_flags |= M_VLANTAG;
		ri->iri_vtag = rxcd->vtag;
	}

	/* Checksum offload */
	if (!rxcd->no_csum) {
		uint32_t csum_flags = 0;

		if (rxcd->ipv4) {
			csum_flags |= CSUM_IP_CHECKED;
			if (rxcd->ipcsum_ok)
				csum_flags |= CSUM_IP_VALID;
		}
		if (!rxcd->fragment && (rxcd->tcp || rxcd->udp)) {
			csum_flags |= CSUM_L4_CALC;
			if (rxcd->csum_ok) {
				csum_flags |= CSUM_L4_VALID;
				ri->iri_csum_data = 0xffff;
			}
		}
		ri->iri_csum_flags = csum_flags;
	}

	/*
	 * The queue numbering scheme used for rxcd->qid is as follows:
	 *  - All of the command ring 0s are numbered [0, nrxqsets - 1]
	 *  - All of the command ring 1s are numbered [nrxqsets, 2*nrxqsets - 1]
	 *
	 * Thus, rxcd->qid less than nrxqsets indicates command ring (and
	 * flid) 0, and rxcd->qid greater than or equal to nrxqsets
	 * indicates command ring (and flid) 1.
	 */
	nfrags = 0;
	total_len = 0;
	do {
		rxcd = &rxc->vxcr_u.rxcd[cqidx];
		KASSERT(rxcd->gen == rxc->vxcr_gen,
		    ("%s: generation mismatch", __func__));
		flid = (rxcd->qid >= scctx->isc_nrxqsets) ? 1 : 0;
		rxr = &rxq->vxrxq_cmd_ring[flid];
		rxd = &rxr->vxrxr_rxd[rxcd->rxd_idx];

		frag = &ri->iri_frags[nfrags];
		frag->irf_flid = flid;
		frag->irf_idx = rxcd->rxd_idx;
		frag->irf_len = rxcd->len;
		total_len += rxcd->len;
		nfrags++;
		if (++cqidx == rxc->vxcr_ndesc) {
			cqidx = 0;
			rxc->vxcr_gen ^= 1;
		}
	} while (!rxcd->eop);

	ri->iri_cidx = cqidx;
	ri->iri_nfrags = nfrags;
	ri->iri_len = total_len;

	return (0);
}

static void
vmxnet3_isc_rxd_refill(void *vsc, if_rxd_update_t iru)
{
	struct vmxnet3_softc *sc;
	struct vmxnet3_rxqueue *rxq;
	struct vmxnet3_rxring *rxr;
	struct vmxnet3_rxdesc *rxd;
	uint64_t *paddrs;
	int count;
	int len;
	int pidx;
	int i;
	uint8_t flid;
	uint8_t btype;

	count = iru->iru_count;
	len = iru->iru_buf_size;
	pidx = iru->iru_pidx;
	flid = iru->iru_flidx;
	paddrs = iru->iru_paddrs;

	sc = vsc;
	rxq = &sc->vmx_rxq[iru->iru_qsidx];
	rxr = &rxq->vxrxq_cmd_ring[flid];
	rxd = rxr->vxrxr_rxd;

	/*
	 * Command ring 0 is filled with BTYPE_HEAD descriptors, and
	 * command ring 1 is filled with BTYPE_BODY descriptors.
	 */
	btype = (flid == 0) ? VMXNET3_BTYPE_HEAD : VMXNET3_BTYPE_BODY;
	for (i = 0; i < count; i++) {
		rxd[pidx].addr = paddrs[i];
		rxd[pidx].len = len;
		rxd[pidx].btype = btype;
		rxd[pidx].gen = rxr->vxrxr_gen;

		if (++pidx == rxr->vxrxr_ndesc) {
			pidx = 0;
			rxr->vxrxr_gen ^= 1;
		}
	}
}

static void
vmxnet3_isc_rxd_flush(void *vsc, uint16_t rxqid, uint8_t flid, qidx_t pidx)
{
	struct vmxnet3_softc *sc;
	struct vmxnet3_rxqueue *rxq;
	struct vmxnet3_rxring *rxr;
	bus_size_t r;
	
	sc = vsc;
	rxq = &sc->vmx_rxq[rxqid];
	rxr = &rxq->vxrxq_cmd_ring[flid];

	if (flid == 0)
		r = VMXNET3_BAR0_RXH1(rxqid);
	else
		r = VMXNET3_BAR0_RXH2(rxqid);

	/*
	 * pidx is the index of the last descriptor with a buffer the device
	 * can use, and the device needs to be told which index is one past
	 * that.
	 */
	if (++pidx == rxr->vxrxr_ndesc)
		pidx = 0;
	vmxnet3_write_bar0(sc, r, pidx);
}

static int
vmxnet3_legacy_intr(void *xsc)
{
	struct vmxnet3_softc *sc;
	if_softc_ctx_t scctx;
	if_ctx_t ctx;
	
	sc = xsc;
	scctx = sc->vmx_scctx;
	ctx = sc->vmx_ctx;

	/*
	 * When there is only a single interrupt configured, this routine
	 * runs in fast interrupt context, following which the rxq 0 task
	 * will be enqueued.
	 */
	if (scctx->isc_intr == IFLIB_INTR_LEGACY) {
		if (vmxnet3_read_bar1(sc, VMXNET3_BAR1_INTR) == 0)
			return (FILTER_HANDLED);
	}
	if (sc->vmx_intr_mask_mode == VMXNET3_IMM_ACTIVE)
		vmxnet3_intr_disable_all(ctx);

	if (sc->vmx_ds->event != 0)
		iflib_admin_intr_deferred(ctx);

	/*
	 * XXX - When there is both rxq and event activity, do we care
	 * whether the rxq 0 task or the admin task re-enables the interrupt
	 * first?
	 */
	return (FILTER_SCHEDULE_THREAD);
}

static int
vmxnet3_rxq_intr(void *vrxq)
{
	struct vmxnet3_softc *sc;
	struct vmxnet3_rxqueue *rxq;

	rxq = vrxq;
	sc = rxq->vxrxq_sc;

	if (sc->vmx_intr_mask_mode == VMXNET3_IMM_ACTIVE)
		vmxnet3_disable_intr(sc, rxq->vxrxq_intr_idx);

	return (FILTER_SCHEDULE_THREAD);
}

static int
vmxnet3_event_intr(void *vsc)
{
	struct vmxnet3_softc *sc;

	sc = vsc;

	if (sc->vmx_intr_mask_mode == VMXNET3_IMM_ACTIVE)
		vmxnet3_disable_intr(sc, sc->vmx_event_intr_idx);

	/*
	 * The work will be done via vmxnet3_update_admin_status(), and the
	 * interrupt will be re-enabled in vmxnet3_link_intr_enable().
	 *
	 * The interrupt will be re-enabled by vmxnet3_link_intr_enable().
	 */
	return (FILTER_SCHEDULE_THREAD);
}

static void
vmxnet3_stop(if_ctx_t ctx)
{
	struct vmxnet3_softc *sc;

	sc = iflib_get_softc(ctx);

	sc->vmx_link_active = 0;
	vmxnet3_write_cmd(sc, VMXNET3_CMD_DISABLE);
	vmxnet3_write_cmd(sc, VMXNET3_CMD_RESET);
}

static void
vmxnet3_txinit(struct vmxnet3_softc *sc, struct vmxnet3_txqueue *txq)
{
	struct vmxnet3_txring *txr;
	struct vmxnet3_comp_ring *txc;

	txq->vxtxq_last_flush = -1;
	
	txr = &txq->vxtxq_cmd_ring;
	txr->vxtxr_next = 0;
	txr->vxtxr_gen = VMXNET3_INIT_GEN;
	/*
	 * iflib has zeroed out the descriptor array during the prior attach
	 * or stop
	 */

	txc = &txq->vxtxq_comp_ring;
	txc->vxcr_next = 0;
	txc->vxcr_gen = VMXNET3_INIT_GEN;
	/*
	 * iflib has zeroed out the descriptor array during the prior attach
	 * or stop
	 */
}

static void
vmxnet3_rxinit(struct vmxnet3_softc *sc, struct vmxnet3_rxqueue *rxq)
{
	struct vmxnet3_rxring *rxr;
	struct vmxnet3_comp_ring *rxc;
	int i;

	/*
	 * The descriptors will be populated with buffers during a
	 * subsequent invocation of vmxnet3_isc_rxd_refill()
	 */
	for (i = 0; i < sc->vmx_sctx->isc_nrxqs - 1; i++) {
		rxr = &rxq->vxrxq_cmd_ring[i];
		rxr->vxrxr_gen = VMXNET3_INIT_GEN;
		/*
		 * iflib has zeroed out the descriptor array during the
		 * prior attach or stop
		 */
	}

	for (/**/; i < VMXNET3_RXRINGS_PERQ; i++) {
		rxr = &rxq->vxrxq_cmd_ring[i];
		rxr->vxrxr_gen = 0;
		bzero(rxr->vxrxr_rxd,
		    rxr->vxrxr_ndesc * sizeof(struct vmxnet3_rxdesc));
	}

	rxc = &rxq->vxrxq_comp_ring;
	rxc->vxcr_next = 0;
	rxc->vxcr_gen = VMXNET3_INIT_GEN;
	/*
	 * iflib has zeroed out the descriptor array during the prior attach
	 * or stop
	 */
}

static void
vmxnet3_reinit_queues(struct vmxnet3_softc *sc)
{
	if_softc_ctx_t scctx;
	int q;

	scctx = sc->vmx_scctx;

	for (q = 0; q < scctx->isc_ntxqsets; q++)
		vmxnet3_txinit(sc, &sc->vmx_txq[q]);

	for (q = 0; q < scctx->isc_nrxqsets; q++)
		vmxnet3_rxinit(sc, &sc->vmx_rxq[q]);
}

static int
vmxnet3_enable_device(struct vmxnet3_softc *sc)
{
	if_softc_ctx_t scctx;
	int q;

	scctx = sc->vmx_scctx;

	if (vmxnet3_read_cmd(sc, VMXNET3_CMD_ENABLE) != 0) {
		device_printf(sc->vmx_dev, "device enable command failed!\n");
		return (1);
	}

	/* Reset the Rx queue heads. */
	for (q = 0; q < scctx->isc_nrxqsets; q++) {
		vmxnet3_write_bar0(sc, VMXNET3_BAR0_RXH1(q), 0);
		vmxnet3_write_bar0(sc, VMXNET3_BAR0_RXH2(q), 0);
	}

	return (0);
}

static void
vmxnet3_reinit_rxfilters(struct vmxnet3_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->vmx_ifp;

	vmxnet3_set_rxfilter(sc, if_getflags(ifp));

	if (ifp->if_capenable & IFCAP_VLAN_HWFILTER)
		bcopy(sc->vmx_vlan_filter, sc->vmx_ds->vlan_filter,
		    sizeof(sc->vmx_ds->vlan_filter));
	else
		bzero(sc->vmx_ds->vlan_filter,
		    sizeof(sc->vmx_ds->vlan_filter));
	vmxnet3_write_cmd(sc, VMXNET3_CMD_VLAN_FILTER);
}

static void
vmxnet3_init(if_ctx_t ctx)
{
	struct vmxnet3_softc *sc;
	if_softc_ctx_t scctx;
	
	sc = iflib_get_softc(ctx);
	scctx = sc->vmx_scctx;

	scctx->isc_max_frame_size = if_getmtu(iflib_get_ifp(ctx)) +
	    ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN + ETHER_CRC_LEN;

	/* Use the current MAC address. */
	bcopy(IF_LLADDR(sc->vmx_ifp), sc->vmx_lladdr, ETHER_ADDR_LEN);
	vmxnet3_set_lladdr(sc);

	vmxnet3_reinit_shared_data(sc);
	vmxnet3_reinit_queues(sc);

	vmxnet3_enable_device(sc);

	vmxnet3_reinit_rxfilters(sc);
	vmxnet3_link_status(sc);
}

static void
vmxnet3_multi_set(if_ctx_t ctx)
{

	vmxnet3_set_rxfilter(iflib_get_softc(ctx),
	    if_getflags(iflib_get_ifp(ctx)));
}

static int
vmxnet3_mtu_set(if_ctx_t ctx, uint32_t mtu)
{

	if (mtu > VMXNET3_TX_MAXSIZE - (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN +
		ETHER_CRC_LEN))
		return (EINVAL);

	return (0);
}

static void
vmxnet3_media_status(if_ctx_t ctx, struct ifmediareq * ifmr)
{
	struct vmxnet3_softc *sc;

	sc = iflib_get_softc(ctx);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (vmxnet3_link_is_up(sc) != 0) {
		ifmr->ifm_status |= IFM_ACTIVE;
		ifmr->ifm_active |= IFM_AUTO;
	} else
		ifmr->ifm_active |= IFM_NONE;
}

static int
vmxnet3_media_change(if_ctx_t ctx)
{

	/* Ignore. */
	return (0);
}

static int
vmxnet3_promisc_set(if_ctx_t ctx, int flags)
{

	vmxnet3_set_rxfilter(iflib_get_softc(ctx), flags);

	return (0);
}

static uint64_t
vmxnet3_get_counter(if_ctx_t ctx, ift_counter cnt)
{
	if_t ifp = iflib_get_ifp(ctx);

	if (cnt < IFCOUNTERS)
		return if_get_counter_default(ifp, cnt);

	return (0);
}

static void
vmxnet3_update_admin_status(if_ctx_t ctx)
{
	struct vmxnet3_softc *sc;

	sc = iflib_get_softc(ctx);
	if (sc->vmx_ds->event != 0)
		vmxnet3_evintr(sc);

	vmxnet3_refresh_host_stats(sc);
}

static void
vmxnet3_txq_timer(if_ctx_t ctx, uint16_t qid)
{
	/* Host stats refresh is global, so just trigger it on txq 0 */
	if (qid == 0)
		vmxnet3_refresh_host_stats(iflib_get_softc(ctx));
}

static void
vmxnet3_update_vlan_filter(struct vmxnet3_softc *sc, int add, uint16_t tag)
{
	int idx, bit;

	if (tag == 0 || tag > 4095)
		return;

	idx = (tag >> 5) & 0x7F;
	bit = tag & 0x1F;

	/* Update our private VLAN bitvector. */
	if (add)
		sc->vmx_vlan_filter[idx] |= (1 << bit);
	else
		sc->vmx_vlan_filter[idx] &= ~(1 << bit);
}

static void
vmxnet3_vlan_register(if_ctx_t ctx, uint16_t tag)
{

	vmxnet3_update_vlan_filter(iflib_get_softc(ctx), 1, tag);
}

static void
vmxnet3_vlan_unregister(if_ctx_t ctx, uint16_t tag)
{

	vmxnet3_update_vlan_filter(iflib_get_softc(ctx), 0, tag);
}

static void
vmxnet3_set_rxfilter(struct vmxnet3_softc *sc, int flags)
{
	struct ifnet *ifp;
	struct vmxnet3_driver_shared *ds;
	struct ifmultiaddr *ifma;
	u_int mode;

	ifp = sc->vmx_ifp;
	ds = sc->vmx_ds;

	mode = VMXNET3_RXMODE_UCAST | VMXNET3_RXMODE_BCAST;
	if (flags & IFF_PROMISC)
		mode |= VMXNET3_RXMODE_PROMISC;
	if (flags & IFF_ALLMULTI)
		mode |= VMXNET3_RXMODE_ALLMULTI;
	else {
		int cnt = 0, overflow = 0;

		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			else if (cnt == VMXNET3_MULTICAST_MAX) {
				overflow = 1;
				break;
			}

			bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
			   &sc->vmx_mcast[cnt*ETHER_ADDR_LEN], ETHER_ADDR_LEN);
			cnt++;
		}
		if_maddr_runlock(ifp);

		if (overflow != 0) {
			cnt = 0;
			mode |= VMXNET3_RXMODE_ALLMULTI;
		} else if (cnt > 0)
			mode |= VMXNET3_RXMODE_MCAST;
		ds->mcast_tablelen = cnt * ETHER_ADDR_LEN;
	}

	ds->rxmode = mode;

	vmxnet3_write_cmd(sc, VMXNET3_CMD_SET_FILTER);
	vmxnet3_write_cmd(sc, VMXNET3_CMD_SET_RXMODE);
}

static void
vmxnet3_refresh_host_stats(struct vmxnet3_softc *sc)
{

	vmxnet3_write_cmd(sc, VMXNET3_CMD_GET_STATS);
}

static int
vmxnet3_link_is_up(struct vmxnet3_softc *sc)
{
	uint32_t status;

	status = vmxnet3_read_cmd(sc, VMXNET3_CMD_GET_LINK);
	return !!(status & 0x1);
}

static void
vmxnet3_link_status(struct vmxnet3_softc *sc)
{
	if_ctx_t ctx;
	uint64_t speed;
	int link;

	ctx = sc->vmx_ctx;
	link = vmxnet3_link_is_up(sc);
	speed = IF_Gbps(10);
	
	if (link != 0 && sc->vmx_link_active == 0) {
		sc->vmx_link_active = 1;
		iflib_link_state_change(ctx, LINK_STATE_UP, speed);
	} else if (link == 0 && sc->vmx_link_active != 0) {
		sc->vmx_link_active = 0;
		iflib_link_state_change(ctx, LINK_STATE_DOWN, speed);
	}
}

static void
vmxnet3_set_lladdr(struct vmxnet3_softc *sc)
{
	uint32_t ml, mh;

	ml  = sc->vmx_lladdr[0];
	ml |= sc->vmx_lladdr[1] << 8;
	ml |= sc->vmx_lladdr[2] << 16;
	ml |= sc->vmx_lladdr[3] << 24;
	vmxnet3_write_bar1(sc, VMXNET3_BAR1_MACL, ml);

	mh  = sc->vmx_lladdr[4];
	mh |= sc->vmx_lladdr[5] << 8;
	vmxnet3_write_bar1(sc, VMXNET3_BAR1_MACH, mh);
}

static void
vmxnet3_get_lladdr(struct vmxnet3_softc *sc)
{
	uint32_t ml, mh;

	ml = vmxnet3_read_cmd(sc, VMXNET3_CMD_GET_MACL);
	mh = vmxnet3_read_cmd(sc, VMXNET3_CMD_GET_MACH);

	sc->vmx_lladdr[0] = ml;
	sc->vmx_lladdr[1] = ml >> 8;
	sc->vmx_lladdr[2] = ml >> 16;
	sc->vmx_lladdr[3] = ml >> 24;
	sc->vmx_lladdr[4] = mh;
	sc->vmx_lladdr[5] = mh >> 8;
}

static void
vmxnet3_setup_txq_sysctl(struct vmxnet3_txqueue *txq,
    struct sysctl_ctx_list *ctx, struct sysctl_oid_list *child)
{
	struct sysctl_oid *node, *txsnode;
	struct sysctl_oid_list *list, *txslist;
	struct UPT1_TxStats *txstats;
	char namebuf[16];

	txstats = &txq->vxtxq_ts->stats;

	snprintf(namebuf, sizeof(namebuf), "txq%d", txq->vxtxq_id);
	node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, namebuf, CTLFLAG_RD,
	    NULL, "Transmit Queue");
	txq->vxtxq_sysctl = list = SYSCTL_CHILDREN(node);

	/*
	 * Add statistics reported by the host. These are updated by the
	 * iflib txq timer on txq 0.
	 */
	txsnode = SYSCTL_ADD_NODE(ctx, list, OID_AUTO, "hstats", CTLFLAG_RD,
	    NULL, "Host Statistics");
	txslist = SYSCTL_CHILDREN(txsnode);
	SYSCTL_ADD_UQUAD(ctx, txslist, OID_AUTO, "tso_packets", CTLFLAG_RD,
	    &txstats->TSO_packets, "TSO packets");
	SYSCTL_ADD_UQUAD(ctx, txslist, OID_AUTO, "tso_bytes", CTLFLAG_RD,
	    &txstats->TSO_bytes, "TSO bytes");
	SYSCTL_ADD_UQUAD(ctx, txslist, OID_AUTO, "ucast_packets", CTLFLAG_RD,
	    &txstats->ucast_packets, "Unicast packets");
	SYSCTL_ADD_UQUAD(ctx, txslist, OID_AUTO, "unicast_bytes", CTLFLAG_RD,
	    &txstats->ucast_bytes, "Unicast bytes");
	SYSCTL_ADD_UQUAD(ctx, txslist, OID_AUTO, "mcast_packets", CTLFLAG_RD,
	    &txstats->mcast_packets, "Multicast packets");
	SYSCTL_ADD_UQUAD(ctx, txslist, OID_AUTO, "mcast_bytes", CTLFLAG_RD,
	    &txstats->mcast_bytes, "Multicast bytes");
	SYSCTL_ADD_UQUAD(ctx, txslist, OID_AUTO, "error", CTLFLAG_RD,
	    &txstats->error, "Errors");
	SYSCTL_ADD_UQUAD(ctx, txslist, OID_AUTO, "discard", CTLFLAG_RD,
	    &txstats->discard, "Discards");
}

static void
vmxnet3_setup_rxq_sysctl(struct vmxnet3_rxqueue *rxq,
    struct sysctl_ctx_list *ctx, struct sysctl_oid_list *child)
{
	struct sysctl_oid *node, *rxsnode;
	struct sysctl_oid_list *list, *rxslist;
	struct UPT1_RxStats *rxstats;
	char namebuf[16];

	rxstats = &rxq->vxrxq_rs->stats;

	snprintf(namebuf, sizeof(namebuf), "rxq%d", rxq->vxrxq_id);
	node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, namebuf, CTLFLAG_RD,
	    NULL, "Receive Queue");
	rxq->vxrxq_sysctl = list = SYSCTL_CHILDREN(node);

	/*
	 * Add statistics reported by the host. These are updated by the
	 * iflib txq timer on txq 0.
	 */
	rxsnode = SYSCTL_ADD_NODE(ctx, list, OID_AUTO, "hstats", CTLFLAG_RD,
	    NULL, "Host Statistics");
	rxslist = SYSCTL_CHILDREN(rxsnode);
	SYSCTL_ADD_UQUAD(ctx, rxslist, OID_AUTO, "lro_packets", CTLFLAG_RD,
	    &rxstats->LRO_packets, "LRO packets");
	SYSCTL_ADD_UQUAD(ctx, rxslist, OID_AUTO, "lro_bytes", CTLFLAG_RD,
	    &rxstats->LRO_bytes, "LRO bytes");
	SYSCTL_ADD_UQUAD(ctx, rxslist, OID_AUTO, "ucast_packets", CTLFLAG_RD,
	    &rxstats->ucast_packets, "Unicast packets");
	SYSCTL_ADD_UQUAD(ctx, rxslist, OID_AUTO, "unicast_bytes", CTLFLAG_RD,
	    &rxstats->ucast_bytes, "Unicast bytes");
	SYSCTL_ADD_UQUAD(ctx, rxslist, OID_AUTO, "mcast_packets", CTLFLAG_RD,
	    &rxstats->mcast_packets, "Multicast packets");
	SYSCTL_ADD_UQUAD(ctx, rxslist, OID_AUTO, "mcast_bytes", CTLFLAG_RD,
	    &rxstats->mcast_bytes, "Multicast bytes");
	SYSCTL_ADD_UQUAD(ctx, rxslist, OID_AUTO, "bcast_packets", CTLFLAG_RD,
	    &rxstats->bcast_packets, "Broadcast packets");
	SYSCTL_ADD_UQUAD(ctx, rxslist, OID_AUTO, "bcast_bytes", CTLFLAG_RD,
	    &rxstats->bcast_bytes, "Broadcast bytes");
	SYSCTL_ADD_UQUAD(ctx, rxslist, OID_AUTO, "nobuffer", CTLFLAG_RD,
	    &rxstats->nobuffer, "No buffer");
	SYSCTL_ADD_UQUAD(ctx, rxslist, OID_AUTO, "error", CTLFLAG_RD,
	    &rxstats->error, "Errors");
}

static void
vmxnet3_setup_debug_sysctl(struct vmxnet3_softc *sc,
    struct sysctl_ctx_list *ctx, struct sysctl_oid_list *child)
{
	if_softc_ctx_t scctx;
	struct sysctl_oid *node;
	struct sysctl_oid_list *list;
	int i;

	scctx = sc->vmx_scctx;
	
	for (i = 0; i < scctx->isc_ntxqsets; i++) {
		struct vmxnet3_txqueue *txq = &sc->vmx_txq[i];

		node = SYSCTL_ADD_NODE(ctx, txq->vxtxq_sysctl, OID_AUTO,
		    "debug", CTLFLAG_RD, NULL, "");
		list = SYSCTL_CHILDREN(node);

		SYSCTL_ADD_UINT(ctx, list, OID_AUTO, "cmd_next", CTLFLAG_RD,
		    &txq->vxtxq_cmd_ring.vxtxr_next, 0, "");
		SYSCTL_ADD_UINT(ctx, list, OID_AUTO, "cmd_ndesc", CTLFLAG_RD,
		    &txq->vxtxq_cmd_ring.vxtxr_ndesc, 0, "");
		SYSCTL_ADD_INT(ctx, list, OID_AUTO, "cmd_gen", CTLFLAG_RD,
		    &txq->vxtxq_cmd_ring.vxtxr_gen, 0, "");
		SYSCTL_ADD_UINT(ctx, list, OID_AUTO, "comp_next", CTLFLAG_RD,
		    &txq->vxtxq_comp_ring.vxcr_next, 0, "");
		SYSCTL_ADD_UINT(ctx, list, OID_AUTO, "comp_ndesc", CTLFLAG_RD,
		    &txq->vxtxq_comp_ring.vxcr_ndesc, 0,"");
		SYSCTL_ADD_INT(ctx, list, OID_AUTO, "comp_gen", CTLFLAG_RD,
		    &txq->vxtxq_comp_ring.vxcr_gen, 0, "");
	}

	for (i = 0; i < scctx->isc_nrxqsets; i++) {
		struct vmxnet3_rxqueue *rxq = &sc->vmx_rxq[i];

		node = SYSCTL_ADD_NODE(ctx, rxq->vxrxq_sysctl, OID_AUTO,
		    "debug", CTLFLAG_RD, NULL, "");
		list = SYSCTL_CHILDREN(node);

		SYSCTL_ADD_UINT(ctx, list, OID_AUTO, "cmd0_ndesc", CTLFLAG_RD,
		    &rxq->vxrxq_cmd_ring[0].vxrxr_ndesc, 0, "");
		SYSCTL_ADD_INT(ctx, list, OID_AUTO, "cmd0_gen", CTLFLAG_RD,
		    &rxq->vxrxq_cmd_ring[0].vxrxr_gen, 0, "");
		SYSCTL_ADD_UINT(ctx, list, OID_AUTO, "cmd1_ndesc", CTLFLAG_RD,
		    &rxq->vxrxq_cmd_ring[1].vxrxr_ndesc, 0, "");
		SYSCTL_ADD_INT(ctx, list, OID_AUTO, "cmd1_gen", CTLFLAG_RD,
		    &rxq->vxrxq_cmd_ring[1].vxrxr_gen, 0, "");
		SYSCTL_ADD_UINT(ctx, list, OID_AUTO, "comp_ndesc", CTLFLAG_RD,
		    &rxq->vxrxq_comp_ring.vxcr_ndesc, 0,"");
		SYSCTL_ADD_INT(ctx, list, OID_AUTO, "comp_gen", CTLFLAG_RD,
		    &rxq->vxrxq_comp_ring.vxcr_gen, 0, "");
	}
}

static void
vmxnet3_setup_queue_sysctl(struct vmxnet3_softc *sc,
    struct sysctl_ctx_list *ctx, struct sysctl_oid_list *child)
{
	if_softc_ctx_t scctx;
	int i;

	scctx = sc->vmx_scctx;
	
	for (i = 0; i < scctx->isc_ntxqsets; i++)
		vmxnet3_setup_txq_sysctl(&sc->vmx_txq[i], ctx, child);
	for (i = 0; i < scctx->isc_nrxqsets; i++)
		vmxnet3_setup_rxq_sysctl(&sc->vmx_rxq[i], ctx, child);

	vmxnet3_setup_debug_sysctl(sc, ctx, child);
}

static void
vmxnet3_setup_sysctl(struct vmxnet3_softc *sc)
{
	device_t dev;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;

	dev = sc->vmx_dev;
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	vmxnet3_setup_queue_sysctl(sc, ctx, child);
}

static void
vmxnet3_write_bar0(struct vmxnet3_softc *sc, bus_size_t r, uint32_t v)
{

	bus_space_write_4(sc->vmx_iot0, sc->vmx_ioh0, r, v);
}

static uint32_t
vmxnet3_read_bar1(struct vmxnet3_softc *sc, bus_size_t r)
{

	return (bus_space_read_4(sc->vmx_iot1, sc->vmx_ioh1, r));
}

static void
vmxnet3_write_bar1(struct vmxnet3_softc *sc, bus_size_t r, uint32_t v)
{

	bus_space_write_4(sc->vmx_iot1, sc->vmx_ioh1, r, v);
}

static void
vmxnet3_write_cmd(struct vmxnet3_softc *sc, uint32_t cmd)
{

	vmxnet3_write_bar1(sc, VMXNET3_BAR1_CMD, cmd);
}

static uint32_t
vmxnet3_read_cmd(struct vmxnet3_softc *sc, uint32_t cmd)
{

	vmxnet3_write_cmd(sc, cmd);
	bus_space_barrier(sc->vmx_iot1, sc->vmx_ioh1, 0, 0,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return (vmxnet3_read_bar1(sc, VMXNET3_BAR1_CMD));
}

static void
vmxnet3_enable_intr(struct vmxnet3_softc *sc, int irq)
{

	vmxnet3_write_bar0(sc, VMXNET3_BAR0_IMASK(irq), 0);
}

static void
vmxnet3_disable_intr(struct vmxnet3_softc *sc, int irq)
{

	vmxnet3_write_bar0(sc, VMXNET3_BAR0_IMASK(irq), 1);
}

static int
vmxnet3_tx_queue_intr_enable(if_ctx_t ctx, uint16_t qid)
{
	/* Not using interrupts for TX */
	return (0);
}

static int
vmxnet3_rx_queue_intr_enable(if_ctx_t ctx, uint16_t qid)
{
	struct vmxnet3_softc *sc;

	sc = iflib_get_softc(ctx);
	vmxnet3_enable_intr(sc, sc->vmx_rxq[qid].vxrxq_intr_idx);
	return (0);
}

static void
vmxnet3_link_intr_enable(if_ctx_t ctx)
{
	struct vmxnet3_softc *sc;

	sc = iflib_get_softc(ctx);
	vmxnet3_enable_intr(sc, sc->vmx_event_intr_idx);
}

static void
vmxnet3_intr_enable_all(if_ctx_t ctx)
{
	struct vmxnet3_softc *sc;
	if_softc_ctx_t scctx;
	int i;

	sc = iflib_get_softc(ctx);
	scctx = sc->vmx_scctx;
	sc->vmx_ds->ictrl &= ~VMXNET3_ICTRL_DISABLE_ALL;
	for (i = 0; i < scctx->isc_vectors; i++)
		vmxnet3_enable_intr(sc, i);
}

static void
vmxnet3_intr_disable_all(if_ctx_t ctx)
{
	struct vmxnet3_softc *sc;
	int i;

	sc = iflib_get_softc(ctx);
	/*
	 * iflib may invoke this routine before vmxnet3_attach_post() has
	 * run, which is before the top level shared data area is
	 * initialized and the device made aware of it.
	 */
	if (sc->vmx_ds != NULL)
		sc->vmx_ds->ictrl |= VMXNET3_ICTRL_DISABLE_ALL;
	for (i = 0; i < VMXNET3_MAX_INTRS; i++)
		vmxnet3_disable_intr(sc, i);
}

/*
 * Since this is a purely paravirtualized device, we do not have
 * to worry about DMA coherency. But at times, we must make sure
 * both the compiler and CPU do not reorder memory operations.
 */
static inline void
vmxnet3_barrier(struct vmxnet3_softc *sc, vmxnet3_barrier_t type)
{

	switch (type) {
	case VMXNET3_BARRIER_RD:
		rmb();
		break;
	case VMXNET3_BARRIER_WR:
		wmb();
		break;
	case VMXNET3_BARRIER_RDWR:
		mb();
		break;
	default:
		panic("%s: bad barrier type %d", __func__, type);
	}
}

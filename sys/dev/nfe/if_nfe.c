/*	$OpenBSD: if_nfe.c,v 1.54 2006/04/07 12:38:12 jsg Exp $	*/

/*-
 * Copyright (c) 2006 Shigeaki Tagashira <shigeaki@se.hiroshima-u.ac.jp>
 * Copyright (c) 2006 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2005, 2006 Jonathan Gray <jsg@openbsd.org>
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

/* Driver for NVIDIA nForce MCP Fast Ethernet and Gigabit Ethernet */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <net/bpf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/nfe/if_nfereg.h>
#include <dev/nfe/if_nfevar.h>

MODULE_DEPEND(nfe, pci, 1, 1, 1);
MODULE_DEPEND(nfe, ether, 1, 1, 1);
MODULE_DEPEND(nfe, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

static int  nfe_probe(device_t);
static int  nfe_attach(device_t);
static int  nfe_detach(device_t);
static int  nfe_suspend(device_t);
static int  nfe_resume(device_t);
static int nfe_shutdown(device_t);
static int  nfe_can_use_msix(struct nfe_softc *);
static int  nfe_detect_msik9(struct nfe_softc *);
static void nfe_power(struct nfe_softc *);
static int  nfe_miibus_readreg(device_t, int, int);
static int  nfe_miibus_writereg(device_t, int, int, int);
static void nfe_miibus_statchg(device_t);
static void nfe_mac_config(struct nfe_softc *, struct mii_data *);
static void nfe_set_intr(struct nfe_softc *);
static __inline void nfe_enable_intr(struct nfe_softc *);
static __inline void nfe_disable_intr(struct nfe_softc *);
static int  nfe_ioctl(if_t, u_long, caddr_t);
static void nfe_alloc_msix(struct nfe_softc *, int);
static int nfe_intr(void *);
static void nfe_int_task(void *, int);
static __inline void nfe_discard_rxbuf(struct nfe_softc *, int);
static __inline void nfe_discard_jrxbuf(struct nfe_softc *, int);
static int nfe_newbuf(struct nfe_softc *, int);
static int nfe_jnewbuf(struct nfe_softc *, int);
static int  nfe_rxeof(struct nfe_softc *, int, int *);
static int  nfe_jrxeof(struct nfe_softc *, int, int *);
static void nfe_txeof(struct nfe_softc *);
static int  nfe_encap(struct nfe_softc *, struct mbuf **);
static void nfe_setmulti(struct nfe_softc *);
static void nfe_start(if_t);
static void nfe_start_locked(if_t);
static void nfe_watchdog(if_t);
static void nfe_init(void *);
static void nfe_init_locked(void *);
static void nfe_stop(if_t);
static int  nfe_alloc_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
static void nfe_alloc_jrx_ring(struct nfe_softc *, struct nfe_jrx_ring *);
static int  nfe_init_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
static int  nfe_init_jrx_ring(struct nfe_softc *, struct nfe_jrx_ring *);
static void nfe_free_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
static void nfe_free_jrx_ring(struct nfe_softc *, struct nfe_jrx_ring *);
static int  nfe_alloc_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
static void nfe_init_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
static void nfe_free_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
static int  nfe_ifmedia_upd(if_t);
static void nfe_ifmedia_sts(if_t, struct ifmediareq *);
static void nfe_tick(void *);
static void nfe_get_macaddr(struct nfe_softc *, uint8_t *);
static void nfe_set_macaddr(struct nfe_softc *, uint8_t *);
static void nfe_dma_map_segs(void *, bus_dma_segment_t *, int, int);

static int sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int sysctl_hw_nfe_proc_limit(SYSCTL_HANDLER_ARGS);
static void nfe_sysctl_node(struct nfe_softc *);
static void nfe_stats_clear(struct nfe_softc *);
static void nfe_stats_update(struct nfe_softc *);
static void nfe_set_linkspeed(struct nfe_softc *);
static void nfe_set_wol(struct nfe_softc *);

#ifdef NFE_DEBUG
static int nfedebug = 0;
#define	DPRINTF(sc, ...)	do {				\
	if (nfedebug)						\
		device_printf((sc)->nfe_dev, __VA_ARGS__);	\
} while (0)
#define	DPRINTFN(sc, n, ...)	do {				\
	if (nfedebug >= (n))					\
		device_printf((sc)->nfe_dev, __VA_ARGS__);	\
} while (0)
#else
#define	DPRINTF(sc, ...)
#define	DPRINTFN(sc, n, ...)
#endif

#define	NFE_LOCK(_sc)		mtx_lock(&(_sc)->nfe_mtx)
#define	NFE_UNLOCK(_sc)		mtx_unlock(&(_sc)->nfe_mtx)
#define	NFE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->nfe_mtx, MA_OWNED)

/* Tunables. */
static int msi_disable = 0;
static int msix_disable = 0;
static int jumbo_disable = 0;
TUNABLE_INT("hw.nfe.msi_disable", &msi_disable);
TUNABLE_INT("hw.nfe.msix_disable", &msix_disable);
TUNABLE_INT("hw.nfe.jumbo_disable", &jumbo_disable);

static device_method_t nfe_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nfe_probe),
	DEVMETHOD(device_attach,	nfe_attach),
	DEVMETHOD(device_detach,	nfe_detach),
	DEVMETHOD(device_suspend,	nfe_suspend),
	DEVMETHOD(device_resume,	nfe_resume),
	DEVMETHOD(device_shutdown,	nfe_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	nfe_miibus_readreg),
	DEVMETHOD(miibus_writereg,	nfe_miibus_writereg),
	DEVMETHOD(miibus_statchg,	nfe_miibus_statchg),

	DEVMETHOD_END
};

static driver_t nfe_driver = {
	"nfe",
	nfe_methods,
	sizeof(struct nfe_softc)
};

static devclass_t nfe_devclass;

DRIVER_MODULE(nfe, pci, nfe_driver, nfe_devclass, 0, 0);
DRIVER_MODULE(miibus, nfe, miibus_driver, miibus_devclass, 0, 0);

static struct nfe_type nfe_devs[] = {
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE_LAN,
	    "NVIDIA nForce MCP Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE2_LAN,
	    "NVIDIA nForce2 MCP2 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE2_400_LAN1,
	    "NVIDIA nForce2 400 MCP4 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE2_400_LAN2,
	    "NVIDIA nForce2 400 MCP5 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN1,
	    "NVIDIA nForce3 MCP3 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_250_LAN,
	    "NVIDIA nForce3 250 MCP6 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN4,
	    "NVIDIA nForce3 MCP7 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE4_LAN1,
	    "NVIDIA nForce4 CK804 MCP8 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE4_LAN2,
	    "NVIDIA nForce4 CK804 MCP9 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP04_LAN1,
	    "NVIDIA nForce MCP04 Networking Adapter"},		/* MCP10 */
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP04_LAN2,
	    "NVIDIA nForce MCP04 Networking Adapter"},		/* MCP11 */
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE430_LAN1,
	    "NVIDIA nForce 430 MCP12 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE430_LAN2,
	    "NVIDIA nForce 430 MCP13 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP55_LAN1,
	    "NVIDIA nForce MCP55 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP55_LAN2,
	    "NVIDIA nForce MCP55 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN1,
	    "NVIDIA nForce MCP61 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN2,
	    "NVIDIA nForce MCP61 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN3,
	    "NVIDIA nForce MCP61 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN4,
	    "NVIDIA nForce MCP61 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN1,
	    "NVIDIA nForce MCP65 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN2,
	    "NVIDIA nForce MCP65 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN3,
	    "NVIDIA nForce MCP65 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN4,
	    "NVIDIA nForce MCP65 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN1,
	    "NVIDIA nForce MCP67 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN2,
	    "NVIDIA nForce MCP67 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN3,
	    "NVIDIA nForce MCP67 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN4,
	    "NVIDIA nForce MCP67 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN1,
	    "NVIDIA nForce MCP73 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN2,
	    "NVIDIA nForce MCP73 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN3,
	    "NVIDIA nForce MCP73 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN4,
	    "NVIDIA nForce MCP73 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN1,
	    "NVIDIA nForce MCP77 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN2,
	    "NVIDIA nForce MCP77 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN3,
	    "NVIDIA nForce MCP77 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN4,
	    "NVIDIA nForce MCP77 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN1,
	    "NVIDIA nForce MCP79 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN2,
	    "NVIDIA nForce MCP79 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN3,
	    "NVIDIA nForce MCP79 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN4,
	    "NVIDIA nForce MCP79 Networking Adapter"},
	{PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP89_LAN,
	    "NVIDIA nForce MCP89 Networking Adapter"},
	{0, 0, NULL}
};


/* Probe for supported hardware ID's */
static int
nfe_probe(device_t dev)
{
	struct nfe_type *t;

	t = nfe_devs;
	/* Check for matching PCI DEVICE ID's */
	while (t->name != NULL) {
		if ((pci_get_vendor(dev) == t->vid_id) &&
		    (pci_get_device(dev) == t->dev_id)) {
			device_set_desc(dev, t->name);
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return (ENXIO);
}

static void
nfe_alloc_msix(struct nfe_softc *sc, int count)
{
	int rid;

	rid = PCIR_BAR(2);
	sc->nfe_msix_res = bus_alloc_resource_any(sc->nfe_dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->nfe_msix_res == NULL) {
		device_printf(sc->nfe_dev,
		    "couldn't allocate MSIX table resource\n");
		return;
	}
	rid = PCIR_BAR(3);
	sc->nfe_msix_pba_res = bus_alloc_resource_any(sc->nfe_dev,
	    SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->nfe_msix_pba_res == NULL) {
		device_printf(sc->nfe_dev,
		    "couldn't allocate MSIX PBA resource\n");
		bus_release_resource(sc->nfe_dev, SYS_RES_MEMORY, PCIR_BAR(2),
		    sc->nfe_msix_res);
		sc->nfe_msix_res = NULL;
		return;
	}

	if (pci_alloc_msix(sc->nfe_dev, &count) == 0) {
		if (count == NFE_MSI_MESSAGES) {
			if (bootverbose)
				device_printf(sc->nfe_dev,
				    "Using %d MSIX messages\n", count);
			sc->nfe_msix = 1;
		} else {
			if (bootverbose)
				device_printf(sc->nfe_dev,
				    "couldn't allocate MSIX\n");
			pci_release_msi(sc->nfe_dev);
			bus_release_resource(sc->nfe_dev, SYS_RES_MEMORY,
			    PCIR_BAR(3), sc->nfe_msix_pba_res);
			bus_release_resource(sc->nfe_dev, SYS_RES_MEMORY,
			    PCIR_BAR(2), sc->nfe_msix_res);
			sc->nfe_msix_pba_res = NULL;
			sc->nfe_msix_res = NULL;
		}
	}
}


static int
nfe_detect_msik9(struct nfe_softc *sc)
{
	static const char *maker = "MSI";
	static const char *product = "K9N6PGM2-V2 (MS-7309)";
	char *m, *p;
	int found;

	found = 0;
	m = kern_getenv("smbios.planar.maker");
	p = kern_getenv("smbios.planar.product");
	if (m != NULL && p != NULL) {
		if (strcmp(m, maker) == 0 && strcmp(p, product) == 0)
			found = 1;
	}
	if (m != NULL)
		freeenv(m);
	if (p != NULL)
		freeenv(p);

	return (found);
}


static int
nfe_attach(device_t dev)
{
	struct nfe_softc *sc;
	if_t ifp;
	bus_addr_t dma_addr_max;
	int error = 0, i, msic, phyloc, reg, rid;

	sc = device_get_softc(dev);
	sc->nfe_dev = dev;

	mtx_init(&sc->nfe_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->nfe_stat_ch, &sc->nfe_mtx, 0);

	pci_enable_busmaster(dev);

	rid = PCIR_BAR(0);
	sc->nfe_res[0] = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->nfe_res[0] == NULL) {
		device_printf(dev, "couldn't map memory resources\n");
		mtx_destroy(&sc->nfe_mtx);
		return (ENXIO);
	}

	if (pci_find_cap(dev, PCIY_EXPRESS, &reg) == 0) {
		uint16_t v, width;

		v = pci_read_config(dev, reg + 0x08, 2);
		/* Change max. read request size to 4096. */
		v &= ~(7 << 12);
		v |= (5 << 12);
		pci_write_config(dev, reg + 0x08, v, 2);

		v = pci_read_config(dev, reg + 0x0c, 2);
		/* link capability */
		v = (v >> 4) & 0x0f;
		width = pci_read_config(dev, reg + 0x12, 2);
		/* negotiated link width */
		width = (width >> 4) & 0x3f;
		if (v != width)
			device_printf(sc->nfe_dev,
			    "warning, negotiated width of link(x%d) != "
			    "max. width of link(x%d)\n", width, v);
	}

	if (nfe_can_use_msix(sc) == 0) {
		device_printf(sc->nfe_dev,
		    "MSI/MSI-X capability black-listed, will use INTx\n"); 
		msix_disable = 1;
		msi_disable = 1;
	}

	/* Allocate interrupt */
	if (msix_disable == 0 || msi_disable == 0) {
		if (msix_disable == 0 &&
		    (msic = pci_msix_count(dev)) == NFE_MSI_MESSAGES)
			nfe_alloc_msix(sc, msic);
		if (msi_disable == 0 && sc->nfe_msix == 0 &&
		    (msic = pci_msi_count(dev)) == NFE_MSI_MESSAGES &&
		    pci_alloc_msi(dev, &msic) == 0) {
			if (msic == NFE_MSI_MESSAGES) {
				if (bootverbose)
					device_printf(dev,
					    "Using %d MSI messages\n", msic);
				sc->nfe_msi = 1;
			} else
				pci_release_msi(dev);
		}
	}

	if (sc->nfe_msix == 0 && sc->nfe_msi == 0) {
		rid = 0;
		sc->nfe_irq[0] = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
		    RF_SHAREABLE | RF_ACTIVE);
		if (sc->nfe_irq[0] == NULL) {
			device_printf(dev, "couldn't allocate IRQ resources\n");
			error = ENXIO;
			goto fail;
		}
	} else {
		for (i = 0, rid = 1; i < NFE_MSI_MESSAGES; i++, rid++) {
			sc->nfe_irq[i] = bus_alloc_resource_any(dev,
			    SYS_RES_IRQ, &rid, RF_ACTIVE);
			if (sc->nfe_irq[i] == NULL) {
				device_printf(dev,
				    "couldn't allocate IRQ resources for "
				    "message %d\n", rid);
				error = ENXIO;
				goto fail;
			}
		}
		/* Map interrupts to vector 0. */
		if (sc->nfe_msix != 0) {
			NFE_WRITE(sc, NFE_MSIX_MAP0, 0);
			NFE_WRITE(sc, NFE_MSIX_MAP1, 0);
		} else if (sc->nfe_msi != 0) {
			NFE_WRITE(sc, NFE_MSI_MAP0, 0);
			NFE_WRITE(sc, NFE_MSI_MAP1, 0);
		}
	}

	/* Set IRQ status/mask register. */
	sc->nfe_irq_status = NFE_IRQ_STATUS;
	sc->nfe_irq_mask = NFE_IRQ_MASK;
	sc->nfe_intrs = NFE_IRQ_WANTED;
	sc->nfe_nointrs = 0;
	if (sc->nfe_msix != 0) {
		sc->nfe_irq_status = NFE_MSIX_IRQ_STATUS;
		sc->nfe_nointrs = NFE_IRQ_WANTED;
	} else if (sc->nfe_msi != 0) {
		sc->nfe_irq_mask = NFE_MSI_IRQ_MASK;
		sc->nfe_intrs = NFE_MSI_VECTOR_0_ENABLED;
	}

	sc->nfe_devid = pci_get_device(dev);
	sc->nfe_revid = pci_get_revid(dev);
	sc->nfe_flags = 0;

	switch (sc->nfe_devid) {
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN2:
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN3:
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN4:
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN5:
		sc->nfe_flags |= NFE_JUMBO_SUP | NFE_HW_CSUM;
		break;
	case PCI_PRODUCT_NVIDIA_MCP51_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP51_LAN2:
		sc->nfe_flags |= NFE_40BIT_ADDR | NFE_PWR_MGMT | NFE_MIB_V1;
		break;
	case PCI_PRODUCT_NVIDIA_CK804_LAN1:
	case PCI_PRODUCT_NVIDIA_CK804_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP04_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP04_LAN2:
		sc->nfe_flags |= NFE_JUMBO_SUP | NFE_40BIT_ADDR | NFE_HW_CSUM |
		    NFE_MIB_V1;
		break;
	case PCI_PRODUCT_NVIDIA_MCP55_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP55_LAN2:
		sc->nfe_flags |= NFE_JUMBO_SUP | NFE_40BIT_ADDR | NFE_HW_CSUM |
		    NFE_HW_VLAN | NFE_PWR_MGMT | NFE_TX_FLOW_CTRL | NFE_MIB_V2;
		break;

	case PCI_PRODUCT_NVIDIA_MCP61_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP61_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP61_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP61_LAN4:
	case PCI_PRODUCT_NVIDIA_MCP67_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP67_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP67_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP67_LAN4:
	case PCI_PRODUCT_NVIDIA_MCP73_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP73_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP73_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP73_LAN4:
		sc->nfe_flags |= NFE_40BIT_ADDR | NFE_PWR_MGMT |
		    NFE_CORRECT_MACADDR | NFE_TX_FLOW_CTRL | NFE_MIB_V2;
		break;
	case PCI_PRODUCT_NVIDIA_MCP77_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP77_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP77_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP77_LAN4:
		/* XXX flow control */
		sc->nfe_flags |= NFE_40BIT_ADDR | NFE_HW_CSUM | NFE_PWR_MGMT |
		    NFE_CORRECT_MACADDR | NFE_MIB_V3;
		break;
	case PCI_PRODUCT_NVIDIA_MCP79_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP79_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP79_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP79_LAN4:
	case PCI_PRODUCT_NVIDIA_MCP89_LAN:
		/* XXX flow control */
		sc->nfe_flags |= NFE_JUMBO_SUP | NFE_40BIT_ADDR | NFE_HW_CSUM |
		    NFE_PWR_MGMT | NFE_CORRECT_MACADDR | NFE_MIB_V3;
		break;
	case PCI_PRODUCT_NVIDIA_MCP65_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP65_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP65_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP65_LAN4:
		sc->nfe_flags |= NFE_JUMBO_SUP | NFE_40BIT_ADDR |
		    NFE_PWR_MGMT | NFE_CORRECT_MACADDR | NFE_TX_FLOW_CTRL |
		    NFE_MIB_V2;
		break;
	}

	nfe_power(sc);
	/* Check for reversed ethernet address */
	if ((NFE_READ(sc, NFE_TX_UNK) & NFE_MAC_ADDR_INORDER) != 0)
		sc->nfe_flags |= NFE_CORRECT_MACADDR;
	nfe_get_macaddr(sc, sc->eaddr);
	/*
	 * Allocate the parent bus DMA tag appropriate for PCI.
	 */
	dma_addr_max = BUS_SPACE_MAXADDR_32BIT;
	if ((sc->nfe_flags & NFE_40BIT_ADDR) != 0)
		dma_addr_max = NFE_DMA_MAXADDR;
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->nfe_dev),	/* parent */
	    1, 0,				/* alignment, boundary */
	    dma_addr_max,			/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT, 0,		/* maxsize, nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,		/* maxsegsize */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockarg */
	    &sc->nfe_parent_tag);
	if (error)
		goto fail;

	ifp = sc->nfe_ifp = if_gethandle(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_gethandle()\n");
		error = ENOSPC;
		goto fail;
	}

	/*
	 * Allocate Tx and Rx rings.
	 */
	if ((error = nfe_alloc_tx_ring(sc, &sc->txq)) != 0)
		goto fail;

	if ((error = nfe_alloc_rx_ring(sc, &sc->rxq)) != 0)
		goto fail;

	nfe_alloc_jrx_ring(sc, &sc->jrxq);
	/* Create sysctl node. */
	nfe_sysctl_node(sc);

	if_setsoftc(ifp, sc);
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	if_setioctlfn(ifp, nfe_ioctl);
	if_setstartfn(ifp, nfe_start);
	if_sethwassist(ifp, 0);
	if_setcapabilities(ifp, 0);
	if_setinitfn(ifp, nfe_init);
	if_setsendqlen(ifp, NFE_TX_RING_COUNT - 1);
	if_setsendqready(ifp);


	if (sc->nfe_flags & NFE_HW_CSUM) {
		if_setcapabilitiesbit(ifp, IFCAP_HWCSUM | IFCAP_TSO4, 0);
		if_sethwassistbits(ifp, NFE_CSUM_FEATURES | CSUM_TSO, 0);
	}
	if_setcapenable(ifp, if_getcapabilities(ifp));

	sc->nfe_framesize = if_getmtu(ifp) + NFE_RX_HEADERS;
	/* VLAN capability setup. */
	if_setcapabilitiesbit(ifp, IFCAP_VLAN_MTU, 0);
	if ((sc->nfe_flags & NFE_HW_VLAN) != 0) {
		if_setcapabilitiesbit(ifp, IFCAP_VLAN_HWTAGGING, 0);
		if ((if_getcapabilities(ifp) & IFCAP_HWCSUM) != 0)
			if_setcapabilitiesbit(ifp,
			    (IFCAP_VLAN_HWCSUM | IFCAP_VLAN_HWTSO), 0);
	}

	if (pci_find_cap(dev, PCIY_PMG, &reg) == 0)
		if_setcapabilitiesbit(ifp, IFCAP_WOL_MAGIC, 0);
	if_setcapenable(ifp, if_getcapabilities(ifp));

	/*
	 * Tell the upper layer(s) we support long frames.
	 * Must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets ifi_hdrlen to the default value.
	 */
	if_setifheaderlen(ifp, sizeof(struct ether_vlan_header));

#ifdef DEVICE_POLLING
	if_setcapabilitiesbit(ifp, IFCAP_POLLING, 0);
#endif

	/* Do MII setup */
	phyloc = MII_PHY_ANY;
	if (sc->nfe_devid == PCI_PRODUCT_NVIDIA_MCP61_LAN1 ||
	    sc->nfe_devid == PCI_PRODUCT_NVIDIA_MCP61_LAN2 ||
	    sc->nfe_devid == PCI_PRODUCT_NVIDIA_MCP61_LAN3 ||
	    sc->nfe_devid == PCI_PRODUCT_NVIDIA_MCP61_LAN4) {
		if (nfe_detect_msik9(sc) != 0)
			phyloc = 0;
	}
	error = mii_attach(dev, &sc->nfe_miibus, ifp,
	    (ifm_change_cb_t)nfe_ifmedia_upd, (ifm_stat_cb_t)nfe_ifmedia_sts,
	    BMSR_DEFCAPMASK, phyloc, MII_OFFSET_ANY, MIIF_DOPAUSE);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}
	ether_ifattach(ifp, sc->eaddr);

	TASK_INIT(&sc->nfe_int_task, 0, nfe_int_task, sc);
	sc->nfe_tq = taskqueue_create_fast("nfe_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->nfe_tq);
	taskqueue_start_threads(&sc->nfe_tq, 1, PI_NET, "%s taskq",
	    device_get_nameunit(sc->nfe_dev));
	error = 0;
	if (sc->nfe_msi == 0 && sc->nfe_msix == 0) {
		error = bus_setup_intr(dev, sc->nfe_irq[0],
		    INTR_TYPE_NET | INTR_MPSAFE, nfe_intr, NULL, sc,
		    &sc->nfe_intrhand[0]);
	} else {
		for (i = 0; i < NFE_MSI_MESSAGES; i++) {
			error = bus_setup_intr(dev, sc->nfe_irq[i],
			    INTR_TYPE_NET | INTR_MPSAFE, nfe_intr, NULL, sc,
			    &sc->nfe_intrhand[i]);
			if (error != 0)
				break;
		}
	}
	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		taskqueue_free(sc->nfe_tq);
		sc->nfe_tq = NULL;
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error)
		nfe_detach(dev);

	return (error);
}


static int
nfe_detach(device_t dev)
{
	struct nfe_softc *sc;
	if_t ifp;
	uint8_t eaddr[ETHER_ADDR_LEN];
	int i, rid;

	sc = device_get_softc(dev);
	KASSERT(mtx_initialized(&sc->nfe_mtx), ("nfe mutex not initialized"));
	ifp = sc->nfe_ifp;

#ifdef DEVICE_POLLING
	if (ifp != NULL && if_getcapenable(ifp) & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif
	if (device_is_attached(dev)) {
		NFE_LOCK(sc);
		nfe_stop(ifp);
		if_setflagbits(ifp, 0, IFF_UP);
		NFE_UNLOCK(sc);
		callout_drain(&sc->nfe_stat_ch);
		ether_ifdetach(ifp);
	}

	if (ifp) {
		/* restore ethernet address */
		if ((sc->nfe_flags & NFE_CORRECT_MACADDR) == 0) {
			for (i = 0; i < ETHER_ADDR_LEN; i++) {
				eaddr[i] = sc->eaddr[5 - i];
			}
		} else
			bcopy(sc->eaddr, eaddr, ETHER_ADDR_LEN);
		nfe_set_macaddr(sc, eaddr);
		if_free(ifp);
	}
	if (sc->nfe_miibus)
		device_delete_child(dev, sc->nfe_miibus);
	bus_generic_detach(dev);
	if (sc->nfe_tq != NULL) {
		taskqueue_drain(sc->nfe_tq, &sc->nfe_int_task);
		taskqueue_free(sc->nfe_tq);
		sc->nfe_tq = NULL;
	}

	for (i = 0; i < NFE_MSI_MESSAGES; i++) {
		if (sc->nfe_intrhand[i] != NULL) {
			bus_teardown_intr(dev, sc->nfe_irq[i],
			    sc->nfe_intrhand[i]);
			sc->nfe_intrhand[i] = NULL;
		}
	}

	if (sc->nfe_msi == 0 && sc->nfe_msix == 0) {
		if (sc->nfe_irq[0] != NULL)
			bus_release_resource(dev, SYS_RES_IRQ, 0,
			    sc->nfe_irq[0]);
	} else {
		for (i = 0, rid = 1; i < NFE_MSI_MESSAGES; i++, rid++) {
			if (sc->nfe_irq[i] != NULL) {
				bus_release_resource(dev, SYS_RES_IRQ, rid,
				    sc->nfe_irq[i]);
				sc->nfe_irq[i] = NULL;
			}
		}
		pci_release_msi(dev);
	}
	if (sc->nfe_msix_pba_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, PCIR_BAR(3),
		    sc->nfe_msix_pba_res);
		sc->nfe_msix_pba_res = NULL;
	}
	if (sc->nfe_msix_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, PCIR_BAR(2),
		    sc->nfe_msix_res);
		sc->nfe_msix_res = NULL;
	}
	if (sc->nfe_res[0] != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, PCIR_BAR(0),
		    sc->nfe_res[0]);
		sc->nfe_res[0] = NULL;
	}

	nfe_free_tx_ring(sc, &sc->txq);
	nfe_free_rx_ring(sc, &sc->rxq);
	nfe_free_jrx_ring(sc, &sc->jrxq);

	if (sc->nfe_parent_tag) {
		bus_dma_tag_destroy(sc->nfe_parent_tag);
		sc->nfe_parent_tag = NULL;
	}

	mtx_destroy(&sc->nfe_mtx);

	return (0);
}


static int
nfe_suspend(device_t dev)
{
	struct nfe_softc *sc;

	sc = device_get_softc(dev);

	NFE_LOCK(sc);
	nfe_stop(sc->nfe_ifp);
	nfe_set_wol(sc);
	sc->nfe_suspended = 1;
	NFE_UNLOCK(sc);

	return (0);
}


static int
nfe_resume(device_t dev)
{
	struct nfe_softc *sc;
	if_t ifp;

	sc = device_get_softc(dev);

	NFE_LOCK(sc);
	nfe_power(sc);
	ifp = sc->nfe_ifp;
	if (if_getflags(ifp) & IFF_UP)
		nfe_init_locked(sc);
	sc->nfe_suspended = 0;
	NFE_UNLOCK(sc);

	return (0);
}


static int
nfe_can_use_msix(struct nfe_softc *sc)
{
	static struct msix_blacklist {
		char	*maker;
		char	*product;
	} msix_blacklists[] = {
		{ "ASUSTeK Computer INC.", "P5N32-SLI PREMIUM" }
	};

	struct msix_blacklist *mblp;
	char *maker, *product;
	int count, n, use_msix;

	/*
	 * Search base board manufacturer and product name table
	 * to see this system has a known MSI/MSI-X issue.
	 */
	maker = kern_getenv("smbios.planar.maker");
	product = kern_getenv("smbios.planar.product");
	use_msix = 1;
	if (maker != NULL && product != NULL) {
		count = nitems(msix_blacklists);
		mblp = msix_blacklists;
		for (n = 0; n < count; n++) {
			if (strcmp(maker, mblp->maker) == 0 &&
			    strcmp(product, mblp->product) == 0) {
				use_msix = 0;
				break;
			}
			mblp++;
		}
	}
	if (maker != NULL)
		freeenv(maker);
	if (product != NULL)
		freeenv(product);

	return (use_msix);
}


/* Take PHY/NIC out of powerdown, from Linux */
static void
nfe_power(struct nfe_softc *sc)
{
	uint32_t pwr;

	if ((sc->nfe_flags & NFE_PWR_MGMT) == 0)
		return;
	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_RESET | NFE_RXTX_BIT2);
	NFE_WRITE(sc, NFE_MAC_RESET, NFE_MAC_RESET_MAGIC);
	DELAY(100);
	NFE_WRITE(sc, NFE_MAC_RESET, 0);
	DELAY(100);
	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_BIT2);
	pwr = NFE_READ(sc, NFE_PWR2_CTL);
	pwr &= ~NFE_PWR2_WAKEUP_MASK;
	if (sc->nfe_revid >= 0xa3 &&
	    (sc->nfe_devid == PCI_PRODUCT_NVIDIA_NFORCE430_LAN1 ||
	    sc->nfe_devid == PCI_PRODUCT_NVIDIA_NFORCE430_LAN2))
		pwr |= NFE_PWR2_REVA3;
	NFE_WRITE(sc, NFE_PWR2_CTL, pwr);
}


static void
nfe_miibus_statchg(device_t dev)
{
	struct nfe_softc *sc;
	struct mii_data *mii;
	if_t ifp;
	uint32_t rxctl, txctl;

	sc = device_get_softc(dev);

	mii = device_get_softc(sc->nfe_miibus);
	ifp = sc->nfe_ifp;

	sc->nfe_link = 0;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
		case IFM_1000_T:
			sc->nfe_link = 1;
			break;
		default:
			break;
		}
	}

	nfe_mac_config(sc, mii);
	txctl = NFE_READ(sc, NFE_TX_CTL);
	rxctl = NFE_READ(sc, NFE_RX_CTL);
	if (sc->nfe_link != 0 && (if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) {
		txctl |= NFE_TX_START;
		rxctl |= NFE_RX_START;
	} else {
		txctl &= ~NFE_TX_START;
		rxctl &= ~NFE_RX_START;
	}
	NFE_WRITE(sc, NFE_TX_CTL, txctl);
	NFE_WRITE(sc, NFE_RX_CTL, rxctl);
}


static void
nfe_mac_config(struct nfe_softc *sc, struct mii_data *mii)
{
	uint32_t link, misc, phy, seed;
	uint32_t val;

	NFE_LOCK_ASSERT(sc);

	phy = NFE_READ(sc, NFE_PHY_IFACE);
	phy &= ~(NFE_PHY_HDX | NFE_PHY_100TX | NFE_PHY_1000T);

	seed = NFE_READ(sc, NFE_RNDSEED);
	seed &= ~NFE_SEED_MASK;

	misc = NFE_MISC1_MAGIC;
	link = NFE_MEDIA_SET;

	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) == 0) {
		phy  |= NFE_PHY_HDX;	/* half-duplex */
		misc |= NFE_MISC1_HDX;
	}

	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_1000_T:	/* full-duplex only */
		link |= NFE_MEDIA_1000T;
		seed |= NFE_SEED_1000T;
		phy  |= NFE_PHY_1000T;
		break;
	case IFM_100_TX:
		link |= NFE_MEDIA_100TX;
		seed |= NFE_SEED_100TX;
		phy  |= NFE_PHY_100TX;
		break;
	case IFM_10_T:
		link |= NFE_MEDIA_10T;
		seed |= NFE_SEED_10T;
		break;
	}

	if ((phy & 0x10000000) != 0) {
		if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T)
			val = NFE_R1_MAGIC_1000;
		else
			val = NFE_R1_MAGIC_10_100;
	} else
		val = NFE_R1_MAGIC_DEFAULT;
	NFE_WRITE(sc, NFE_SETUP_R1, val);

	NFE_WRITE(sc, NFE_RNDSEED, seed);	/* XXX: gigabit NICs only? */

	NFE_WRITE(sc, NFE_PHY_IFACE, phy);
	NFE_WRITE(sc, NFE_MISC1, misc);
	NFE_WRITE(sc, NFE_LINKSPEED, link);

	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		/* It seems all hardwares supports Rx pause frames. */
		val = NFE_READ(sc, NFE_RXFILTER);
		if ((IFM_OPTIONS(mii->mii_media_active) &
		    IFM_ETH_RXPAUSE) != 0)
			val |= NFE_PFF_RX_PAUSE;
		else
			val &= ~NFE_PFF_RX_PAUSE;
		NFE_WRITE(sc, NFE_RXFILTER, val);
		if ((sc->nfe_flags & NFE_TX_FLOW_CTRL) != 0) {
			val = NFE_READ(sc, NFE_MISC1);
			if ((IFM_OPTIONS(mii->mii_media_active) &
			    IFM_ETH_TXPAUSE) != 0) {
				NFE_WRITE(sc, NFE_TX_PAUSE_FRAME,
				    NFE_TX_PAUSE_FRAME_ENABLE);
				val |= NFE_MISC1_TX_PAUSE;
			} else {
				val &= ~NFE_MISC1_TX_PAUSE;
				NFE_WRITE(sc, NFE_TX_PAUSE_FRAME,
				    NFE_TX_PAUSE_FRAME_DISABLE);
			}
			NFE_WRITE(sc, NFE_MISC1, val);
		}
	} else {
		/* disable rx/tx pause frames */
		val = NFE_READ(sc, NFE_RXFILTER);
		val &= ~NFE_PFF_RX_PAUSE;
		NFE_WRITE(sc, NFE_RXFILTER, val);
		if ((sc->nfe_flags & NFE_TX_FLOW_CTRL) != 0) {
			NFE_WRITE(sc, NFE_TX_PAUSE_FRAME,
			    NFE_TX_PAUSE_FRAME_DISABLE);
			val = NFE_READ(sc, NFE_MISC1);
			val &= ~NFE_MISC1_TX_PAUSE;
			NFE_WRITE(sc, NFE_MISC1, val);
		}
	}
}


static int
nfe_miibus_readreg(device_t dev, int phy, int reg)
{
	struct nfe_softc *sc = device_get_softc(dev);
	uint32_t val;
	int ntries;

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	if (NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY) {
		NFE_WRITE(sc, NFE_PHY_CTL, NFE_PHY_BUSY);
		DELAY(100);
	}

	NFE_WRITE(sc, NFE_PHY_CTL, (phy << NFE_PHYADD_SHIFT) | reg);

	for (ntries = 0; ntries < NFE_TIMEOUT; ntries++) {
		DELAY(100);
		if (!(NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY))
			break;
	}
	if (ntries == NFE_TIMEOUT) {
		DPRINTFN(sc, 2, "timeout waiting for PHY\n");
		return 0;
	}

	if (NFE_READ(sc, NFE_PHY_STATUS) & NFE_PHY_ERROR) {
		DPRINTFN(sc, 2, "could not read PHY\n");
		return 0;
	}

	val = NFE_READ(sc, NFE_PHY_DATA);
	if (val != 0xffffffff && val != 0)
		sc->mii_phyaddr = phy;

	DPRINTFN(sc, 2, "mii read phy %d reg 0x%x ret 0x%x\n", phy, reg, val);

	return (val);
}


static int
nfe_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct nfe_softc *sc = device_get_softc(dev);
	uint32_t ctl;
	int ntries;

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	if (NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY) {
		NFE_WRITE(sc, NFE_PHY_CTL, NFE_PHY_BUSY);
		DELAY(100);
	}

	NFE_WRITE(sc, NFE_PHY_DATA, val);
	ctl = NFE_PHY_WRITE | (phy << NFE_PHYADD_SHIFT) | reg;
	NFE_WRITE(sc, NFE_PHY_CTL, ctl);

	for (ntries = 0; ntries < NFE_TIMEOUT; ntries++) {
		DELAY(100);
		if (!(NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY))
			break;
	}
#ifdef NFE_DEBUG
	if (nfedebug >= 2 && ntries == NFE_TIMEOUT)
		device_printf(sc->nfe_dev, "could not write to PHY\n");
#endif
	return (0);
}

struct nfe_dmamap_arg {
	bus_addr_t nfe_busaddr;
};

static int
nfe_alloc_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	struct nfe_dmamap_arg ctx;
	struct nfe_rx_data *data;
	void *desc;
	int i, error, descsize;

	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc = ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	ring->cur = ring->next = 0;

	error = bus_dma_tag_create(sc->nfe_parent_tag,
	    NFE_RING_ALIGN, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,			/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filter, filterarg */
	    NFE_RX_RING_COUNT * descsize, 1,	/* maxsize, nsegments */
	    NFE_RX_RING_COUNT * descsize,	/* maxsegsize */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockarg */
	    &ring->rx_desc_tag);
	if (error != 0) {
		device_printf(sc->nfe_dev, "could not create desc DMA tag\n");
		goto fail;
	}

	/* allocate memory to desc */
	error = bus_dmamem_alloc(ring->rx_desc_tag, &desc, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &ring->rx_desc_map);
	if (error != 0) {
		device_printf(sc->nfe_dev, "could not create desc DMA map\n");
		goto fail;
	}
	if (sc->nfe_flags & NFE_40BIT_ADDR)
		ring->desc64 = desc;
	else
		ring->desc32 = desc;

	/* map desc to device visible address space */
	ctx.nfe_busaddr = 0;
	error = bus_dmamap_load(ring->rx_desc_tag, ring->rx_desc_map, desc,
	    NFE_RX_RING_COUNT * descsize, nfe_dma_map_segs, &ctx, 0);
	if (error != 0) {
		device_printf(sc->nfe_dev, "could not load desc DMA map\n");
		goto fail;
	}
	ring->physaddr = ctx.nfe_busaddr;

	error = bus_dma_tag_create(sc->nfe_parent_tag,
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES, 1,		/* maxsize, nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &ring->rx_data_tag);
	if (error != 0) {
		device_printf(sc->nfe_dev, "could not create Rx DMA tag\n");
		goto fail;
	}

	error = bus_dmamap_create(ring->rx_data_tag, 0, &ring->rx_spare_map);
	if (error != 0) {
		device_printf(sc->nfe_dev,
		    "could not create Rx DMA spare map\n");
		goto fail;
	}

	/*
	 * Pre-allocate Rx buffers and populate Rx ring.
	 */
	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		data = &sc->rxq.data[i];
		data->rx_data_map = NULL;
		data->m = NULL;
		error = bus_dmamap_create(ring->rx_data_tag, 0,
		    &data->rx_data_map);
		if (error != 0) {
			device_printf(sc->nfe_dev,
			    "could not create Rx DMA map\n");
			goto fail;
		}
	}

fail:
	return (error);
}


static void
nfe_alloc_jrx_ring(struct nfe_softc *sc, struct nfe_jrx_ring *ring)
{
	struct nfe_dmamap_arg ctx;
	struct nfe_rx_data *data;
	void *desc;
	int i, error, descsize;

	if ((sc->nfe_flags & NFE_JUMBO_SUP) == 0)
		return;
	if (jumbo_disable != 0) {
		device_printf(sc->nfe_dev, "disabling jumbo frame support\n");
		sc->nfe_jumbo_disable = 1;
		return;
	}

	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc = ring->jdesc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->jdesc32;
		descsize = sizeof (struct nfe_desc32);
	}

	ring->jcur = ring->jnext = 0;

	/* Create DMA tag for jumbo Rx ring. */
	error = bus_dma_tag_create(sc->nfe_parent_tag,
	    NFE_RING_ALIGN, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,			/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filter, filterarg */
	    NFE_JUMBO_RX_RING_COUNT * descsize,	/* maxsize */
	    1, 					/* nsegments */
	    NFE_JUMBO_RX_RING_COUNT * descsize,	/* maxsegsize */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockarg */
	    &ring->jrx_desc_tag);
	if (error != 0) {
		device_printf(sc->nfe_dev,
		    "could not create jumbo ring DMA tag\n");
		goto fail;
	}

	/* Create DMA tag for jumbo Rx buffers. */
	error = bus_dma_tag_create(sc->nfe_parent_tag,
	    1, 0,				/* alignment, boundary */
	    BUS_SPACE_MAXADDR,			/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filter, filterarg */
	    MJUM9BYTES,				/* maxsize */
	    1,					/* nsegments */
	    MJUM9BYTES,				/* maxsegsize */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockarg */
	    &ring->jrx_data_tag);
	if (error != 0) {
		device_printf(sc->nfe_dev,
		    "could not create jumbo Rx buffer DMA tag\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for jumbo Rx ring. */
	error = bus_dmamem_alloc(ring->jrx_desc_tag, &desc, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &ring->jrx_desc_map);
	if (error != 0) {
		device_printf(sc->nfe_dev,
		    "could not allocate DMA'able memory for jumbo Rx ring\n");
		goto fail;
	}
	if (sc->nfe_flags & NFE_40BIT_ADDR)
		ring->jdesc64 = desc;
	else
		ring->jdesc32 = desc;

	ctx.nfe_busaddr = 0;
	error = bus_dmamap_load(ring->jrx_desc_tag, ring->jrx_desc_map, desc,
	    NFE_JUMBO_RX_RING_COUNT * descsize, nfe_dma_map_segs, &ctx, 0);
	if (error != 0) {
		device_printf(sc->nfe_dev,
		    "could not load DMA'able memory for jumbo Rx ring\n");
		goto fail;
	}
	ring->jphysaddr = ctx.nfe_busaddr;

	/* Create DMA maps for jumbo Rx buffers. */
	error = bus_dmamap_create(ring->jrx_data_tag, 0, &ring->jrx_spare_map);
	if (error != 0) {
		device_printf(sc->nfe_dev,
		    "could not create jumbo Rx DMA spare map\n");
		goto fail;
	}

	for (i = 0; i < NFE_JUMBO_RX_RING_COUNT; i++) {
		data = &sc->jrxq.jdata[i];
		data->rx_data_map = NULL;
		data->m = NULL;
		error = bus_dmamap_create(ring->jrx_data_tag, 0,
		    &data->rx_data_map);
		if (error != 0) {
			device_printf(sc->nfe_dev,
			    "could not create jumbo Rx DMA map\n");
			goto fail;
		}
	}

	return;

fail:
	/*
	 * Running without jumbo frame support is ok for most cases
	 * so don't fail on creating dma tag/map for jumbo frame.
	 */
	nfe_free_jrx_ring(sc, ring);
	device_printf(sc->nfe_dev, "disabling jumbo frame support due to "
	    "resource shortage\n");
	sc->nfe_jumbo_disable = 1;
}


static int
nfe_init_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	void *desc;
	size_t descsize;
	int i;

	ring->cur = ring->next = 0;
	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc = ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}
	bzero(desc, descsize * NFE_RX_RING_COUNT);
	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		if (nfe_newbuf(sc, i) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(ring->rx_desc_tag, ring->rx_desc_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}


static int
nfe_init_jrx_ring(struct nfe_softc *sc, struct nfe_jrx_ring *ring)
{
	void *desc;
	size_t descsize;
	int i;

	ring->jcur = ring->jnext = 0;
	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc = ring->jdesc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->jdesc32;
		descsize = sizeof (struct nfe_desc32);
	}
	bzero(desc, descsize * NFE_JUMBO_RX_RING_COUNT);
	for (i = 0; i < NFE_JUMBO_RX_RING_COUNT; i++) {
		if (nfe_jnewbuf(sc, i) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(ring->jrx_desc_tag, ring->jrx_desc_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}


static void
nfe_free_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	struct nfe_rx_data *data;
	void *desc;
	int i;

	if (sc->nfe_flags & NFE_40BIT_ADDR)
		desc = ring->desc64;
	else
		desc = ring->desc32;

	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		data = &ring->data[i];
		if (data->rx_data_map != NULL) {
			bus_dmamap_destroy(ring->rx_data_tag,
			    data->rx_data_map);
			data->rx_data_map = NULL;
		}
		if (data->m != NULL) {
			m_freem(data->m);
			data->m = NULL;
		}
	}
	if (ring->rx_data_tag != NULL) {
		if (ring->rx_spare_map != NULL) {
			bus_dmamap_destroy(ring->rx_data_tag,
			    ring->rx_spare_map);
			ring->rx_spare_map = NULL;
		}
		bus_dma_tag_destroy(ring->rx_data_tag);
		ring->rx_data_tag = NULL;
	}

	if (desc != NULL) {
		bus_dmamap_unload(ring->rx_desc_tag, ring->rx_desc_map);
		bus_dmamem_free(ring->rx_desc_tag, desc, ring->rx_desc_map);
		ring->desc64 = NULL;
		ring->desc32 = NULL;
	}
	if (ring->rx_desc_tag != NULL) {
		bus_dma_tag_destroy(ring->rx_desc_tag);
		ring->rx_desc_tag = NULL;
	}
}


static void
nfe_free_jrx_ring(struct nfe_softc *sc, struct nfe_jrx_ring *ring)
{
	struct nfe_rx_data *data;
	void *desc;
	int i, descsize;

	if ((sc->nfe_flags & NFE_JUMBO_SUP) == 0)
		return;

	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc = ring->jdesc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->jdesc32;
		descsize = sizeof (struct nfe_desc32);
	}

	for (i = 0; i < NFE_JUMBO_RX_RING_COUNT; i++) {
		data = &ring->jdata[i];
		if (data->rx_data_map != NULL) {
			bus_dmamap_destroy(ring->jrx_data_tag,
			    data->rx_data_map);
			data->rx_data_map = NULL;
		}
		if (data->m != NULL) {
			m_freem(data->m);
			data->m = NULL;
		}
	}
	if (ring->jrx_data_tag != NULL) {
		if (ring->jrx_spare_map != NULL) {
			bus_dmamap_destroy(ring->jrx_data_tag,
			    ring->jrx_spare_map);
			ring->jrx_spare_map = NULL;
		}
		bus_dma_tag_destroy(ring->jrx_data_tag);
		ring->jrx_data_tag = NULL;
	}

	if (desc != NULL) {
		bus_dmamap_unload(ring->jrx_desc_tag, ring->jrx_desc_map);
		bus_dmamem_free(ring->jrx_desc_tag, desc, ring->jrx_desc_map);
		ring->jdesc64 = NULL;
		ring->jdesc32 = NULL;
	}

	if (ring->jrx_desc_tag != NULL) {
		bus_dma_tag_destroy(ring->jrx_desc_tag);
		ring->jrx_desc_tag = NULL;
	}
}


static int
nfe_alloc_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	struct nfe_dmamap_arg ctx;
	int i, error;
	void *desc;
	int descsize;

	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc = ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	ring->queued = 0;
	ring->cur = ring->next = 0;

	error = bus_dma_tag_create(sc->nfe_parent_tag,
	    NFE_RING_ALIGN, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,			/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filter, filterarg */
	    NFE_TX_RING_COUNT * descsize, 1,	/* maxsize, nsegments */
	    NFE_TX_RING_COUNT * descsize,	/* maxsegsize */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockarg */
	    &ring->tx_desc_tag);
	if (error != 0) {
		device_printf(sc->nfe_dev, "could not create desc DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(ring->tx_desc_tag, &desc, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &ring->tx_desc_map);
	if (error != 0) {
		device_printf(sc->nfe_dev, "could not create desc DMA map\n");
		goto fail;
	}
	if (sc->nfe_flags & NFE_40BIT_ADDR)
		ring->desc64 = desc;
	else
		ring->desc32 = desc;

	ctx.nfe_busaddr = 0;
	error = bus_dmamap_load(ring->tx_desc_tag, ring->tx_desc_map, desc,
	    NFE_TX_RING_COUNT * descsize, nfe_dma_map_segs, &ctx, 0);
	if (error != 0) {
		device_printf(sc->nfe_dev, "could not load desc DMA map\n");
		goto fail;
	}
	ring->physaddr = ctx.nfe_busaddr;

	error = bus_dma_tag_create(sc->nfe_parent_tag,
	    1, 0,
	    BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    NFE_TSO_MAXSIZE,
	    NFE_MAX_SCATTER,
	    NFE_TSO_MAXSGSIZE,
	    0,
	    NULL, NULL,
	    &ring->tx_data_tag);
	if (error != 0) {
		device_printf(sc->nfe_dev, "could not create Tx DMA tag\n");
		goto fail;
	}

	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		error = bus_dmamap_create(ring->tx_data_tag, 0,
		    &ring->data[i].tx_data_map);
		if (error != 0) {
			device_printf(sc->nfe_dev,
			    "could not create Tx DMA map\n");
			goto fail;
		}
	}

fail:
	return (error);
}


static void
nfe_init_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	void *desc;
	size_t descsize;

	sc->nfe_force_tx = 0;
	ring->queued = 0;
	ring->cur = ring->next = 0;
	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc = ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}
	bzero(desc, descsize * NFE_TX_RING_COUNT);

	bus_dmamap_sync(ring->tx_desc_tag, ring->tx_desc_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}


static void
nfe_free_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	struct nfe_tx_data *data;
	void *desc;
	int i, descsize;

	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc = ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(ring->tx_data_tag, data->tx_data_map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ring->tx_data_tag, data->tx_data_map);
			m_freem(data->m);
			data->m = NULL;
		}
		if (data->tx_data_map != NULL) {
			bus_dmamap_destroy(ring->tx_data_tag,
			    data->tx_data_map);
			data->tx_data_map = NULL;
		}
	}

	if (ring->tx_data_tag != NULL) {
		bus_dma_tag_destroy(ring->tx_data_tag);
		ring->tx_data_tag = NULL;
	}

	if (desc != NULL) {
		bus_dmamap_sync(ring->tx_desc_tag, ring->tx_desc_map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ring->tx_desc_tag, ring->tx_desc_map);
		bus_dmamem_free(ring->tx_desc_tag, desc, ring->tx_desc_map);
		ring->desc64 = NULL;
		ring->desc32 = NULL;
		bus_dma_tag_destroy(ring->tx_desc_tag);
		ring->tx_desc_tag = NULL;
	}
}

#ifdef DEVICE_POLLING
static poll_handler_t nfe_poll;


static int
nfe_poll(if_t ifp, enum poll_cmd cmd, int count)
{
	struct nfe_softc *sc = if_getsoftc(ifp);
	uint32_t r;
	int rx_npkts = 0;

	NFE_LOCK(sc);

	if (!(if_getdrvflags(ifp) & IFF_DRV_RUNNING)) {
		NFE_UNLOCK(sc);
		return (rx_npkts);
	}

	if (sc->nfe_framesize > MCLBYTES - ETHER_HDR_LEN)
		rx_npkts = nfe_jrxeof(sc, count, &rx_npkts);
	else
		rx_npkts = nfe_rxeof(sc, count, &rx_npkts);
	nfe_txeof(sc);
	if (!if_sendq_empty(ifp))
		nfe_start_locked(ifp);

	if (cmd == POLL_AND_CHECK_STATUS) {
		if ((r = NFE_READ(sc, sc->nfe_irq_status)) == 0) {
			NFE_UNLOCK(sc);
			return (rx_npkts);
		}
		NFE_WRITE(sc, sc->nfe_irq_status, r);

		if (r & NFE_IRQ_LINK) {
			NFE_READ(sc, NFE_PHY_STATUS);
			NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);
			DPRINTF(sc, "link state changed\n");
		}
	}
	NFE_UNLOCK(sc);
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

static void
nfe_set_intr(struct nfe_softc *sc)
{

	if (sc->nfe_msi != 0)
		NFE_WRITE(sc, NFE_IRQ_MASK, NFE_IRQ_WANTED);
}


/* In MSIX, a write to mask reegisters behaves as XOR. */
static __inline void
nfe_enable_intr(struct nfe_softc *sc)
{

	if (sc->nfe_msix != 0) {
		/* XXX Should have a better way to enable interrupts! */
		if (NFE_READ(sc, sc->nfe_irq_mask) == 0)
			NFE_WRITE(sc, sc->nfe_irq_mask, sc->nfe_intrs);
	} else
		NFE_WRITE(sc, sc->nfe_irq_mask, sc->nfe_intrs);
}


static __inline void
nfe_disable_intr(struct nfe_softc *sc)
{

	if (sc->nfe_msix != 0) {
		/* XXX Should have a better way to disable interrupts! */
		if (NFE_READ(sc, sc->nfe_irq_mask) != 0)
			NFE_WRITE(sc, sc->nfe_irq_mask, sc->nfe_nointrs);
	} else
		NFE_WRITE(sc, sc->nfe_irq_mask, sc->nfe_nointrs);
}


static int
nfe_ioctl(if_t ifp, u_long cmd, caddr_t data)
{
	struct nfe_softc *sc;
	struct ifreq *ifr;
	struct mii_data *mii;
	int error, init, mask;

	sc = if_getsoftc(ifp);
	ifr = (struct ifreq *) data;
	error = 0;
	init = 0;
	switch (cmd) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > NFE_JUMBO_MTU)
			error = EINVAL;
		else if (if_getmtu(ifp) != ifr->ifr_mtu) {
			if ((((sc->nfe_flags & NFE_JUMBO_SUP) == 0) ||
			    (sc->nfe_jumbo_disable != 0)) &&
			    ifr->ifr_mtu > ETHERMTU)
				error = EINVAL;
			else {
				NFE_LOCK(sc);
				if_setmtu(ifp, ifr->ifr_mtu);
				if (if_getdrvflags(ifp) & IFF_DRV_RUNNING) {
					if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
					nfe_init_locked(sc);
				}
				NFE_UNLOCK(sc);
			}
		}
		break;
	case SIOCSIFFLAGS:
		NFE_LOCK(sc);
		if (if_getflags(ifp) & IFF_UP) {
			/*
			 * If only the PROMISC or ALLMULTI flag changes, then
			 * don't do a full re-init of the chip, just update
			 * the Rx filter.
			 */
			if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) &&
			    ((if_getflags(ifp) ^ sc->nfe_if_flags) &
			     (IFF_ALLMULTI | IFF_PROMISC)) != 0)
				nfe_setmulti(sc);
			else
				nfe_init_locked(sc);
		} else {
			if (if_getdrvflags(ifp) & IFF_DRV_RUNNING)
				nfe_stop(ifp);
		}
		sc->nfe_if_flags = if_getflags(ifp);
		NFE_UNLOCK(sc);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) {
			NFE_LOCK(sc);
			nfe_setmulti(sc);
			NFE_UNLOCK(sc);
			error = 0;
		}
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->nfe_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;
	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ if_getcapenable(ifp);
#ifdef DEVICE_POLLING
		if ((mask & IFCAP_POLLING) != 0) {
			if ((ifr->ifr_reqcap & IFCAP_POLLING) != 0) {
				error = ether_poll_register(nfe_poll, ifp);
				if (error)
					break;
				NFE_LOCK(sc);
				nfe_disable_intr(sc);
				if_setcapenablebit(ifp, IFCAP_POLLING, 0);
				NFE_UNLOCK(sc);
			} else {
				error = ether_poll_deregister(ifp);
				/* Enable interrupt even in error case */
				NFE_LOCK(sc);
				nfe_enable_intr(sc);
				if_setcapenablebit(ifp, 0, IFCAP_POLLING);
				NFE_UNLOCK(sc);
			}
		}
#endif /* DEVICE_POLLING */
		if ((mask & IFCAP_WOL_MAGIC) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_WOL_MAGIC) != 0)
			if_togglecapenable(ifp, IFCAP_WOL_MAGIC);
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_TXCSUM) != 0) {
			if_togglecapenable(ifp, IFCAP_TXCSUM);
			if ((if_getcapenable(ifp) & IFCAP_TXCSUM) != 0)
				if_sethwassistbits(ifp, NFE_CSUM_FEATURES, 0);
			else
				if_sethwassistbits(ifp, 0, NFE_CSUM_FEATURES);
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_RXCSUM) != 0) {
			if_togglecapenable(ifp, IFCAP_RXCSUM);
			init++;
		}
		if ((mask & IFCAP_TSO4) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_TSO4) != 0) {
			if_togglecapenable(ifp, IFCAP_TSO4);
			if ((IFCAP_TSO4 & if_getcapenable(ifp)) != 0)
				if_sethwassistbits(ifp, CSUM_TSO, 0);
			else
				if_sethwassistbits(ifp, 0, CSUM_TSO);
		}
		if ((mask & IFCAP_VLAN_HWTSO) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_VLAN_HWTSO) != 0)
			if_togglecapenable(ifp, IFCAP_VLAN_HWTSO);
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_VLAN_HWTAGGING) != 0) {
			if_togglecapenable(ifp, IFCAP_VLAN_HWTAGGING);
			if ((if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) == 0)
				if_setcapenablebit(ifp, 0, IFCAP_VLAN_HWTSO);
			init++;
		}
		/*
		 * XXX
		 * It seems that VLAN stripping requires Rx checksum offload.
		 * Unfortunately FreeBSD has no way to disable only Rx side
		 * VLAN stripping. So when we know Rx checksum offload is
		 * disabled turn entire hardware VLAN assist off.
		 */
		if ((if_getcapenable(ifp) & IFCAP_RXCSUM) == 0) {
			if ((if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) != 0)
				init++;
			if_setcapenablebit(ifp, 0,
			    (IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWTSO));
		}
		if (init > 0 && (if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) {
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
			nfe_init(sc);
		}
		if_vlancap(ifp);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}


static int
nfe_intr(void *arg)
{
	struct nfe_softc *sc;
	uint32_t status;

	sc = (struct nfe_softc *)arg;

	status = NFE_READ(sc, sc->nfe_irq_status);
	if (status == 0 || status == 0xffffffff)
		return (FILTER_STRAY);
	nfe_disable_intr(sc);
	taskqueue_enqueue(sc->nfe_tq, &sc->nfe_int_task);

	return (FILTER_HANDLED);
}


static void
nfe_int_task(void *arg, int pending)
{
	struct nfe_softc *sc = arg;
	if_t ifp = sc->nfe_ifp;
	uint32_t r;
	int domore;

	NFE_LOCK(sc);

	if ((r = NFE_READ(sc, sc->nfe_irq_status)) == 0) {
		nfe_enable_intr(sc);
		NFE_UNLOCK(sc);
		return;	/* not for us */
	}
	NFE_WRITE(sc, sc->nfe_irq_status, r);

	DPRINTFN(sc, 5, "nfe_intr: interrupt register %x\n", r);

#ifdef DEVICE_POLLING
	if (if_getcapenable(ifp) & IFCAP_POLLING) {
		NFE_UNLOCK(sc);
		return;
	}
#endif

	if (r & NFE_IRQ_LINK) {
		NFE_READ(sc, NFE_PHY_STATUS);
		NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);
		DPRINTF(sc, "link state changed\n");
	}

	if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) == 0) {
		NFE_UNLOCK(sc);
		nfe_disable_intr(sc);
		return;
	}

	domore = 0;
	/* check Rx ring */
	if (sc->nfe_framesize > MCLBYTES - ETHER_HDR_LEN)
		domore = nfe_jrxeof(sc, sc->nfe_process_limit, NULL);
	else
		domore = nfe_rxeof(sc, sc->nfe_process_limit, NULL);
	/* check Tx ring */
	nfe_txeof(sc);

	if (!if_sendq_empty(ifp))
		nfe_start_locked(ifp);

	NFE_UNLOCK(sc);

	if (domore || (NFE_READ(sc, sc->nfe_irq_status) != 0)) {
		taskqueue_enqueue(sc->nfe_tq, &sc->nfe_int_task);
		return;
	}

	/* Reenable interrupts. */
	nfe_enable_intr(sc);
}


static __inline void
nfe_discard_rxbuf(struct nfe_softc *sc, int idx)
{
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_rx_data *data;
	struct mbuf *m;

	data = &sc->rxq.data[idx];
	m = data->m;

	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc64 = &sc->rxq.desc64[idx];
		/* VLAN packet may have overwritten it. */
		desc64->physaddr[0] = htole32(NFE_ADDR_HI(data->paddr));
		desc64->physaddr[1] = htole32(NFE_ADDR_LO(data->paddr));
		desc64->length = htole16(m->m_len);
		desc64->flags = htole16(NFE_RX_READY);
	} else {
		desc32 = &sc->rxq.desc32[idx];
		desc32->length = htole16(m->m_len);
		desc32->flags = htole16(NFE_RX_READY);
	}
}


static __inline void
nfe_discard_jrxbuf(struct nfe_softc *sc, int idx)
{
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_rx_data *data;
	struct mbuf *m;

	data = &sc->jrxq.jdata[idx];
	m = data->m;

	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc64 = &sc->jrxq.jdesc64[idx];
		/* VLAN packet may have overwritten it. */
		desc64->physaddr[0] = htole32(NFE_ADDR_HI(data->paddr));
		desc64->physaddr[1] = htole32(NFE_ADDR_LO(data->paddr));
		desc64->length = htole16(m->m_len);
		desc64->flags = htole16(NFE_RX_READY);
	} else {
		desc32 = &sc->jrxq.jdesc32[idx];
		desc32->length = htole16(m->m_len);
		desc32->flags = htole16(NFE_RX_READY);
	}
}


static int
nfe_newbuf(struct nfe_softc *sc, int idx)
{
	struct nfe_rx_data *data;
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);

	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, ETHER_ALIGN);

	if (bus_dmamap_load_mbuf_sg(sc->rxq.rx_data_tag, sc->rxq.rx_spare_map,
	    m, segs, &nsegs, BUS_DMA_NOWAIT) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	data = &sc->rxq.data[idx];
	if (data->m != NULL) {
		bus_dmamap_sync(sc->rxq.rx_data_tag, data->rx_data_map,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->rxq.rx_data_tag, data->rx_data_map);
	}
	map = data->rx_data_map;
	data->rx_data_map = sc->rxq.rx_spare_map;
	sc->rxq.rx_spare_map = map;
	bus_dmamap_sync(sc->rxq.rx_data_tag, data->rx_data_map,
	    BUS_DMASYNC_PREREAD);
	data->paddr = segs[0].ds_addr;
	data->m = m;
	/* update mapping address in h/w descriptor */
	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc64 = &sc->rxq.desc64[idx];
		desc64->physaddr[0] = htole32(NFE_ADDR_HI(segs[0].ds_addr));
		desc64->physaddr[1] = htole32(NFE_ADDR_LO(segs[0].ds_addr));
		desc64->length = htole16(segs[0].ds_len);
		desc64->flags = htole16(NFE_RX_READY);
	} else {
		desc32 = &sc->rxq.desc32[idx];
		desc32->physaddr = htole32(NFE_ADDR_LO(segs[0].ds_addr));
		desc32->length = htole16(segs[0].ds_len);
		desc32->flags = htole16(NFE_RX_READY);
	}

	return (0);
}


static int
nfe_jnewbuf(struct nfe_softc *sc, int idx)
{
	struct nfe_rx_data *data;
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int nsegs;

	m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUM9BYTES);
	if (m == NULL)
		return (ENOBUFS);
	m->m_pkthdr.len = m->m_len = MJUM9BYTES;
	m_adj(m, ETHER_ALIGN);

	if (bus_dmamap_load_mbuf_sg(sc->jrxq.jrx_data_tag,
	    sc->jrxq.jrx_spare_map, m, segs, &nsegs, BUS_DMA_NOWAIT) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	data = &sc->jrxq.jdata[idx];
	if (data->m != NULL) {
		bus_dmamap_sync(sc->jrxq.jrx_data_tag, data->rx_data_map,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->jrxq.jrx_data_tag, data->rx_data_map);
	}
	map = data->rx_data_map;
	data->rx_data_map = sc->jrxq.jrx_spare_map;
	sc->jrxq.jrx_spare_map = map;
	bus_dmamap_sync(sc->jrxq.jrx_data_tag, data->rx_data_map,
	    BUS_DMASYNC_PREREAD);
	data->paddr = segs[0].ds_addr;
	data->m = m;
	/* update mapping address in h/w descriptor */
	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc64 = &sc->jrxq.jdesc64[idx];
		desc64->physaddr[0] = htole32(NFE_ADDR_HI(segs[0].ds_addr));
		desc64->physaddr[1] = htole32(NFE_ADDR_LO(segs[0].ds_addr));
		desc64->length = htole16(segs[0].ds_len);
		desc64->flags = htole16(NFE_RX_READY);
	} else {
		desc32 = &sc->jrxq.jdesc32[idx];
		desc32->physaddr = htole32(NFE_ADDR_LO(segs[0].ds_addr));
		desc32->length = htole16(segs[0].ds_len);
		desc32->flags = htole16(NFE_RX_READY);
	}

	return (0);
}


static int
nfe_rxeof(struct nfe_softc *sc, int count, int *rx_npktsp)
{
	if_t ifp = sc->nfe_ifp;
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_rx_data *data;
	struct mbuf *m;
	uint16_t flags;
	int len, prog, rx_npkts;
	uint32_t vtag = 0;

	rx_npkts = 0;
	NFE_LOCK_ASSERT(sc);

	bus_dmamap_sync(sc->rxq.rx_desc_tag, sc->rxq.rx_desc_map,
	    BUS_DMASYNC_POSTREAD);

	for (prog = 0;;NFE_INC(sc->rxq.cur, NFE_RX_RING_COUNT), vtag = 0) {
		if (count <= 0)
			break;
		count--;

		data = &sc->rxq.data[sc->rxq.cur];

		if (sc->nfe_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->rxq.desc64[sc->rxq.cur];
			vtag = le32toh(desc64->physaddr[1]);
			flags = le16toh(desc64->flags);
			len = le16toh(desc64->length) & NFE_RX_LEN_MASK;
		} else {
			desc32 = &sc->rxq.desc32[sc->rxq.cur];
			flags = le16toh(desc32->flags);
			len = le16toh(desc32->length) & NFE_RX_LEN_MASK;
		}

		if (flags & NFE_RX_READY)
			break;
		prog++;
		if ((sc->nfe_flags & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
			if (!(flags & NFE_RX_VALID_V1)) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				nfe_discard_rxbuf(sc, sc->rxq.cur);
				continue;
			}
			if ((flags & NFE_RX_FIXME_V1) == NFE_RX_FIXME_V1) {
				flags &= ~NFE_RX_ERROR;
				len--;	/* fix buffer length */
			}
		} else {
			if (!(flags & NFE_RX_VALID_V2)) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				nfe_discard_rxbuf(sc, sc->rxq.cur);
				continue;
			}

			if ((flags & NFE_RX_FIXME_V2) == NFE_RX_FIXME_V2) {
				flags &= ~NFE_RX_ERROR;
				len--;	/* fix buffer length */
			}
		}

		if (flags & NFE_RX_ERROR) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			nfe_discard_rxbuf(sc, sc->rxq.cur);
			continue;
		}

		m = data->m;
		if (nfe_newbuf(sc, sc->rxq.cur) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			nfe_discard_rxbuf(sc, sc->rxq.cur);
			continue;
		}

		if ((vtag & NFE_RX_VTAG) != 0 &&
		    (if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) != 0) {
			m->m_pkthdr.ether_vtag = vtag & 0xffff;
			m->m_flags |= M_VLANTAG;
		}

		m->m_pkthdr.len = m->m_len = len;
		m->m_pkthdr.rcvif = ifp;

		if ((if_getcapenable(ifp) & IFCAP_RXCSUM) != 0) {
			if ((flags & NFE_RX_IP_CSUMOK) != 0) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
				m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
				if ((flags & NFE_RX_TCP_CSUMOK) != 0 ||
				    (flags & NFE_RX_UDP_CSUMOK) != 0) {
					m->m_pkthdr.csum_flags |=
					    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
					m->m_pkthdr.csum_data = 0xffff;
				}
			}
		}

		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

		NFE_UNLOCK(sc);
		if_input(ifp, m);
		NFE_LOCK(sc);
		rx_npkts++;
	}

	if (prog > 0)
		bus_dmamap_sync(sc->rxq.rx_desc_tag, sc->rxq.rx_desc_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if (rx_npktsp != NULL)
		*rx_npktsp = rx_npkts;
	return (count > 0 ? 0 : EAGAIN);
}


static int
nfe_jrxeof(struct nfe_softc *sc, int count, int *rx_npktsp)
{
	if_t ifp = sc->nfe_ifp;
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_rx_data *data;
	struct mbuf *m;
	uint16_t flags;
	int len, prog, rx_npkts;
	uint32_t vtag = 0;

	rx_npkts = 0;
	NFE_LOCK_ASSERT(sc);

	bus_dmamap_sync(sc->jrxq.jrx_desc_tag, sc->jrxq.jrx_desc_map,
	    BUS_DMASYNC_POSTREAD);

	for (prog = 0;;NFE_INC(sc->jrxq.jcur, NFE_JUMBO_RX_RING_COUNT),
	    vtag = 0) {
		if (count <= 0)
			break;
		count--;

		data = &sc->jrxq.jdata[sc->jrxq.jcur];

		if (sc->nfe_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->jrxq.jdesc64[sc->jrxq.jcur];
			vtag = le32toh(desc64->physaddr[1]);
			flags = le16toh(desc64->flags);
			len = le16toh(desc64->length) & NFE_RX_LEN_MASK;
		} else {
			desc32 = &sc->jrxq.jdesc32[sc->jrxq.jcur];
			flags = le16toh(desc32->flags);
			len = le16toh(desc32->length) & NFE_RX_LEN_MASK;
		}

		if (flags & NFE_RX_READY)
			break;
		prog++;
		if ((sc->nfe_flags & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
			if (!(flags & NFE_RX_VALID_V1)) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				nfe_discard_jrxbuf(sc, sc->jrxq.jcur);
				continue;
			}
			if ((flags & NFE_RX_FIXME_V1) == NFE_RX_FIXME_V1) {
				flags &= ~NFE_RX_ERROR;
				len--;	/* fix buffer length */
			}
		} else {
			if (!(flags & NFE_RX_VALID_V2)) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				nfe_discard_jrxbuf(sc, sc->jrxq.jcur);
				continue;
			}

			if ((flags & NFE_RX_FIXME_V2) == NFE_RX_FIXME_V2) {
				flags &= ~NFE_RX_ERROR;
				len--;	/* fix buffer length */
			}
		}

		if (flags & NFE_RX_ERROR) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			nfe_discard_jrxbuf(sc, sc->jrxq.jcur);
			continue;
		}

		m = data->m;
		if (nfe_jnewbuf(sc, sc->jrxq.jcur) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			nfe_discard_jrxbuf(sc, sc->jrxq.jcur);
			continue;
		}

		if ((vtag & NFE_RX_VTAG) != 0 &&
		    (if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) != 0) {
			m->m_pkthdr.ether_vtag = vtag & 0xffff;
			m->m_flags |= M_VLANTAG;
		}

		m->m_pkthdr.len = m->m_len = len;
		m->m_pkthdr.rcvif = ifp;

		if ((if_getcapenable(ifp) & IFCAP_RXCSUM) != 0) {
			if ((flags & NFE_RX_IP_CSUMOK) != 0) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
				m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
				if ((flags & NFE_RX_TCP_CSUMOK) != 0 ||
				    (flags & NFE_RX_UDP_CSUMOK) != 0) {
					m->m_pkthdr.csum_flags |=
					    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
					m->m_pkthdr.csum_data = 0xffff;
				}
			}
		}

		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

		NFE_UNLOCK(sc);
		if_input(ifp, m);
		NFE_LOCK(sc);
		rx_npkts++;
	}

	if (prog > 0)
		bus_dmamap_sync(sc->jrxq.jrx_desc_tag, sc->jrxq.jrx_desc_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if (rx_npktsp != NULL)
		*rx_npktsp = rx_npkts;
	return (count > 0 ? 0 : EAGAIN);
}


static void
nfe_txeof(struct nfe_softc *sc)
{
	if_t ifp = sc->nfe_ifp;
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_tx_data *data = NULL;
	uint16_t flags;
	int cons, prog;

	NFE_LOCK_ASSERT(sc);

	bus_dmamap_sync(sc->txq.tx_desc_tag, sc->txq.tx_desc_map,
	    BUS_DMASYNC_POSTREAD);

	prog = 0;
	for (cons = sc->txq.next; cons != sc->txq.cur;
	    NFE_INC(cons, NFE_TX_RING_COUNT)) {
		if (sc->nfe_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->txq.desc64[cons];
			flags = le16toh(desc64->flags);
		} else {
			desc32 = &sc->txq.desc32[cons];
			flags = le16toh(desc32->flags);
		}

		if (flags & NFE_TX_VALID)
			break;

		prog++;
		sc->txq.queued--;
		data = &sc->txq.data[cons];

		if ((sc->nfe_flags & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
			if ((flags & NFE_TX_LASTFRAG_V1) == 0)
				continue;
			if ((flags & NFE_TX_ERROR_V1) != 0) {
				device_printf(sc->nfe_dev,
				    "tx v1 error 0x%4b\n", flags, NFE_V1_TXERR);

				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			} else
				if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		} else {
			if ((flags & NFE_TX_LASTFRAG_V2) == 0)
				continue;
			if ((flags & NFE_TX_ERROR_V2) != 0) {
				device_printf(sc->nfe_dev,
				    "tx v2 error 0x%4b\n", flags, NFE_V2_TXERR);
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			} else
				if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		}

		/* last fragment of the mbuf chain transmitted */
		KASSERT(data->m != NULL, ("%s: freeing NULL mbuf!", __func__));
		bus_dmamap_sync(sc->txq.tx_data_tag, data->tx_data_map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->txq.tx_data_tag, data->tx_data_map);
		m_freem(data->m);
		data->m = NULL;
	}

	if (prog > 0) {
		sc->nfe_force_tx = 0;
		sc->txq.next = cons;
		if_setdrvflagbits(ifp, 0, IFF_DRV_OACTIVE);
		if (sc->txq.queued == 0)
			sc->nfe_watchdog_timer = 0;
	}
}

static int
nfe_encap(struct nfe_softc *sc, struct mbuf **m_head)
{
	struct nfe_desc32 *desc32 = NULL;
	struct nfe_desc64 *desc64 = NULL;
	bus_dmamap_t map;
	bus_dma_segment_t segs[NFE_MAX_SCATTER];
	int error, i, nsegs, prod, si;
	uint32_t tsosegsz;
	uint16_t cflags, flags;
	struct mbuf *m;

	prod = si = sc->txq.cur;
	map = sc->txq.data[prod].tx_data_map;

	error = bus_dmamap_load_mbuf_sg(sc->txq.tx_data_tag, map, *m_head, segs,
	    &nsegs, BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, NFE_MAX_SCATTER);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->txq.tx_data_tag, map,
		    *m_head, segs, &nsegs, BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
	} else if (error != 0)
		return (error);
	if (nsegs == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	if (sc->txq.queued + nsegs >= NFE_TX_RING_COUNT - 2) {
		bus_dmamap_unload(sc->txq.tx_data_tag, map);
		return (ENOBUFS);
	}

	m = *m_head;
	cflags = flags = 0;
	tsosegsz = 0;
	if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		tsosegsz = (uint32_t)m->m_pkthdr.tso_segsz <<
		    NFE_TX_TSO_SHIFT;
		cflags &= ~(NFE_TX_IP_CSUM | NFE_TX_TCP_UDP_CSUM);
		cflags |= NFE_TX_TSO;
	} else if ((m->m_pkthdr.csum_flags & NFE_CSUM_FEATURES) != 0) {
		if ((m->m_pkthdr.csum_flags & CSUM_IP) != 0)
			cflags |= NFE_TX_IP_CSUM;
		if ((m->m_pkthdr.csum_flags & CSUM_TCP) != 0)
			cflags |= NFE_TX_TCP_UDP_CSUM;
		if ((m->m_pkthdr.csum_flags & CSUM_UDP) != 0)
			cflags |= NFE_TX_TCP_UDP_CSUM;
	}

	for (i = 0; i < nsegs; i++) {
		if (sc->nfe_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->txq.desc64[prod];
			desc64->physaddr[0] =
			    htole32(NFE_ADDR_HI(segs[i].ds_addr));
			desc64->physaddr[1] =
			    htole32(NFE_ADDR_LO(segs[i].ds_addr));
			desc64->vtag = 0;
			desc64->length = htole16(segs[i].ds_len - 1);
			desc64->flags = htole16(flags);
		} else {
			desc32 = &sc->txq.desc32[prod];
			desc32->physaddr =
			    htole32(NFE_ADDR_LO(segs[i].ds_addr));
			desc32->length = htole16(segs[i].ds_len - 1);
			desc32->flags = htole16(flags);
		}

		/*
		 * Setting of the valid bit in the first descriptor is
		 * deferred until the whole chain is fully setup.
		 */
		flags |= NFE_TX_VALID;

		sc->txq.queued++;
		NFE_INC(prod, NFE_TX_RING_COUNT);
	}

	/*
	 * the whole mbuf chain has been DMA mapped, fix last/first descriptor.
	 * csum flags, vtag and TSO belong to the first fragment only.
	 */
	if (sc->nfe_flags & NFE_40BIT_ADDR) {
		desc64->flags |= htole16(NFE_TX_LASTFRAG_V2);
		desc64 = &sc->txq.desc64[si];
		if ((m->m_flags & M_VLANTAG) != 0)
			desc64->vtag = htole32(NFE_TX_VTAG |
			    m->m_pkthdr.ether_vtag);
		if (tsosegsz != 0) {
			/*
			 * XXX
			 * The following indicates the descriptor element
			 * is a 32bit quantity.
			 */
			desc64->length |= htole16((uint16_t)tsosegsz);
			desc64->flags |= htole16(tsosegsz >> 16);
		}
		/*
		 * finally, set the valid/checksum/TSO bit in the first
		 * descriptor.
		 */
		desc64->flags |= htole16(NFE_TX_VALID | cflags);
	} else {
		if (sc->nfe_flags & NFE_JUMBO_SUP)
			desc32->flags |= htole16(NFE_TX_LASTFRAG_V2);
		else
			desc32->flags |= htole16(NFE_TX_LASTFRAG_V1);
		desc32 = &sc->txq.desc32[si];
		if (tsosegsz != 0) {
			/*
			 * XXX
			 * The following indicates the descriptor element
			 * is a 32bit quantity.
			 */
			desc32->length |= htole16((uint16_t)tsosegsz);
			desc32->flags |= htole16(tsosegsz >> 16);
		}
		/*
		 * finally, set the valid/checksum/TSO bit in the first
		 * descriptor.
		 */
		desc32->flags |= htole16(NFE_TX_VALID | cflags);
	}

	sc->txq.cur = prod;
	prod = (prod + NFE_TX_RING_COUNT - 1) % NFE_TX_RING_COUNT;
	sc->txq.data[si].tx_data_map = sc->txq.data[prod].tx_data_map;
	sc->txq.data[prod].tx_data_map = map;
	sc->txq.data[prod].m = m;

	bus_dmamap_sync(sc->txq.tx_data_tag, map, BUS_DMASYNC_PREWRITE);

	return (0);
}


static void
nfe_setmulti(struct nfe_softc *sc)
{
	if_t ifp = sc->nfe_ifp;
	int i, mc_count, mcnt;
	uint32_t filter;
	uint8_t addr[ETHER_ADDR_LEN], mask[ETHER_ADDR_LEN];
	uint8_t etherbroadcastaddr[ETHER_ADDR_LEN] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	uint8_t *mta;

	NFE_LOCK_ASSERT(sc);

	if ((if_getflags(ifp) & (IFF_ALLMULTI | IFF_PROMISC)) != 0) {
		bzero(addr, ETHER_ADDR_LEN);
		bzero(mask, ETHER_ADDR_LEN);
		goto done;
	}

	bcopy(etherbroadcastaddr, addr, ETHER_ADDR_LEN);
	bcopy(etherbroadcastaddr, mask, ETHER_ADDR_LEN);

	mc_count = if_multiaddr_count(ifp, -1);
	mta = malloc(sizeof(uint8_t) * ETHER_ADDR_LEN * mc_count, M_DEVBUF,
	    M_NOWAIT);

	/* Unable to get memory - process without filtering */
	if (mta == NULL) {
		device_printf(sc->nfe_dev, "nfe_setmulti: failed to allocate"
		    "temp multicast buffer!\n");

		bzero(addr, ETHER_ADDR_LEN);
		bzero(mask, ETHER_ADDR_LEN);
		goto done;
	}

	if_multiaddr_array(ifp, mta, &mcnt, mc_count);

	for (i = 0; i < mcnt; i++) {
		uint8_t *addrp;
		int j;

		addrp = mta + (i * ETHER_ADDR_LEN);
		for (j = 0; j < ETHER_ADDR_LEN; j++) {
			u_int8_t mcaddr = addrp[j];
			addr[j] &= mcaddr;
			mask[j] &= ~mcaddr;
		}
	}

	free(mta, M_DEVBUF);

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		mask[i] |= addr[i];
	}

done:
	addr[0] |= 0x01;	/* make sure multicast bit is set */

	NFE_WRITE(sc, NFE_MULTIADDR_HI,
	    addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0]);
	NFE_WRITE(sc, NFE_MULTIADDR_LO,
	    addr[5] <<  8 | addr[4]);
	NFE_WRITE(sc, NFE_MULTIMASK_HI,
	    mask[3] << 24 | mask[2] << 16 | mask[1] << 8 | mask[0]);
	NFE_WRITE(sc, NFE_MULTIMASK_LO,
	    mask[5] <<  8 | mask[4]);

	filter = NFE_READ(sc, NFE_RXFILTER);
	filter &= NFE_PFF_RX_PAUSE;
	filter |= NFE_RXFILTER_MAGIC;
	filter |= (if_getflags(ifp) & IFF_PROMISC) ? NFE_PFF_PROMISC : NFE_PFF_U2M;
	NFE_WRITE(sc, NFE_RXFILTER, filter);
}


static void
nfe_start(if_t ifp)
{
	struct nfe_softc *sc = if_getsoftc(ifp);

	NFE_LOCK(sc);
	nfe_start_locked(ifp);
	NFE_UNLOCK(sc);
}

static void
nfe_start_locked(if_t ifp)
{
	struct nfe_softc *sc = if_getsoftc(ifp);
	struct mbuf *m0;
	int enq = 0;

	NFE_LOCK_ASSERT(sc);

	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || sc->nfe_link == 0)
		return;

	while (!if_sendq_empty(ifp)) {
		m0 = if_dequeue(ifp);

		if (m0 == NULL)
			break;

		if (nfe_encap(sc, &m0) != 0) {
			if (m0 == NULL)
				break;
			if_sendq_prepend(ifp, m0);
			if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, 0);
			break;
		}
		enq++;
		if_etherbpfmtap(ifp, m0);
	}

	if (enq > 0) {
		bus_dmamap_sync(sc->txq.tx_desc_tag, sc->txq.tx_desc_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		/* kick Tx */
		NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_KICKTX | sc->rxtxctl);

		/*
		 * Set a timeout in case the chip goes out to lunch.
		 */
		sc->nfe_watchdog_timer = 5;
	}
}


static void
nfe_watchdog(if_t ifp)
{
	struct nfe_softc *sc = if_getsoftc(ifp);

	if (sc->nfe_watchdog_timer == 0 || --sc->nfe_watchdog_timer)
		return;

	/* Check if we've lost Tx completion interrupt. */
	nfe_txeof(sc);
	if (sc->txq.queued == 0) {
		if_printf(ifp, "watchdog timeout (missed Tx interrupts) "
		    "-- recovering\n");
		if (!if_sendq_empty(ifp))
			nfe_start_locked(ifp);
		return;
	}
	/* Check if we've lost start Tx command. */
	sc->nfe_force_tx++;
	if (sc->nfe_force_tx <= 3) {
		/*
		 * If this is the case for watchdog timeout, the following
		 * code should go to nfe_txeof().
		 */
		NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_KICKTX | sc->rxtxctl);
		return;
	}
	sc->nfe_force_tx = 0;

	if_printf(ifp, "watchdog timeout\n");

	if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	nfe_init_locked(sc);
}


static void
nfe_init(void *xsc)
{
	struct nfe_softc *sc = xsc;

	NFE_LOCK(sc);
	nfe_init_locked(sc);
	NFE_UNLOCK(sc);
}


static void
nfe_init_locked(void *xsc)
{
	struct nfe_softc *sc = xsc;
	if_t ifp = sc->nfe_ifp;
	struct mii_data *mii;
	uint32_t val;
	int error;

	NFE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->nfe_miibus);

	if (if_getdrvflags(ifp) & IFF_DRV_RUNNING)
		return;

	nfe_stop(ifp);

	sc->nfe_framesize = if_getmtu(ifp) + NFE_RX_HEADERS;

	nfe_init_tx_ring(sc, &sc->txq);
	if (sc->nfe_framesize > (MCLBYTES - ETHER_HDR_LEN))
		error = nfe_init_jrx_ring(sc, &sc->jrxq);
	else
		error = nfe_init_rx_ring(sc, &sc->rxq);
	if (error != 0) {
		device_printf(sc->nfe_dev,
		    "initialization failed: no memory for rx buffers\n");
		nfe_stop(ifp);
		return;
	}

	val = 0;
	if ((sc->nfe_flags & NFE_CORRECT_MACADDR) != 0)
		val |= NFE_MAC_ADDR_INORDER;
	NFE_WRITE(sc, NFE_TX_UNK, val);
	NFE_WRITE(sc, NFE_STATUS, 0);

	if ((sc->nfe_flags & NFE_TX_FLOW_CTRL) != 0)
		NFE_WRITE(sc, NFE_TX_PAUSE_FRAME, NFE_TX_PAUSE_FRAME_DISABLE);

	sc->rxtxctl = NFE_RXTX_BIT2;
	if (sc->nfe_flags & NFE_40BIT_ADDR)
		sc->rxtxctl |= NFE_RXTX_V3MAGIC;
	else if (sc->nfe_flags & NFE_JUMBO_SUP)
		sc->rxtxctl |= NFE_RXTX_V2MAGIC;

	if ((if_getcapenable(ifp) & IFCAP_RXCSUM) != 0)
		sc->rxtxctl |= NFE_RXTX_RXCSUM;
	if ((if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) != 0)
		sc->rxtxctl |= NFE_RXTX_VTAG_INSERT | NFE_RXTX_VTAG_STRIP;

	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_RESET | sc->rxtxctl);
	DELAY(10);
	NFE_WRITE(sc, NFE_RXTX_CTL, sc->rxtxctl);

	if ((if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) != 0)
		NFE_WRITE(sc, NFE_VTAG_CTL, NFE_VTAG_ENABLE);
	else
		NFE_WRITE(sc, NFE_VTAG_CTL, 0);

	NFE_WRITE(sc, NFE_SETUP_R6, 0);

	/* set MAC address */
	nfe_set_macaddr(sc, if_getlladdr(ifp));

	/* tell MAC where rings are in memory */
	if (sc->nfe_framesize > MCLBYTES - ETHER_HDR_LEN) {
		NFE_WRITE(sc, NFE_RX_RING_ADDR_HI,
		    NFE_ADDR_HI(sc->jrxq.jphysaddr));
		NFE_WRITE(sc, NFE_RX_RING_ADDR_LO,
		    NFE_ADDR_LO(sc->jrxq.jphysaddr));
	} else {
		NFE_WRITE(sc, NFE_RX_RING_ADDR_HI,
		    NFE_ADDR_HI(sc->rxq.physaddr));
		NFE_WRITE(sc, NFE_RX_RING_ADDR_LO,
		    NFE_ADDR_LO(sc->rxq.physaddr));
	}
	NFE_WRITE(sc, NFE_TX_RING_ADDR_HI, NFE_ADDR_HI(sc->txq.physaddr));
	NFE_WRITE(sc, NFE_TX_RING_ADDR_LO, NFE_ADDR_LO(sc->txq.physaddr));

	NFE_WRITE(sc, NFE_RING_SIZE,
	    (NFE_RX_RING_COUNT - 1) << 16 |
	    (NFE_TX_RING_COUNT - 1));

	NFE_WRITE(sc, NFE_RXBUFSZ, sc->nfe_framesize);

	/* force MAC to wakeup */
	val = NFE_READ(sc, NFE_PWR_STATE);
	if ((val & NFE_PWR_WAKEUP) == 0)
		NFE_WRITE(sc, NFE_PWR_STATE, val | NFE_PWR_WAKEUP);
	DELAY(10);
	val = NFE_READ(sc, NFE_PWR_STATE);
	NFE_WRITE(sc, NFE_PWR_STATE, val | NFE_PWR_VALID);

#if 1
	/* configure interrupts coalescing/mitigation */
	NFE_WRITE(sc, NFE_IMTIMER, NFE_IM_DEFAULT);
#else
	/* no interrupt mitigation: one interrupt per packet */
	NFE_WRITE(sc, NFE_IMTIMER, 970);
#endif

	NFE_WRITE(sc, NFE_SETUP_R1, NFE_R1_MAGIC_10_100);
	NFE_WRITE(sc, NFE_SETUP_R2, NFE_R2_MAGIC);
	NFE_WRITE(sc, NFE_SETUP_R6, NFE_R6_MAGIC);

	/* update MAC knowledge of PHY; generates a NFE_IRQ_LINK interrupt */
	NFE_WRITE(sc, NFE_STATUS, sc->mii_phyaddr << 24 | NFE_STATUS_MAGIC);

	NFE_WRITE(sc, NFE_SETUP_R4, NFE_R4_MAGIC);
	/* Disable WOL. */
	NFE_WRITE(sc, NFE_WOL_CTL, 0);

	sc->rxtxctl &= ~NFE_RXTX_BIT2;
	NFE_WRITE(sc, NFE_RXTX_CTL, sc->rxtxctl);
	DELAY(10);
	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_BIT1 | sc->rxtxctl);

	/* set Rx filter */
	nfe_setmulti(sc);

	/* enable Rx */
	NFE_WRITE(sc, NFE_RX_CTL, NFE_RX_START);

	/* enable Tx */
	NFE_WRITE(sc, NFE_TX_CTL, NFE_TX_START);

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	/* Clear hardware stats. */
	nfe_stats_clear(sc);

#ifdef DEVICE_POLLING
	if (if_getcapenable(ifp) & IFCAP_POLLING)
		nfe_disable_intr(sc);
	else
#endif
	nfe_set_intr(sc);
	nfe_enable_intr(sc); /* enable interrupts */

	if_setdrvflagbits(ifp, IFF_DRV_RUNNING, 0);
	if_setdrvflagbits(ifp, 0, IFF_DRV_OACTIVE);

	sc->nfe_link = 0;
	mii_mediachg(mii);

	callout_reset(&sc->nfe_stat_ch, hz, nfe_tick, sc);
}


static void
nfe_stop(if_t ifp)
{
	struct nfe_softc *sc = if_getsoftc(ifp);
	struct nfe_rx_ring *rx_ring;
	struct nfe_jrx_ring *jrx_ring;
	struct nfe_tx_ring *tx_ring;
	struct nfe_rx_data *rdata;
	struct nfe_tx_data *tdata;
	int i;

	NFE_LOCK_ASSERT(sc);

	sc->nfe_watchdog_timer = 0;
	if_setdrvflagbits(ifp, 0, (IFF_DRV_RUNNING | IFF_DRV_OACTIVE));

	callout_stop(&sc->nfe_stat_ch);

	/* abort Tx */
	NFE_WRITE(sc, NFE_TX_CTL, 0);

	/* disable Rx */
	NFE_WRITE(sc, NFE_RX_CTL, 0);

	/* disable interrupts */
	nfe_disable_intr(sc);

	sc->nfe_link = 0;

	/* free Rx and Tx mbufs still in the queues. */
	rx_ring = &sc->rxq;
	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		rdata = &rx_ring->data[i];
		if (rdata->m != NULL) {
			bus_dmamap_sync(rx_ring->rx_data_tag,
			    rdata->rx_data_map, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(rx_ring->rx_data_tag,
			    rdata->rx_data_map);
			m_freem(rdata->m);
			rdata->m = NULL;
		}
	}

	if ((sc->nfe_flags & NFE_JUMBO_SUP) != 0) {
		jrx_ring = &sc->jrxq;
		for (i = 0; i < NFE_JUMBO_RX_RING_COUNT; i++) {
			rdata = &jrx_ring->jdata[i];
			if (rdata->m != NULL) {
				bus_dmamap_sync(jrx_ring->jrx_data_tag,
				    rdata->rx_data_map, BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(jrx_ring->jrx_data_tag,
				    rdata->rx_data_map);
				m_freem(rdata->m);
				rdata->m = NULL;
			}
		}
	}

	tx_ring = &sc->txq;
	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		tdata = &tx_ring->data[i];
		if (tdata->m != NULL) {
			bus_dmamap_sync(tx_ring->tx_data_tag,
			    tdata->tx_data_map, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(tx_ring->tx_data_tag,
			    tdata->tx_data_map);
			m_freem(tdata->m);
			tdata->m = NULL;
		}
	}
	/* Update hardware stats. */
	nfe_stats_update(sc);
}


static int
nfe_ifmedia_upd(if_t ifp)
{
	struct nfe_softc *sc = if_getsoftc(ifp);
	struct mii_data *mii;

	NFE_LOCK(sc);
	mii = device_get_softc(sc->nfe_miibus);
	mii_mediachg(mii);
	NFE_UNLOCK(sc);

	return (0);
}


static void
nfe_ifmedia_sts(if_t ifp, struct ifmediareq *ifmr)
{
	struct nfe_softc *sc;
	struct mii_data *mii;

	sc = if_getsoftc(ifp);

	NFE_LOCK(sc);
	mii = device_get_softc(sc->nfe_miibus);
	mii_pollstat(mii);

	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	NFE_UNLOCK(sc);
}


void
nfe_tick(void *xsc)
{
	struct nfe_softc *sc;
	struct mii_data *mii;
	if_t ifp;

	sc = (struct nfe_softc *)xsc;

	NFE_LOCK_ASSERT(sc);

	ifp = sc->nfe_ifp;

	mii = device_get_softc(sc->nfe_miibus);
	mii_tick(mii);
	nfe_stats_update(sc);
	nfe_watchdog(ifp);
	callout_reset(&sc->nfe_stat_ch, hz, nfe_tick, sc);
}


static int
nfe_shutdown(device_t dev)
{

	return (nfe_suspend(dev));
}


static void
nfe_get_macaddr(struct nfe_softc *sc, uint8_t *addr)
{
	uint32_t val;

	if ((sc->nfe_flags & NFE_CORRECT_MACADDR) == 0) {
		val = NFE_READ(sc, NFE_MACADDR_LO);
		addr[0] = (val >> 8) & 0xff;
		addr[1] = (val & 0xff);

		val = NFE_READ(sc, NFE_MACADDR_HI);
		addr[2] = (val >> 24) & 0xff;
		addr[3] = (val >> 16) & 0xff;
		addr[4] = (val >>  8) & 0xff;
		addr[5] = (val & 0xff);
	} else {
		val = NFE_READ(sc, NFE_MACADDR_LO);
		addr[5] = (val >> 8) & 0xff;
		addr[4] = (val & 0xff);

		val = NFE_READ(sc, NFE_MACADDR_HI);
		addr[3] = (val >> 24) & 0xff;
		addr[2] = (val >> 16) & 0xff;
		addr[1] = (val >>  8) & 0xff;
		addr[0] = (val & 0xff);
	}
}


static void
nfe_set_macaddr(struct nfe_softc *sc, uint8_t *addr)
{

	NFE_WRITE(sc, NFE_MACADDR_LO, addr[5] <<  8 | addr[4]);
	NFE_WRITE(sc, NFE_MACADDR_HI, addr[3] << 24 | addr[2] << 16 |
	    addr[1] << 8 | addr[0]);
}


/*
 * Map a single buffer address.
 */

static void
nfe_dma_map_segs(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct nfe_dmamap_arg *ctx;

	if (error != 0)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));

	ctx = (struct nfe_dmamap_arg *)arg;
	ctx->nfe_busaddr = segs[0].ds_addr;
}


static int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	if (!arg1)
		return (EINVAL);
	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || !req->newptr)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
	*(int *)arg1 = value;

	return (0);
}


static int
sysctl_hw_nfe_proc_limit(SYSCTL_HANDLER_ARGS)
{

	return (sysctl_int_range(oidp, arg1, arg2, req, NFE_PROC_MIN,
	    NFE_PROC_MAX));
}


#define	NFE_SYSCTL_STAT_ADD32(c, h, n, p, d)	\
	    SYSCTL_ADD_UINT(c, h, OID_AUTO, n, CTLFLAG_RD, p, 0, d)
#define	NFE_SYSCTL_STAT_ADD64(c, h, n, p, d)	\
	    SYSCTL_ADD_UQUAD(c, h, OID_AUTO, n, CTLFLAG_RD, p, d)

static void
nfe_sysctl_node(struct nfe_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child, *parent;
	struct sysctl_oid *tree;
	struct nfe_hw_stats *stats;
	int error;

	stats = &sc->nfe_stats;
	ctx = device_get_sysctl_ctx(sc->nfe_dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->nfe_dev));
	SYSCTL_ADD_PROC(ctx, child,
	    OID_AUTO, "process_limit", CTLTYPE_INT | CTLFLAG_RW,
	    &sc->nfe_process_limit, 0, sysctl_hw_nfe_proc_limit, "I",
	    "max number of Rx events to process");

	sc->nfe_process_limit = NFE_PROC_DEFAULT;
	error = resource_int_value(device_get_name(sc->nfe_dev),
	    device_get_unit(sc->nfe_dev), "process_limit",
	    &sc->nfe_process_limit);
	if (error == 0) {
		if (sc->nfe_process_limit < NFE_PROC_MIN ||
		    sc->nfe_process_limit > NFE_PROC_MAX) {
			device_printf(sc->nfe_dev,
			    "process_limit value out of range; "
			    "using default: %d\n", NFE_PROC_DEFAULT);
			sc->nfe_process_limit = NFE_PROC_DEFAULT;
		}
	}

	if ((sc->nfe_flags & (NFE_MIB_V1 | NFE_MIB_V2 | NFE_MIB_V3)) == 0)
		return;

	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "NFE statistics");
	parent = SYSCTL_CHILDREN(tree);

	/* Rx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "Rx MAC statistics");
	child = SYSCTL_CHILDREN(tree);

	NFE_SYSCTL_STAT_ADD32(ctx, child, "frame_errors",
	    &stats->rx_frame_errors, "Framing Errors");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "extra_bytes",
	    &stats->rx_extra_bytes, "Extra Bytes");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "late_cols",
	    &stats->rx_late_cols, "Late Collisions");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "runts",
	    &stats->rx_runts, "Runts");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "jumbos",
	    &stats->rx_jumbos, "Jumbos");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "fifo_overuns",
	    &stats->rx_fifo_overuns, "FIFO Overruns");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "crc_errors",
	    &stats->rx_crc_errors, "CRC Errors");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "fae",
	    &stats->rx_fae, "Frame Alignment Errors");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "len_errors",
	    &stats->rx_len_errors, "Length Errors");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "unicast",
	    &stats->rx_unicast, "Unicast Frames");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "multicast",
	    &stats->rx_multicast, "Multicast Frames");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "broadcast",
	    &stats->rx_broadcast, "Broadcast Frames");
	if ((sc->nfe_flags & NFE_MIB_V2) != 0) {
		NFE_SYSCTL_STAT_ADD64(ctx, child, "octets",
		    &stats->rx_octets, "Octets");
		NFE_SYSCTL_STAT_ADD32(ctx, child, "pause",
		    &stats->rx_pause, "Pause frames");
		NFE_SYSCTL_STAT_ADD32(ctx, child, "drops",
		    &stats->rx_drops, "Drop frames");
	}

	/* Tx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "Tx MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	NFE_SYSCTL_STAT_ADD64(ctx, child, "octets",
	    &stats->tx_octets, "Octets");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "zero_rexmits",
	    &stats->tx_zero_rexmits, "Zero Retransmits");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "one_rexmits",
	    &stats->tx_one_rexmits, "One Retransmits");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "multi_rexmits",
	    &stats->tx_multi_rexmits, "Multiple Retransmits");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "late_cols",
	    &stats->tx_late_cols, "Late Collisions");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "fifo_underuns",
	    &stats->tx_fifo_underuns, "FIFO Underruns");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "carrier_losts",
	    &stats->tx_carrier_losts, "Carrier Losts");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "excess_deferrals",
	    &stats->tx_excess_deferals, "Excess Deferrals");
	NFE_SYSCTL_STAT_ADD32(ctx, child, "retry_errors",
	    &stats->tx_retry_errors, "Retry Errors");
	if ((sc->nfe_flags & NFE_MIB_V2) != 0) {
		NFE_SYSCTL_STAT_ADD32(ctx, child, "deferrals",
		    &stats->tx_deferals, "Deferrals");
		NFE_SYSCTL_STAT_ADD32(ctx, child, "frames",
		    &stats->tx_frames, "Frames");
		NFE_SYSCTL_STAT_ADD32(ctx, child, "pause",
		    &stats->tx_pause, "Pause Frames");
	}
	if ((sc->nfe_flags & NFE_MIB_V3) != 0) {
		NFE_SYSCTL_STAT_ADD32(ctx, child, "unicast",
		    &stats->tx_deferals, "Unicast Frames");
		NFE_SYSCTL_STAT_ADD32(ctx, child, "multicast",
		    &stats->tx_frames, "Multicast Frames");
		NFE_SYSCTL_STAT_ADD32(ctx, child, "broadcast",
		    &stats->tx_pause, "Broadcast Frames");
	}
}

#undef NFE_SYSCTL_STAT_ADD32
#undef NFE_SYSCTL_STAT_ADD64

static void
nfe_stats_clear(struct nfe_softc *sc)
{
	int i, mib_cnt;

	if ((sc->nfe_flags & NFE_MIB_V1) != 0)
		mib_cnt = NFE_NUM_MIB_STATV1;
	else if ((sc->nfe_flags & (NFE_MIB_V2 | NFE_MIB_V3)) != 0)
		mib_cnt = NFE_NUM_MIB_STATV2;
	else
		return;

	for (i = 0; i < mib_cnt; i++)
		NFE_READ(sc, NFE_TX_OCTET + i * sizeof(uint32_t));

	if ((sc->nfe_flags & NFE_MIB_V3) != 0) {
		NFE_READ(sc, NFE_TX_UNICAST);
		NFE_READ(sc, NFE_TX_MULTICAST);
		NFE_READ(sc, NFE_TX_BROADCAST);
	}
}

static void
nfe_stats_update(struct nfe_softc *sc)
{
	struct nfe_hw_stats *stats;

	NFE_LOCK_ASSERT(sc);

	if ((sc->nfe_flags & (NFE_MIB_V1 | NFE_MIB_V2 | NFE_MIB_V3)) == 0)
		return;

	stats = &sc->nfe_stats;
	stats->tx_octets += NFE_READ(sc, NFE_TX_OCTET);
	stats->tx_zero_rexmits += NFE_READ(sc, NFE_TX_ZERO_REXMIT);
	stats->tx_one_rexmits += NFE_READ(sc, NFE_TX_ONE_REXMIT);
	stats->tx_multi_rexmits += NFE_READ(sc, NFE_TX_MULTI_REXMIT);
	stats->tx_late_cols += NFE_READ(sc, NFE_TX_LATE_COL);
	stats->tx_fifo_underuns += NFE_READ(sc, NFE_TX_FIFO_UNDERUN);
	stats->tx_carrier_losts += NFE_READ(sc, NFE_TX_CARRIER_LOST);
	stats->tx_excess_deferals += NFE_READ(sc, NFE_TX_EXCESS_DEFERRAL);
	stats->tx_retry_errors += NFE_READ(sc, NFE_TX_RETRY_ERROR);
	stats->rx_frame_errors += NFE_READ(sc, NFE_RX_FRAME_ERROR);
	stats->rx_extra_bytes += NFE_READ(sc, NFE_RX_EXTRA_BYTES);
	stats->rx_late_cols += NFE_READ(sc, NFE_RX_LATE_COL);
	stats->rx_runts += NFE_READ(sc, NFE_RX_RUNT);
	stats->rx_jumbos += NFE_READ(sc, NFE_RX_JUMBO);
	stats->rx_fifo_overuns += NFE_READ(sc, NFE_RX_FIFO_OVERUN);
	stats->rx_crc_errors += NFE_READ(sc, NFE_RX_CRC_ERROR);
	stats->rx_fae += NFE_READ(sc, NFE_RX_FAE);
	stats->rx_len_errors += NFE_READ(sc, NFE_RX_LEN_ERROR);
	stats->rx_unicast += NFE_READ(sc, NFE_RX_UNICAST);
	stats->rx_multicast += NFE_READ(sc, NFE_RX_MULTICAST);
	stats->rx_broadcast += NFE_READ(sc, NFE_RX_BROADCAST);

	if ((sc->nfe_flags & NFE_MIB_V2) != 0) {
		stats->tx_deferals += NFE_READ(sc, NFE_TX_DEFERAL);
		stats->tx_frames += NFE_READ(sc, NFE_TX_FRAME);
		stats->rx_octets += NFE_READ(sc, NFE_RX_OCTET);
		stats->tx_pause += NFE_READ(sc, NFE_TX_PAUSE);
		stats->rx_pause += NFE_READ(sc, NFE_RX_PAUSE);
		stats->rx_drops += NFE_READ(sc, NFE_RX_DROP);
	}

	if ((sc->nfe_flags & NFE_MIB_V3) != 0) {
		stats->tx_unicast += NFE_READ(sc, NFE_TX_UNICAST);
		stats->tx_multicast += NFE_READ(sc, NFE_TX_MULTICAST);
		stats->tx_broadcast += NFE_READ(sc, NFE_TX_BROADCAST);
	}
}


static void
nfe_set_linkspeed(struct nfe_softc *sc)
{
	struct mii_softc *miisc;
	struct mii_data *mii;
	int aneg, i, phyno;

	NFE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->nfe_miibus);
	mii_pollstat(mii);
	aneg = 0;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch IFM_SUBTYPE(mii->mii_media_active) {
		case IFM_10_T:
		case IFM_100_TX:
			return;
		case IFM_1000_T:
			aneg++;
			break;
		default:
			break;
		}
	}
	miisc = LIST_FIRST(&mii->mii_phys);
	phyno = miisc->mii_phy;
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	nfe_miibus_writereg(sc->nfe_dev, phyno, MII_100T2CR, 0);
	nfe_miibus_writereg(sc->nfe_dev, phyno,
	    MII_ANAR, ANAR_TX_FD | ANAR_TX | ANAR_10_FD | ANAR_10 | ANAR_CSMA);
	nfe_miibus_writereg(sc->nfe_dev, phyno,
	    MII_BMCR, BMCR_RESET | BMCR_AUTOEN | BMCR_STARTNEG);
	DELAY(1000);
	if (aneg != 0) {
		/*
		 * Poll link state until nfe(4) get a 10/100Mbps link.
		 */
		for (i = 0; i < MII_ANEGTICKS_GIGE; i++) {
			mii_pollstat(mii);
			if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID))
			    == (IFM_ACTIVE | IFM_AVALID)) {
				switch (IFM_SUBTYPE(mii->mii_media_active)) {
				case IFM_10_T:
				case IFM_100_TX:
					nfe_mac_config(sc, mii);
					return;
				default:
					break;
				}
			}
			NFE_UNLOCK(sc);
			pause("nfelnk", hz);
			NFE_LOCK(sc);
		}
		if (i == MII_ANEGTICKS_GIGE)
			device_printf(sc->nfe_dev,
			    "establishing a link failed, WOL may not work!");
	}
	/*
	 * No link, force MAC to have 100Mbps, full-duplex link.
	 * This is the last resort and may/may not work.
	 */
	mii->mii_media_status = IFM_AVALID | IFM_ACTIVE;
	mii->mii_media_active = IFM_ETHER | IFM_100_TX | IFM_FDX;
	nfe_mac_config(sc, mii);
}


static void
nfe_set_wol(struct nfe_softc *sc)
{
	if_t ifp;
	uint32_t wolctl;
	int pmc;
	uint16_t pmstat;

	NFE_LOCK_ASSERT(sc);

	if (pci_find_cap(sc->nfe_dev, PCIY_PMG, &pmc) != 0)
		return;
	ifp = sc->nfe_ifp;
	if ((if_getcapenable(ifp) & IFCAP_WOL_MAGIC) != 0)
		wolctl = NFE_WOL_MAGIC;
	else
		wolctl = 0;
	NFE_WRITE(sc, NFE_WOL_CTL, wolctl);
	if ((if_getcapenable(ifp) & IFCAP_WOL_MAGIC) != 0) {
		nfe_set_linkspeed(sc);
		if ((sc->nfe_flags & NFE_PWR_MGMT) != 0)
			NFE_WRITE(sc, NFE_PWR2_CTL,
			    NFE_READ(sc, NFE_PWR2_CTL) & ~NFE_PWR2_GATE_CLOCKS);
		/* Enable RX. */
		NFE_WRITE(sc, NFE_RX_RING_ADDR_HI, 0);
		NFE_WRITE(sc, NFE_RX_RING_ADDR_LO, 0);
		NFE_WRITE(sc, NFE_RX_CTL, NFE_READ(sc, NFE_RX_CTL) |
		    NFE_RX_START);
	}
	/* Request PME if WOL is requested. */
	pmstat = pci_read_config(sc->nfe_dev, pmc + PCIR_POWER_STATUS, 2);
	pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
	if ((if_getcapenable(ifp) & IFCAP_WOL) != 0)
		pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
	pci_write_config(sc->nfe_dev, pmc + PCIR_POWER_STATUS, pmstat, 2);
}

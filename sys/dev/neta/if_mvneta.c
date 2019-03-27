/*
 * Copyright (c) 2017 Stormshield.
 * Copyright (c) 2017 Semihalf.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_platform.h"
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/smp.h>
#include <sys/taskqueue.h>
#ifdef MVNETA_KTR
#include <sys/ktr.h>
#endif

#include <net/ethernet.h>
#include <net/bpf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp_lro.h>

#include <sys/sockio.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mdio/mdio.h>

#include <arm/mv/mvvar.h>

#if !defined(__aarch64__)
#include <arm/mv/mvreg.h>
#include <arm/mv/mvwin.h>
#endif

#include "if_mvnetareg.h"
#include "if_mvnetavar.h"

#include "miibus_if.h"
#include "mdio_if.h"

#ifdef MVNETA_DEBUG
#define	STATIC /* nothing */
#else
#define	STATIC static
#endif

#define	DASSERT(x) KASSERT((x), (#x))

#define	A3700_TCLK_250MHZ		250000000

/* Device Register Initialization */
STATIC int mvneta_initreg(struct ifnet *);

/* Descriptor Ring Control for each of queues */
STATIC int mvneta_ring_alloc_rx_queue(struct mvneta_softc *, int);
STATIC int mvneta_ring_alloc_tx_queue(struct mvneta_softc *, int);
STATIC void mvneta_ring_dealloc_rx_queue(struct mvneta_softc *, int);
STATIC void mvneta_ring_dealloc_tx_queue(struct mvneta_softc *, int);
STATIC int mvneta_ring_init_rx_queue(struct mvneta_softc *, int);
STATIC int mvneta_ring_init_tx_queue(struct mvneta_softc *, int);
STATIC void mvneta_ring_flush_rx_queue(struct mvneta_softc *, int);
STATIC void mvneta_ring_flush_tx_queue(struct mvneta_softc *, int);
STATIC void mvneta_dmamap_cb(void *, bus_dma_segment_t *, int, int);
STATIC int mvneta_dma_create(struct mvneta_softc *);

/* Rx/Tx Queue Control */
STATIC int mvneta_rx_queue_init(struct ifnet *, int);
STATIC int mvneta_tx_queue_init(struct ifnet *, int);
STATIC int mvneta_rx_queue_enable(struct ifnet *, int);
STATIC int mvneta_tx_queue_enable(struct ifnet *, int);
STATIC void mvneta_rx_lockq(struct mvneta_softc *, int);
STATIC void mvneta_rx_unlockq(struct mvneta_softc *, int);
STATIC void mvneta_tx_lockq(struct mvneta_softc *, int);
STATIC void mvneta_tx_unlockq(struct mvneta_softc *, int);

/* Interrupt Handlers */
STATIC void mvneta_disable_intr(struct mvneta_softc *);
STATIC void mvneta_enable_intr(struct mvneta_softc *);
STATIC void mvneta_rxtxth_intr(void *);
STATIC int mvneta_misc_intr(struct mvneta_softc *);
STATIC void mvneta_tick(void *);
/* struct ifnet and mii callbacks*/
STATIC int mvneta_xmitfast_locked(struct mvneta_softc *, int, struct mbuf **);
STATIC int mvneta_xmit_locked(struct mvneta_softc *, int);
#ifdef MVNETA_MULTIQUEUE
STATIC int mvneta_transmit(struct ifnet *, struct mbuf *);
#else /* !MVNETA_MULTIQUEUE */
STATIC void mvneta_start(struct ifnet *);
#endif
STATIC void mvneta_qflush(struct ifnet *);
STATIC void mvneta_tx_task(void *, int);
STATIC int mvneta_ioctl(struct ifnet *, u_long, caddr_t);
STATIC void mvneta_init(void *);
STATIC void mvneta_init_locked(void *);
STATIC void mvneta_stop(struct mvneta_softc *);
STATIC void mvneta_stop_locked(struct mvneta_softc *);
STATIC int mvneta_mediachange(struct ifnet *);
STATIC void mvneta_mediastatus(struct ifnet *, struct ifmediareq *);
STATIC void mvneta_portup(struct mvneta_softc *);
STATIC void mvneta_portdown(struct mvneta_softc *);

/* Link State Notify */
STATIC void mvneta_update_autoneg(struct mvneta_softc *, int);
STATIC int mvneta_update_media(struct mvneta_softc *, int);
STATIC void mvneta_adjust_link(struct mvneta_softc *);
STATIC void mvneta_update_eee(struct mvneta_softc *);
STATIC void mvneta_update_fc(struct mvneta_softc *);
STATIC void mvneta_link_isr(struct mvneta_softc *);
STATIC void mvneta_linkupdate(struct mvneta_softc *, boolean_t);
STATIC void mvneta_linkup(struct mvneta_softc *);
STATIC void mvneta_linkdown(struct mvneta_softc *);
STATIC void mvneta_linkreset(struct mvneta_softc *);

/* Tx Subroutines */
STATIC int mvneta_tx_queue(struct mvneta_softc *, struct mbuf **, int);
STATIC void mvneta_tx_set_csumflag(struct ifnet *,
    struct mvneta_tx_desc *, struct mbuf *);
STATIC void mvneta_tx_queue_complete(struct mvneta_softc *, int);
STATIC void mvneta_tx_drain(struct mvneta_softc *);

/* Rx Subroutines */
STATIC int mvneta_rx(struct mvneta_softc *, int, int);
STATIC void mvneta_rx_queue(struct mvneta_softc *, int, int);
STATIC void mvneta_rx_queue_refill(struct mvneta_softc *, int);
STATIC void mvneta_rx_set_csumflag(struct ifnet *,
    struct mvneta_rx_desc *, struct mbuf *);
STATIC void mvneta_rx_buf_free(struct mvneta_softc *, struct mvneta_buf *);

/* MAC address filter */
STATIC void mvneta_filter_setup(struct mvneta_softc *);

/* sysctl(9) */
STATIC int sysctl_read_mib(SYSCTL_HANDLER_ARGS);
STATIC int sysctl_clear_mib(SYSCTL_HANDLER_ARGS);
STATIC int sysctl_set_queue_rxthtime(SYSCTL_HANDLER_ARGS);
STATIC void sysctl_mvneta_init(struct mvneta_softc *);

/* MIB */
STATIC void mvneta_clear_mib(struct mvneta_softc *);
STATIC void mvneta_update_mib(struct mvneta_softc *);

/* Switch */
STATIC boolean_t mvneta_find_ethernet_prop_switch(phandle_t, phandle_t);
STATIC boolean_t mvneta_has_switch(device_t);

#define	mvneta_sc_lock(sc) mtx_lock(&sc->mtx)
#define	mvneta_sc_unlock(sc) mtx_unlock(&sc->mtx)

STATIC struct mtx mii_mutex;
STATIC int mii_init = 0;

/* Device */
STATIC int mvneta_detach(device_t);
/* MII */
STATIC int mvneta_miibus_readreg(device_t, int, int);
STATIC int mvneta_miibus_writereg(device_t, int, int, int);

/* Clock */
STATIC uint32_t mvneta_get_clk(void);

static device_method_t mvneta_methods[] = {
	/* Device interface */
	DEVMETHOD(device_detach,	mvneta_detach),
	/* MII interface */
	DEVMETHOD(miibus_readreg,       mvneta_miibus_readreg),
	DEVMETHOD(miibus_writereg,      mvneta_miibus_writereg),
	/* MDIO interface */
	DEVMETHOD(mdio_readreg,		mvneta_miibus_readreg),
	DEVMETHOD(mdio_writereg,	mvneta_miibus_writereg),

	/* End */
	DEVMETHOD_END
};

DEFINE_CLASS_0(mvneta, mvneta_driver, mvneta_methods, sizeof(struct mvneta_softc));

DRIVER_MODULE(miibus, mvneta, miibus_driver, miibus_devclass, 0, 0);
DRIVER_MODULE(mdio, mvneta, mdio_driver, mdio_devclass, 0, 0);
MODULE_DEPEND(mvneta, mdio, 1, 1, 1);
MODULE_DEPEND(mvneta, ether, 1, 1, 1);
MODULE_DEPEND(mvneta, miibus, 1, 1, 1);
MODULE_DEPEND(mvneta, mvxpbm, 1, 1, 1);

/*
 * List of MIB register and names
 */
enum mvneta_mib_idx
{
	MVNETA_MIB_RX_GOOD_OCT_IDX,
	MVNETA_MIB_RX_BAD_OCT_IDX,
	MVNETA_MIB_TX_MAC_TRNS_ERR_IDX,
	MVNETA_MIB_RX_GOOD_FRAME_IDX,
	MVNETA_MIB_RX_BAD_FRAME_IDX,
	MVNETA_MIB_RX_BCAST_FRAME_IDX,
	MVNETA_MIB_RX_MCAST_FRAME_IDX,
	MVNETA_MIB_RX_FRAME64_OCT_IDX,
	MVNETA_MIB_RX_FRAME127_OCT_IDX,
	MVNETA_MIB_RX_FRAME255_OCT_IDX,
	MVNETA_MIB_RX_FRAME511_OCT_IDX,
	MVNETA_MIB_RX_FRAME1023_OCT_IDX,
	MVNETA_MIB_RX_FRAMEMAX_OCT_IDX,
	MVNETA_MIB_TX_GOOD_OCT_IDX,
	MVNETA_MIB_TX_GOOD_FRAME_IDX,
	MVNETA_MIB_TX_EXCES_COL_IDX,
	MVNETA_MIB_TX_MCAST_FRAME_IDX,
	MVNETA_MIB_TX_BCAST_FRAME_IDX,
	MVNETA_MIB_TX_MAC_CTL_ERR_IDX,
	MVNETA_MIB_FC_SENT_IDX,
	MVNETA_MIB_FC_GOOD_IDX,
	MVNETA_MIB_FC_BAD_IDX,
	MVNETA_MIB_PKT_UNDERSIZE_IDX,
	MVNETA_MIB_PKT_FRAGMENT_IDX,
	MVNETA_MIB_PKT_OVERSIZE_IDX,
	MVNETA_MIB_PKT_JABBER_IDX,
	MVNETA_MIB_MAC_RX_ERR_IDX,
	MVNETA_MIB_MAC_CRC_ERR_IDX,
	MVNETA_MIB_MAC_COL_IDX,
	MVNETA_MIB_MAC_LATE_COL_IDX,
};

STATIC struct mvneta_mib_def {
	uint32_t regnum;
	int reg64;
	const char *sysctl_name;
	const char *desc;
} mvneta_mib_list[] = {
	[MVNETA_MIB_RX_GOOD_OCT_IDX] = {MVNETA_MIB_RX_GOOD_OCT, 1,
	    "rx_good_oct", "Good Octets Rx"},
	[MVNETA_MIB_RX_BAD_OCT_IDX] = {MVNETA_MIB_RX_BAD_OCT, 0,
	    "rx_bad_oct", "Bad  Octets Rx"},
	[MVNETA_MIB_TX_MAC_TRNS_ERR_IDX] = {MVNETA_MIB_TX_MAC_TRNS_ERR, 0,
	    "tx_mac_err", "MAC Transmit Error"},
	[MVNETA_MIB_RX_GOOD_FRAME_IDX] = {MVNETA_MIB_RX_GOOD_FRAME, 0,
	    "rx_good_frame", "Good Frames Rx"},
	[MVNETA_MIB_RX_BAD_FRAME_IDX] = {MVNETA_MIB_RX_BAD_FRAME, 0,
	    "rx_bad_frame", "Bad Frames Rx"},
	[MVNETA_MIB_RX_BCAST_FRAME_IDX] = {MVNETA_MIB_RX_BCAST_FRAME, 0,
	    "rx_bcast_frame", "Broadcast Frames Rx"},
	[MVNETA_MIB_RX_MCAST_FRAME_IDX] = {MVNETA_MIB_RX_MCAST_FRAME, 0,
	    "rx_mcast_frame", "Multicast Frames Rx"},
	[MVNETA_MIB_RX_FRAME64_OCT_IDX] = {MVNETA_MIB_RX_FRAME64_OCT, 0,
	    "rx_frame_1_64", "Frame Size    1 -   64"},
	[MVNETA_MIB_RX_FRAME127_OCT_IDX] = {MVNETA_MIB_RX_FRAME127_OCT, 0,
	    "rx_frame_65_127", "Frame Size   65 -  127"},
	[MVNETA_MIB_RX_FRAME255_OCT_IDX] = {MVNETA_MIB_RX_FRAME255_OCT, 0,
	    "rx_frame_128_255", "Frame Size  128 -  255"},
	[MVNETA_MIB_RX_FRAME511_OCT_IDX] = {MVNETA_MIB_RX_FRAME511_OCT, 0,
	    "rx_frame_256_511", "Frame Size  256 -  511"},
	[MVNETA_MIB_RX_FRAME1023_OCT_IDX] = {MVNETA_MIB_RX_FRAME1023_OCT, 0,
	    "rx_frame_512_1023", "Frame Size  512 - 1023"},
	[MVNETA_MIB_RX_FRAMEMAX_OCT_IDX] = {MVNETA_MIB_RX_FRAMEMAX_OCT, 0,
	    "rx_fame_1024_max", "Frame Size 1024 -  Max"},
	[MVNETA_MIB_TX_GOOD_OCT_IDX] = {MVNETA_MIB_TX_GOOD_OCT, 1,
	    "tx_good_oct", "Good Octets Tx"},
	[MVNETA_MIB_TX_GOOD_FRAME_IDX] = {MVNETA_MIB_TX_GOOD_FRAME, 0,
	    "tx_good_frame", "Good Frames Tx"},
	[MVNETA_MIB_TX_EXCES_COL_IDX] = {MVNETA_MIB_TX_EXCES_COL, 0,
	    "tx_exces_collision", "Excessive Collision"},
	[MVNETA_MIB_TX_MCAST_FRAME_IDX] = {MVNETA_MIB_TX_MCAST_FRAME, 0,
	    "tx_mcast_frame", "Multicast Frames Tx"},
	[MVNETA_MIB_TX_BCAST_FRAME_IDX] = {MVNETA_MIB_TX_BCAST_FRAME, 0,
	    "tx_bcast_frame", "Broadcast Frames Tx"},
	[MVNETA_MIB_TX_MAC_CTL_ERR_IDX] = {MVNETA_MIB_TX_MAC_CTL_ERR, 0,
	    "tx_mac_ctl_err", "Unknown MAC Control"},
	[MVNETA_MIB_FC_SENT_IDX] = {MVNETA_MIB_FC_SENT, 0,
	    "fc_tx", "Flow Control Tx"},
	[MVNETA_MIB_FC_GOOD_IDX] = {MVNETA_MIB_FC_GOOD, 0,
	    "fc_rx_good", "Good Flow Control Rx"},
	[MVNETA_MIB_FC_BAD_IDX] = {MVNETA_MIB_FC_BAD, 0,
	    "fc_rx_bad", "Bad Flow Control Rx"},
	[MVNETA_MIB_PKT_UNDERSIZE_IDX] = {MVNETA_MIB_PKT_UNDERSIZE, 0,
	    "pkt_undersize", "Undersized Packets Rx"},
	[MVNETA_MIB_PKT_FRAGMENT_IDX] = {MVNETA_MIB_PKT_FRAGMENT, 0,
	    "pkt_fragment", "Fragmented Packets Rx"},
	[MVNETA_MIB_PKT_OVERSIZE_IDX] = {MVNETA_MIB_PKT_OVERSIZE, 0,
	    "pkt_oversize", "Oversized Packets Rx"},
	[MVNETA_MIB_PKT_JABBER_IDX] = {MVNETA_MIB_PKT_JABBER, 0,
	    "pkt_jabber", "Jabber Packets Rx"},
	[MVNETA_MIB_MAC_RX_ERR_IDX] = {MVNETA_MIB_MAC_RX_ERR, 0,
	    "mac_rx_err", "MAC Rx Errors"},
	[MVNETA_MIB_MAC_CRC_ERR_IDX] = {MVNETA_MIB_MAC_CRC_ERR, 0,
	    "mac_crc_err", "MAC CRC Errors"},
	[MVNETA_MIB_MAC_COL_IDX] = {MVNETA_MIB_MAC_COL, 0,
	    "mac_collision", "MAC Collision"},
	[MVNETA_MIB_MAC_LATE_COL_IDX] = {MVNETA_MIB_MAC_LATE_COL, 0,
	    "mac_late_collision", "MAC Late Collision"},
};

static struct resource_spec res_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ SYS_RES_IRQ, 0, RF_ACTIVE },
	{ -1, 0}
};

static struct {
	driver_intr_t *handler;
	char * description;
} mvneta_intrs[] = {
	{ mvneta_rxtxth_intr, "MVNETA aggregated interrupt" },
};

STATIC uint32_t
mvneta_get_clk()
{
#if defined(__aarch64__)
	return (A3700_TCLK_250MHZ);
#else
	return (get_tclk());
#endif
}

static int
mvneta_set_mac_address(struct mvneta_softc *sc, uint8_t *addr)
{
	unsigned int mac_h;
	unsigned int mac_l;

	mac_l = (addr[4] << 8) | (addr[5]);
	mac_h = (addr[0] << 24) | (addr[1] << 16) |
	    (addr[2] << 8) | (addr[3] << 0);

	MVNETA_WRITE(sc, MVNETA_MACAL, mac_l);
	MVNETA_WRITE(sc, MVNETA_MACAH, mac_h);
	return (0);
}

static int
mvneta_get_mac_address(struct mvneta_softc *sc, uint8_t *addr)
{
	uint32_t mac_l, mac_h;

#ifdef FDT
	if (mvneta_fdt_mac_address(sc, addr) == 0)
		return (0);
#endif
	/*
	 * Fall back -- use the currently programmed address.
	 */
	mac_l = MVNETA_READ(sc, MVNETA_MACAL);
	mac_h = MVNETA_READ(sc, MVNETA_MACAH);
	if (mac_l == 0 && mac_h == 0) {
		/*
		 * Generate pseudo-random MAC.
		 * Set lower part to random number | unit number.
		 */
		mac_l = arc4random() & ~0xff;
		mac_l |= device_get_unit(sc->dev) & 0xff;
		mac_h = arc4random();
		mac_h &= ~(3 << 24);	/* Clear multicast and LAA bits */
		if (bootverbose) {
			device_printf(sc->dev,
			    "Could not acquire MAC address. "
			    "Using randomized one.\n");
		}
	}

	addr[0] = (mac_h & 0xff000000) >> 24;
	addr[1] = (mac_h & 0x00ff0000) >> 16;
	addr[2] = (mac_h & 0x0000ff00) >> 8;
	addr[3] = (mac_h & 0x000000ff);
	addr[4] = (mac_l & 0x0000ff00) >> 8;
	addr[5] = (mac_l & 0x000000ff);
	return (0);
}

STATIC boolean_t
mvneta_find_ethernet_prop_switch(phandle_t ethernet, phandle_t node)
{
	boolean_t ret;
	phandle_t child, switch_eth_handle, switch_eth;

	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if (OF_getencprop(child, "ethernet", (void*)&switch_eth_handle,
		    sizeof(switch_eth_handle)) > 0) {
			if (switch_eth_handle > 0) {
				switch_eth = OF_node_from_xref(
				    switch_eth_handle);

				if (switch_eth == ethernet)
					return (true);
			}
		}

		ret = mvneta_find_ethernet_prop_switch(ethernet, child);
		if (ret != 0)
			return (ret);
	}

	return (false);
}

STATIC boolean_t
mvneta_has_switch(device_t self)
{
	phandle_t node;

	node = ofw_bus_get_node(self);

	return mvneta_find_ethernet_prop_switch(node, OF_finddevice("/"));
}

STATIC int
mvneta_dma_create(struct mvneta_softc *sc)
{
	size_t maxsize, maxsegsz;
	size_t q;
	int error;

	/*
	 * Create Tx DMA
	 */
	maxsize = maxsegsz = sizeof(struct mvneta_tx_desc) * MVNETA_TX_RING_CNT;

	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->dev),		/* parent */
	    16, 0,                              /* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,            /* lowaddr */
	    BUS_SPACE_MAXADDR,                  /* highaddr */
	    NULL, NULL,                         /* filtfunc, filtfuncarg */
	    maxsize,				/* maxsize */
	    1,					/* nsegments */
	    maxsegsz,				/* maxsegsz */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &sc->tx_dtag);			/* dmat */
	if (error != 0) {
		device_printf(sc->dev,
		    "Failed to create DMA tag for Tx descriptors.\n");
		goto fail;
	}
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->dev),		/* parent */
	    1, 0,				/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    MVNETA_PACKET_SIZE,			/* maxsize */
	    MVNETA_TX_SEGLIMIT,			/* nsegments */
	    MVNETA_PACKET_SIZE,			/* maxsegsz */
	    BUS_DMA_ALLOCNOW,			/* flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &sc->txmbuf_dtag);
	if (error != 0) {
		device_printf(sc->dev,
		    "Failed to create DMA tag for Tx mbufs.\n");
		goto fail;
	}

	for (q = 0; q < MVNETA_TX_QNUM_MAX; q++) {
		error = mvneta_ring_alloc_tx_queue(sc, q);
		if (error != 0) {
			device_printf(sc->dev,
			    "Failed to allocate DMA safe memory for TxQ: %zu\n", q);
			goto fail;
		}
	}

	/*
	 * Create Rx DMA.
	 */
	/* Create tag for Rx descripors */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->dev),		/* parent */
	    32, 0,                              /* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,            /* lowaddr */
	    BUS_SPACE_MAXADDR,                  /* highaddr */
	    NULL, NULL,                         /* filtfunc, filtfuncarg */
	    sizeof(struct mvneta_rx_desc) * MVNETA_RX_RING_CNT, /* maxsize */
	    1,					/* nsegments */
	    sizeof(struct mvneta_rx_desc) * MVNETA_RX_RING_CNT, /* maxsegsz */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &sc->rx_dtag);			/* dmat */
	if (error != 0) {
		device_printf(sc->dev,
		    "Failed to create DMA tag for Rx descriptors.\n");
		goto fail;
	}

	/* Create tag for Rx buffers */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->dev),		/* parent */
	    32, 0,				/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    MVNETA_PACKET_SIZE, 1,		/* maxsize, nsegments */
	    MVNETA_PACKET_SIZE,			/* maxsegsz */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &sc->rxbuf_dtag);			/* dmat */
	if (error != 0) {
		device_printf(sc->dev,
		    "Failed to create DMA tag for Rx buffers.\n");
		goto fail;
	}

	for (q = 0; q < MVNETA_RX_QNUM_MAX; q++) {
		if (mvneta_ring_alloc_rx_queue(sc, q) != 0) {
			device_printf(sc->dev,
			    "Failed to allocate DMA safe memory for RxQ: %zu\n", q);
			goto fail;
		}
	}

	return (0);
fail:
	mvneta_detach(sc->dev);

	return (error);
}

/* ARGSUSED */
int
mvneta_attach(device_t self)
{
	struct mvneta_softc *sc;
	struct ifnet *ifp;
	device_t child;
	int ifm_target;
	int q, error;
#if !defined(__aarch64__)
	uint32_t reg;
#endif

	sc = device_get_softc(self);
	sc->dev = self;

	mtx_init(&sc->mtx, "mvneta_sc", NULL, MTX_DEF);

	error = bus_alloc_resources(self, res_spec, sc->res);
	if (error) {
		device_printf(self, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->version = MVNETA_READ(sc, MVNETA_PV);
	device_printf(self, "version is %x\n", sc->version);
	callout_init(&sc->tick_ch, 0);

	/*
	 * make sure DMA engines are in reset state
	 */
	MVNETA_WRITE(sc, MVNETA_PRXINIT, 0x00000001);
	MVNETA_WRITE(sc, MVNETA_PTXINIT, 0x00000001);

#if !defined(__aarch64__)
	/*
	 * Disable port snoop for buffers and descriptors
	 * to avoid L2 caching of both without DRAM copy.
	 * Obtain coherency settings from the first MBUS
	 * window attribute.
	 */
	if ((MVNETA_READ(sc, MV_WIN_NETA_BASE(0)) & IO_WIN_COH_ATTR_MASK) == 0) {
		reg = MVNETA_READ(sc, MVNETA_PSNPCFG);
		reg &= ~MVNETA_PSNPCFG_DESCSNP_MASK;
		reg &= ~MVNETA_PSNPCFG_BUFSNP_MASK;
		MVNETA_WRITE(sc, MVNETA_PSNPCFG, reg);
	}
#endif

	/*
	 * MAC address
	 */
	if (mvneta_get_mac_address(sc, sc->enaddr)) {
		device_printf(self, "no mac address.\n");
		return (ENXIO);
	}
	mvneta_set_mac_address(sc, sc->enaddr);

	mvneta_disable_intr(sc);

	/* Allocate network interface */
	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(self, "if_alloc() failed\n");
		mvneta_detach(self);
		return (ENOMEM);
	}
	if_initname(ifp, device_get_name(self), device_get_unit(self));

	/*
	 * We can support 802.1Q VLAN-sized frames and jumbo
	 * Ethernet frames.
	 */
	ifp->if_capabilities |= IFCAP_VLAN_MTU | IFCAP_JUMBO_MTU;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
#ifdef MVNETA_MULTIQUEUE
	ifp->if_transmit = mvneta_transmit;
	ifp->if_qflush = mvneta_qflush;
#else /* !MVNETA_MULTIQUEUE */
	ifp->if_start = mvneta_start;
	ifp->if_snd.ifq_drv_maxlen = MVNETA_TX_RING_CNT - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);
#endif
	ifp->if_init = mvneta_init;
	ifp->if_ioctl = mvneta_ioctl;

	/*
	 * We can do IPv4/TCPv4/UDPv4/TCPv6/UDPv6 checksums in hardware.
	 */
	ifp->if_capabilities |= IFCAP_HWCSUM;

	/*
	 * As VLAN hardware tagging is not supported
	 * but is necessary to perform VLAN hardware checksums,
	 * it is done in the driver
	 */
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWCSUM;

	/*
	 * Currently IPv6 HW checksum is broken, so make sure it is disabled.
	 */
	ifp->if_capabilities &= ~IFCAP_HWCSUM_IPV6;
	ifp->if_capenable = ifp->if_capabilities;

	/*
	 * Disabled option(s):
	 * - Support for Large Receive Offload
	 */
	ifp->if_capabilities |= IFCAP_LRO;

	ifp->if_hwassist = CSUM_IP | CSUM_TCP | CSUM_UDP;

	/*
	 * Device DMA Buffer allocation.
	 * Handles resource deallocation in case of failure.
	 */
	error = mvneta_dma_create(sc);
	if (error != 0) {
		mvneta_detach(self);
		return (error);
	}

	/* Initialize queues */
	for (q = 0; q < MVNETA_TX_QNUM_MAX; q++) {
		error = mvneta_ring_init_tx_queue(sc, q);
		if (error != 0) {
			mvneta_detach(self);
			return (error);
		}
	}

	for (q = 0; q < MVNETA_RX_QNUM_MAX; q++) {
		error = mvneta_ring_init_rx_queue(sc, q);
		if (error != 0) {
			mvneta_detach(self);
			return (error);
		}
	}

	ether_ifattach(ifp, sc->enaddr);

	/*
	 * Enable DMA engines and Initialize Device Registers.
	 */
	MVNETA_WRITE(sc, MVNETA_PRXINIT, 0x00000000);
	MVNETA_WRITE(sc, MVNETA_PTXINIT, 0x00000000);
	MVNETA_WRITE(sc, MVNETA_PACC, MVNETA_PACC_ACCELERATIONMODE_EDM);
	mvneta_sc_lock(sc);
	mvneta_filter_setup(sc);
	mvneta_sc_unlock(sc);
	mvneta_initreg(ifp);

	/*
	 * Now MAC is working, setup MII.
	 */
	if (mii_init == 0) {
		/*
		 * MII bus is shared by all MACs and all PHYs in SoC.
		 * serializing the bus access should be safe.
		 */
		mtx_init(&mii_mutex, "mvneta_mii", NULL, MTX_DEF);
		mii_init = 1;
	}

	/* Attach PHY(s) */
	if ((sc->phy_addr != MII_PHY_ANY) && (!sc->use_inband_status)) {
		error = mii_attach(self, &sc->miibus, ifp, mvneta_mediachange,
		    mvneta_mediastatus, BMSR_DEFCAPMASK, sc->phy_addr,
		    MII_OFFSET_ANY, 0);
		if (error != 0) {
			if (bootverbose) {
				device_printf(self,
				    "MII attach failed, error: %d\n", error);
			}
			ether_ifdetach(sc->ifp);
			mvneta_detach(self);
			return (error);
		}
		sc->mii = device_get_softc(sc->miibus);
		sc->phy_attached = 1;

		/* Disable auto-negotiation in MAC - rely on PHY layer */
		mvneta_update_autoneg(sc, FALSE);
	} else if (sc->use_inband_status == TRUE) {
		/* In-band link status */
		ifmedia_init(&sc->mvneta_ifmedia, 0, mvneta_mediachange,
		    mvneta_mediastatus);

		/* Configure media */
		ifmedia_add(&sc->mvneta_ifmedia, IFM_ETHER | IFM_1000_T | IFM_FDX,
		    0, NULL);
		ifmedia_add(&sc->mvneta_ifmedia, IFM_ETHER | IFM_100_TX, 0, NULL);
		ifmedia_add(&sc->mvneta_ifmedia, IFM_ETHER | IFM_100_TX | IFM_FDX,
		    0, NULL);
		ifmedia_add(&sc->mvneta_ifmedia, IFM_ETHER | IFM_10_T, 0, NULL);
		ifmedia_add(&sc->mvneta_ifmedia, IFM_ETHER | IFM_10_T | IFM_FDX,
		    0, NULL);
		ifmedia_add(&sc->mvneta_ifmedia, IFM_ETHER | IFM_AUTO, 0, NULL);
		ifmedia_set(&sc->mvneta_ifmedia, IFM_ETHER | IFM_AUTO);

		/* Enable auto-negotiation */
		mvneta_update_autoneg(sc, TRUE);

		mvneta_sc_lock(sc);
		if (MVNETA_IS_LINKUP(sc))
			mvneta_linkup(sc);
		else
			mvneta_linkdown(sc);
		mvneta_sc_unlock(sc);

	} else {
		/* Fixed-link, use predefined values */
		mvneta_update_autoneg(sc, FALSE);
		ifmedia_init(&sc->mvneta_ifmedia, 0, mvneta_mediachange,
		    mvneta_mediastatus);

		ifm_target = IFM_ETHER;
		switch (sc->phy_speed) {
		case 2500:
			if (sc->phy_mode != MVNETA_PHY_SGMII &&
			    sc->phy_mode != MVNETA_PHY_QSGMII) {
				device_printf(self,
				    "2.5G speed can work only in (Q)SGMII mode\n");
				ether_ifdetach(sc->ifp);
				mvneta_detach(self);
				return (ENXIO);
			}
			ifm_target |= IFM_2500_T;
			break;
		case 1000:
			ifm_target |= IFM_1000_T;
			break;
		case 100:
			ifm_target |= IFM_100_TX;
			break;
		case 10:
			ifm_target |= IFM_10_T;
			break;
		default:
			ether_ifdetach(sc->ifp);
			mvneta_detach(self);
			return (ENXIO);
		}

		if (sc->phy_fdx)
			ifm_target |= IFM_FDX;
		else
			ifm_target |= IFM_HDX;

		ifmedia_add(&sc->mvneta_ifmedia, ifm_target, 0, NULL);
		ifmedia_set(&sc->mvneta_ifmedia, ifm_target);
		if_link_state_change(sc->ifp, LINK_STATE_UP);

		if (mvneta_has_switch(self)) {
			if (bootverbose)
				device_printf(self, "This device is attached to a switch\n");
			child = device_add_child(sc->dev, "mdio", -1);
			if (child == NULL) {
				ether_ifdetach(sc->ifp);
				mvneta_detach(self);
				return (ENXIO);
			}
			bus_generic_attach(sc->dev);
			bus_generic_attach(child);
		}

		/* Configure MAC media */
		mvneta_update_media(sc, ifm_target);
	}

	sysctl_mvneta_init(sc);

	callout_reset(&sc->tick_ch, 0, mvneta_tick, sc);

	error = bus_setup_intr(self, sc->res[1],
	    INTR_TYPE_NET | INTR_MPSAFE, NULL, mvneta_intrs[0].handler, sc,
	    &sc->ih_cookie[0]);
	if (error) {
		device_printf(self, "could not setup %s\n",
		    mvneta_intrs[0].description);
		ether_ifdetach(sc->ifp);
		mvneta_detach(self);
		return (error);
	}

	return (0);
}

STATIC int
mvneta_detach(device_t dev)
{
	struct mvneta_softc *sc;
	int q;

	sc = device_get_softc(dev);

	mvneta_stop(sc);
	/* Detach network interface */
	if (sc->ifp)
		if_free(sc->ifp);

	for (q = 0; q < MVNETA_RX_QNUM_MAX; q++)
		mvneta_ring_dealloc_rx_queue(sc, q);
	for (q = 0; q < MVNETA_TX_QNUM_MAX; q++)
		mvneta_ring_dealloc_tx_queue(sc, q);

	if (sc->tx_dtag != NULL)
		bus_dma_tag_destroy(sc->tx_dtag);
	if (sc->rx_dtag != NULL)
		bus_dma_tag_destroy(sc->rx_dtag);
	if (sc->txmbuf_dtag != NULL)
		bus_dma_tag_destroy(sc->txmbuf_dtag);

	bus_release_resources(dev, res_spec, sc->res);
	return (0);
}

/*
 * MII
 */
STATIC int
mvneta_miibus_readreg(device_t dev, int phy, int reg)
{
	struct mvneta_softc *sc;
	struct ifnet *ifp;
	uint32_t smi, val;
	int i;

	sc = device_get_softc(dev);
	ifp = sc->ifp;

	mtx_lock(&mii_mutex);

	for (i = 0; i < MVNETA_PHY_TIMEOUT; i++) {
		if ((MVNETA_READ(sc, MVNETA_SMI) & MVNETA_SMI_BUSY) == 0)
			break;
		DELAY(1);
	}
	if (i == MVNETA_PHY_TIMEOUT) {
		if_printf(ifp, "SMI busy timeout\n");
		mtx_unlock(&mii_mutex);
		return (-1);
	}

	smi = MVNETA_SMI_PHYAD(phy) |
	    MVNETA_SMI_REGAD(reg) | MVNETA_SMI_OPCODE_READ;
	MVNETA_WRITE(sc, MVNETA_SMI, smi);

	for (i = 0; i < MVNETA_PHY_TIMEOUT; i++) {
		if ((MVNETA_READ(sc, MVNETA_SMI) & MVNETA_SMI_BUSY) == 0)
			break;
		DELAY(1);
	}

	if (i == MVNETA_PHY_TIMEOUT) {
		if_printf(ifp, "SMI busy timeout\n");
		mtx_unlock(&mii_mutex);
		return (-1);
	}
	for (i = 0; i < MVNETA_PHY_TIMEOUT; i++) {
		smi = MVNETA_READ(sc, MVNETA_SMI);
		if (smi & MVNETA_SMI_READVALID)
			break;
		DELAY(1);
	}

	if (i == MVNETA_PHY_TIMEOUT) {
		if_printf(ifp, "SMI busy timeout\n");
		mtx_unlock(&mii_mutex);
		return (-1);
	}

	mtx_unlock(&mii_mutex);

#ifdef MVNETA_KTR
	CTR3(KTR_SPARE2, "%s i=%d, timeout=%d\n", ifp->if_xname, i,
	    MVNETA_PHY_TIMEOUT);
#endif

	val = smi & MVNETA_SMI_DATA_MASK;

#ifdef MVNETA_KTR
	CTR4(KTR_SPARE2, "%s phy=%d, reg=%#x, val=%#x\n", ifp->if_xname, phy,
	    reg, val);
#endif
	return (val);
}

STATIC int
mvneta_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct mvneta_softc *sc;
	struct ifnet *ifp;
	uint32_t smi;
	int i;

	sc = device_get_softc(dev);
	ifp = sc->ifp;
#ifdef MVNETA_KTR
	CTR4(KTR_SPARE2, "%s phy=%d, reg=%#x, val=%#x\n", ifp->if_xname,
	    phy, reg, val);
#endif

	mtx_lock(&mii_mutex);

	for (i = 0; i < MVNETA_PHY_TIMEOUT; i++) {
		if ((MVNETA_READ(sc, MVNETA_SMI) & MVNETA_SMI_BUSY) == 0)
			break;
		DELAY(1);
	}
	if (i == MVNETA_PHY_TIMEOUT) {
		if_printf(ifp, "SMI busy timeout\n");
		mtx_unlock(&mii_mutex);
		return (0);
	}

	smi = MVNETA_SMI_PHYAD(phy) | MVNETA_SMI_REGAD(reg) |
	    MVNETA_SMI_OPCODE_WRITE | (val & MVNETA_SMI_DATA_MASK);
	MVNETA_WRITE(sc, MVNETA_SMI, smi);

	for (i = 0; i < MVNETA_PHY_TIMEOUT; i++) {
		if ((MVNETA_READ(sc, MVNETA_SMI) & MVNETA_SMI_BUSY) == 0)
			break;
		DELAY(1);
	}

	mtx_unlock(&mii_mutex);

	if (i == MVNETA_PHY_TIMEOUT)
		if_printf(ifp, "phy write timed out\n");

	return (0);
}

STATIC void
mvneta_portup(struct mvneta_softc *sc)
{
	int q;

	for (q = 0; q < MVNETA_RX_QNUM_MAX; q++) {
		mvneta_rx_lockq(sc, q);
		mvneta_rx_queue_enable(sc->ifp, q);
		mvneta_rx_unlockq(sc, q);
	}

	for (q = 0; q < MVNETA_TX_QNUM_MAX; q++) {
		mvneta_tx_lockq(sc, q);
		mvneta_tx_queue_enable(sc->ifp, q);
		mvneta_tx_unlockq(sc, q);
	}

}

STATIC void
mvneta_portdown(struct mvneta_softc *sc)
{
	struct mvneta_rx_ring *rx;
	struct mvneta_tx_ring *tx;
	int q, cnt;
	uint32_t reg;

	for (q = 0; q < MVNETA_RX_QNUM_MAX; q++) {
		rx = MVNETA_RX_RING(sc, q);
		mvneta_rx_lockq(sc, q);
		rx->queue_status = MVNETA_QUEUE_DISABLED;
		mvneta_rx_unlockq(sc, q);
	}

	for (q = 0; q < MVNETA_TX_QNUM_MAX; q++) {
		tx = MVNETA_TX_RING(sc, q);
		mvneta_tx_lockq(sc, q);
		tx->queue_status = MVNETA_QUEUE_DISABLED;
		mvneta_tx_unlockq(sc, q);
	}

	/* Wait for all Rx activity to terminate. */
	reg = MVNETA_READ(sc, MVNETA_RQC) & MVNETA_RQC_EN_MASK;
	reg = MVNETA_RQC_DIS(reg);
	MVNETA_WRITE(sc, MVNETA_RQC, reg);
	cnt = 0;
	do {
		if (cnt >= RX_DISABLE_TIMEOUT) {
			if_printf(sc->ifp,
			    "timeout for RX stopped. rqc 0x%x\n", reg);
			break;
		}
		cnt++;
		reg = MVNETA_READ(sc, MVNETA_RQC);
	} while ((reg & MVNETA_RQC_EN_MASK) != 0);

	/* Wait for all Tx activity to terminate. */
	reg  = MVNETA_READ(sc, MVNETA_PIE);
	reg &= ~MVNETA_PIE_TXPKTINTRPTENB_MASK;
	MVNETA_WRITE(sc, MVNETA_PIE, reg);

	reg  = MVNETA_READ(sc, MVNETA_PRXTXTIM);
	reg &= ~MVNETA_PRXTXTI_TBTCQ_MASK;
	MVNETA_WRITE(sc, MVNETA_PRXTXTIM, reg);

	reg = MVNETA_READ(sc, MVNETA_TQC) & MVNETA_TQC_EN_MASK;
	reg = MVNETA_TQC_DIS(reg);
	MVNETA_WRITE(sc, MVNETA_TQC, reg);
	cnt = 0;
	do {
		if (cnt >= TX_DISABLE_TIMEOUT) {
			if_printf(sc->ifp,
			    "timeout for TX stopped. tqc 0x%x\n", reg);
			break;
		}
		cnt++;
		reg = MVNETA_READ(sc, MVNETA_TQC);
	} while ((reg & MVNETA_TQC_EN_MASK) != 0);

	/* Wait for all Tx FIFO is empty */
	cnt = 0;
	do {
		if (cnt >= TX_FIFO_EMPTY_TIMEOUT) {
			if_printf(sc->ifp,
			    "timeout for TX FIFO drained. ps0 0x%x\n", reg);
			break;
		}
		cnt++;
		reg = MVNETA_READ(sc, MVNETA_PS0);
	} while (((reg & MVNETA_PS0_TXFIFOEMP) == 0) &&
	    ((reg & MVNETA_PS0_TXINPROG) != 0));
}

/*
 * Device Register Initialization
 *  reset device registers to device driver default value.
 *  the device is not enabled here.
 */
STATIC int
mvneta_initreg(struct ifnet *ifp)
{
	struct mvneta_softc *sc;
	int q, i;
	uint32_t reg;

	sc = ifp->if_softc;
#ifdef MVNETA_KTR
	CTR1(KTR_SPARE2, "%s initializing device register", ifp->if_xname);
#endif

	/* Disable Legacy WRR, Disable EJP, Release from reset. */
	MVNETA_WRITE(sc, MVNETA_TQC_1, 0);
	/* Enable mbus retry. */
	MVNETA_WRITE(sc, MVNETA_MBUS_CONF, MVNETA_MBUS_RETRY_EN);

	/* Init TX/RX Queue Registers */
	for (q = 0; q < MVNETA_RX_QNUM_MAX; q++) {
		mvneta_rx_lockq(sc, q);
		if (mvneta_rx_queue_init(ifp, q) != 0) {
			device_printf(sc->dev,
			    "initialization failed: cannot initialize queue\n");
			mvneta_rx_unlockq(sc, q);
			return (ENOBUFS);
		}
		mvneta_rx_unlockq(sc, q);
	}
	for (q = 0; q < MVNETA_TX_QNUM_MAX; q++) {
		mvneta_tx_lockq(sc, q);
		if (mvneta_tx_queue_init(ifp, q) != 0) {
			device_printf(sc->dev,
			    "initialization failed: cannot initialize queue\n");
			mvneta_tx_unlockq(sc, q);
			return (ENOBUFS);
		}
		mvneta_tx_unlockq(sc, q);
	}

	/*
	 * Ethernet Unit Control - disable automatic PHY management by HW.
	 * In case the port uses SMI-controlled PHY, poll its status with
	 * mii_tick() and update MAC settings accordingly.
	 */
	reg = MVNETA_READ(sc, MVNETA_EUC);
	reg &= ~MVNETA_EUC_POLLING;
	MVNETA_WRITE(sc, MVNETA_EUC, reg);

	/* EEE: Low Power Idle */
	reg  = MVNETA_LPIC0_LILIMIT(MVNETA_LPI_LI);
	reg |= MVNETA_LPIC0_TSLIMIT(MVNETA_LPI_TS);
	MVNETA_WRITE(sc, MVNETA_LPIC0, reg);

	reg  = MVNETA_LPIC1_TWLIMIT(MVNETA_LPI_TW);
	MVNETA_WRITE(sc, MVNETA_LPIC1, reg);

	reg = MVNETA_LPIC2_MUSTSET;
	MVNETA_WRITE(sc, MVNETA_LPIC2, reg);

	/* Port MAC Control set 0 */
	reg  = MVNETA_PMACC0_MUSTSET;	/* must write 0x1 */
	reg &= ~MVNETA_PMACC0_PORTEN;	/* port is still disabled */
	reg |= MVNETA_PMACC0_FRAMESIZELIMIT(MVNETA_MAX_FRAME);
	MVNETA_WRITE(sc, MVNETA_PMACC0, reg);

	/* Port MAC Control set 2 */
	reg = MVNETA_READ(sc, MVNETA_PMACC2);
	switch (sc->phy_mode) {
	case MVNETA_PHY_QSGMII:
		reg |= (MVNETA_PMACC2_PCSEN | MVNETA_PMACC2_RGMIIEN);
		MVNETA_WRITE(sc, MVNETA_PSERDESCFG, MVNETA_PSERDESCFG_QSGMII);
		break;
	case MVNETA_PHY_SGMII:
		reg |= (MVNETA_PMACC2_PCSEN | MVNETA_PMACC2_RGMIIEN);
		MVNETA_WRITE(sc, MVNETA_PSERDESCFG, MVNETA_PSERDESCFG_SGMII);
		break;
	case MVNETA_PHY_RGMII:
	case MVNETA_PHY_RGMII_ID:
		reg |= MVNETA_PMACC2_RGMIIEN;
		break;
	}
	reg |= MVNETA_PMACC2_MUSTSET;
	reg &= ~MVNETA_PMACC2_PORTMACRESET;
	MVNETA_WRITE(sc, MVNETA_PMACC2, reg);

	/* Port Configuration Extended: enable Tx CRC generation */
	reg = MVNETA_READ(sc, MVNETA_PXCX);
	reg &= ~MVNETA_PXCX_TXCRCDIS;
	MVNETA_WRITE(sc, MVNETA_PXCX, reg);

	/* clear MIB counter registers(clear by read) */
	for (i = 0; i < nitems(mvneta_mib_list); i++) {
		if (mvneta_mib_list[i].reg64)
			MVNETA_READ_MIB_8(sc, mvneta_mib_list[i].regnum);
		else
			MVNETA_READ_MIB_4(sc, mvneta_mib_list[i].regnum);
	}
	MVNETA_READ(sc, MVNETA_PDFC);
	MVNETA_READ(sc, MVNETA_POFC);

	/* Set SDC register except IPGINT bits */
	reg  = MVNETA_SDC_RXBSZ_16_64BITWORDS;
	reg |= MVNETA_SDC_TXBSZ_16_64BITWORDS;
	reg |= MVNETA_SDC_BLMR;
	reg |= MVNETA_SDC_BLMT;
	MVNETA_WRITE(sc, MVNETA_SDC, reg);

	return (0);
}

STATIC void
mvneta_dmamap_cb(void *arg, bus_dma_segment_t * segs, int nseg, int error)
{

	if (error != 0)
		return;
	*(bus_addr_t *)arg = segs->ds_addr;
}

STATIC int
mvneta_ring_alloc_rx_queue(struct mvneta_softc *sc, int q)
{
	struct mvneta_rx_ring *rx;
	struct mvneta_buf *rxbuf;
	bus_dmamap_t dmap;
	int i, error;

	if (q >= MVNETA_RX_QNUM_MAX)
		return (EINVAL);

	rx = MVNETA_RX_RING(sc, q);
	mtx_init(&rx->ring_mtx, "mvneta_rx", NULL, MTX_DEF);
	/* Allocate DMA memory for Rx descriptors */
	error = bus_dmamem_alloc(sc->rx_dtag,
	    (void**)&(rx->desc),
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO,
	    &rx->desc_map);
	if (error != 0 || rx->desc == NULL)
		goto fail;
	error = bus_dmamap_load(sc->rx_dtag, rx->desc_map,
	    rx->desc,
	    sizeof(struct mvneta_rx_desc) * MVNETA_RX_RING_CNT,
	    mvneta_dmamap_cb, &rx->desc_pa, BUS_DMA_NOWAIT);
	if (error != 0)
		goto fail;

	for (i = 0; i < MVNETA_RX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->rxbuf_dtag, 0, &dmap);
		if (error != 0) {
			device_printf(sc->dev,
			    "Failed to create DMA map for Rx buffer num: %d\n", i);
			goto fail;
		}
		rxbuf = &rx->rxbuf[i];
		rxbuf->dmap = dmap;
		rxbuf->m = NULL;
	}

	return (0);
fail:
	mvneta_ring_dealloc_rx_queue(sc, q);
	device_printf(sc->dev, "DMA Ring buffer allocation failure.\n");
	return (error);
}

STATIC int
mvneta_ring_alloc_tx_queue(struct mvneta_softc *sc, int q)
{
	struct mvneta_tx_ring *tx;
	int error;

	if (q >= MVNETA_TX_QNUM_MAX)
		return (EINVAL);
	tx = MVNETA_TX_RING(sc, q);
	mtx_init(&tx->ring_mtx, "mvneta_tx", NULL, MTX_DEF);
	error = bus_dmamem_alloc(sc->tx_dtag,
	    (void**)&(tx->desc),
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO,
	    &tx->desc_map);
	if (error != 0 || tx->desc == NULL)
		goto fail;
	error = bus_dmamap_load(sc->tx_dtag, tx->desc_map,
	    tx->desc,
	    sizeof(struct mvneta_tx_desc) * MVNETA_TX_RING_CNT,
	    mvneta_dmamap_cb, &tx->desc_pa, BUS_DMA_NOWAIT);
	if (error != 0)
		goto fail;

#ifdef MVNETA_MULTIQUEUE
	tx->br = buf_ring_alloc(MVNETA_BUFRING_SIZE, M_DEVBUF, M_NOWAIT,
	    &tx->ring_mtx);
	if (tx->br == NULL) {
		device_printf(sc->dev,
		    "Could not setup buffer ring for TxQ(%d)\n", q);
		error = ENOMEM;
		goto fail;
	}
#endif

	return (0);
fail:
	mvneta_ring_dealloc_tx_queue(sc, q);
	device_printf(sc->dev, "DMA Ring buffer allocation failure.\n");
	return (error);
}

STATIC void
mvneta_ring_dealloc_tx_queue(struct mvneta_softc *sc, int q)
{
	struct mvneta_tx_ring *tx;
	struct mvneta_buf *txbuf;
	void *kva;
	int error;
	int i;

	if (q >= MVNETA_TX_QNUM_MAX)
		return;
	tx = MVNETA_TX_RING(sc, q);

	if (tx->taskq != NULL) {
		/* Remove task */
		while (taskqueue_cancel(tx->taskq, &tx->task, NULL) != 0)
			taskqueue_drain(tx->taskq, &tx->task);
	}
#ifdef MVNETA_MULTIQUEUE
	if (tx->br != NULL)
		drbr_free(tx->br, M_DEVBUF);
#endif

	if (sc->txmbuf_dtag != NULL) {
		if (mtx_name(&tx->ring_mtx) != NULL) {
			/*
			 * It is assumed that maps are being loaded after mutex
			 * is initialized. Therefore we can skip unloading maps
			 * when mutex is empty.
			 */
			mvneta_tx_lockq(sc, q);
			mvneta_ring_flush_tx_queue(sc, q);
			mvneta_tx_unlockq(sc, q);
		}
		for (i = 0; i < MVNETA_TX_RING_CNT; i++) {
			txbuf = &tx->txbuf[i];
			if (txbuf->dmap != NULL) {
				error = bus_dmamap_destroy(sc->txmbuf_dtag,
				    txbuf->dmap);
				if (error != 0) {
					panic("%s: map busy for Tx descriptor (Q%d, %d)",
					    __func__, q, i);
				}
			}
		}
	}

	if (tx->desc_pa != 0)
		bus_dmamap_unload(sc->tx_dtag, tx->desc_map);

	kva = (void *)tx->desc;
	if (kva != NULL)
		bus_dmamem_free(sc->tx_dtag, tx->desc, tx->desc_map);

	if (mtx_name(&tx->ring_mtx) != NULL)
		mtx_destroy(&tx->ring_mtx);

	memset(tx, 0, sizeof(*tx));
}

STATIC void
mvneta_ring_dealloc_rx_queue(struct mvneta_softc *sc, int q)
{
	struct mvneta_rx_ring *rx;
	struct lro_ctrl	*lro;
	void *kva;

	if (q >= MVNETA_RX_QNUM_MAX)
		return;

	rx = MVNETA_RX_RING(sc, q);

	mvneta_ring_flush_rx_queue(sc, q);

	if (rx->desc_pa != 0)
		bus_dmamap_unload(sc->rx_dtag, rx->desc_map);

	kva = (void *)rx->desc;
	if (kva != NULL)
		bus_dmamem_free(sc->rx_dtag, rx->desc, rx->desc_map);

	lro = &rx->lro;
	tcp_lro_free(lro);

	if (mtx_name(&rx->ring_mtx) != NULL)
		mtx_destroy(&rx->ring_mtx);

	memset(rx, 0, sizeof(*rx));
}

STATIC int
mvneta_ring_init_rx_queue(struct mvneta_softc *sc, int q)
{
	struct mvneta_rx_ring *rx;
	struct lro_ctrl	*lro;
	int error;

	if (q >= MVNETA_RX_QNUM_MAX)
		return (0);

	rx = MVNETA_RX_RING(sc, q);
	rx->dma = rx->cpu = 0;
	rx->queue_th_received = MVNETA_RXTH_COUNT;
	rx->queue_th_time = (mvneta_get_clk() / 1000) / 10; /* 0.1 [ms] */

	/* Initialize LRO */
	rx->lro_enabled = FALSE;
	if ((sc->ifp->if_capenable & IFCAP_LRO) != 0) {
		lro = &rx->lro;
		error = tcp_lro_init(lro);
		if (error != 0)
			device_printf(sc->dev, "LRO Initialization failed!\n");
		else {
			rx->lro_enabled = TRUE;
			lro->ifp = sc->ifp;
		}
	}

	return (0);
}

STATIC int
mvneta_ring_init_tx_queue(struct mvneta_softc *sc, int q)
{
	struct mvneta_tx_ring *tx;
	struct mvneta_buf *txbuf;
	int i, error;

	if (q >= MVNETA_TX_QNUM_MAX)
		return (0);

	tx = MVNETA_TX_RING(sc, q);

	/* Tx handle */
	for (i = 0; i < MVNETA_TX_RING_CNT; i++) {
		txbuf = &tx->txbuf[i];
		txbuf->m = NULL;
		/* Tx handle needs DMA map for busdma_load_mbuf() */
		error = bus_dmamap_create(sc->txmbuf_dtag, 0,
		    &txbuf->dmap);
		if (error != 0) {
			device_printf(sc->dev,
			    "can't create dma map (tx ring %d)\n", i);
			return (error);
		}
	}
	tx->dma = tx->cpu = 0;
	tx->used = 0;
	tx->drv_error = 0;
	tx->queue_status = MVNETA_QUEUE_DISABLED;
	tx->queue_hung = FALSE;

	tx->ifp = sc->ifp;
	tx->qidx = q;
	TASK_INIT(&tx->task, 0, mvneta_tx_task, tx);
	tx->taskq = taskqueue_create_fast("mvneta_tx_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &tx->taskq);
	taskqueue_start_threads(&tx->taskq, 1, PI_NET, "%s: tx_taskq(%d)",
	    device_get_nameunit(sc->dev), q);

	return (0);
}

STATIC void
mvneta_ring_flush_tx_queue(struct mvneta_softc *sc, int q)
{
	struct mvneta_tx_ring *tx;
	struct mvneta_buf *txbuf;
	int i;

	tx = MVNETA_TX_RING(sc, q);
	KASSERT_TX_MTX(sc, q);

	/* Tx handle */
	for (i = 0; i < MVNETA_TX_RING_CNT; i++) {
		txbuf = &tx->txbuf[i];
		bus_dmamap_unload(sc->txmbuf_dtag, txbuf->dmap);
		if (txbuf->m != NULL) {
			m_freem(txbuf->m);
			txbuf->m = NULL;
		}
	}
	tx->dma = tx->cpu = 0;
	tx->used = 0;
}

STATIC void
mvneta_ring_flush_rx_queue(struct mvneta_softc *sc, int q)
{
	struct mvneta_rx_ring *rx;
	struct mvneta_buf *rxbuf;
	int i;

	rx = MVNETA_RX_RING(sc, q);
	KASSERT_RX_MTX(sc, q);

	/* Rx handle */
	for (i = 0; i < MVNETA_RX_RING_CNT; i++) {
		rxbuf = &rx->rxbuf[i];
		mvneta_rx_buf_free(sc, rxbuf);
	}
	rx->dma = rx->cpu = 0;
}

/*
 * Rx/Tx Queue Control
 */
STATIC int
mvneta_rx_queue_init(struct ifnet *ifp, int q)
{
	struct mvneta_softc *sc;
	struct mvneta_rx_ring *rx;
	uint32_t reg;

	sc = ifp->if_softc;
	KASSERT_RX_MTX(sc, q);
	rx =  MVNETA_RX_RING(sc, q);
	DASSERT(rx->desc_pa != 0);

	/* descriptor address */
	MVNETA_WRITE(sc, MVNETA_PRXDQA(q), rx->desc_pa);

	/* Rx buffer size and descriptor ring size */
	reg  = MVNETA_PRXDQS_BUFFERSIZE(MVNETA_PACKET_SIZE >> 3);
	reg |= MVNETA_PRXDQS_DESCRIPTORSQUEUESIZE(MVNETA_RX_RING_CNT);
	MVNETA_WRITE(sc, MVNETA_PRXDQS(q), reg);
#ifdef MVNETA_KTR
	CTR3(KTR_SPARE2, "%s PRXDQS(%d): %#x", ifp->if_xname, q,
	    MVNETA_READ(sc, MVNETA_PRXDQS(q)));
#endif
	/* Rx packet offset address */
	reg = MVNETA_PRXC_PACKETOFFSET(MVNETA_PACKET_OFFSET >> 3);
	MVNETA_WRITE(sc, MVNETA_PRXC(q), reg);
#ifdef MVNETA_KTR
	CTR3(KTR_SPARE2, "%s PRXC(%d): %#x", ifp->if_xname, q,
	    MVNETA_READ(sc, MVNETA_PRXC(q)));
#endif

	/* if DMA is not working, register is not updated */
	DASSERT(MVNETA_READ(sc, MVNETA_PRXDQA(q)) == rx->desc_pa);
	return (0);
}

STATIC int
mvneta_tx_queue_init(struct ifnet *ifp, int q)
{
	struct mvneta_softc *sc;
	struct mvneta_tx_ring *tx;
	uint32_t reg;

	sc = ifp->if_softc;
	KASSERT_TX_MTX(sc, q);
	tx = MVNETA_TX_RING(sc, q);
	DASSERT(tx->desc_pa != 0);

	/* descriptor address */
	MVNETA_WRITE(sc, MVNETA_PTXDQA(q), tx->desc_pa);

	/* descriptor ring size */
	reg = MVNETA_PTXDQS_DQS(MVNETA_TX_RING_CNT);
	MVNETA_WRITE(sc, MVNETA_PTXDQS(q), reg);

	/* if DMA is not working, register is not updated */
	DASSERT(MVNETA_READ(sc, MVNETA_PTXDQA(q)) == tx->desc_pa);
	return (0);
}

STATIC int
mvneta_rx_queue_enable(struct ifnet *ifp, int q)
{
	struct mvneta_softc *sc;
	struct mvneta_rx_ring *rx;
	uint32_t reg;

	sc = ifp->if_softc;
	rx = MVNETA_RX_RING(sc, q);
	KASSERT_RX_MTX(sc, q);

	/* Set Rx interrupt threshold */
	reg  = MVNETA_PRXDQTH_ODT(rx->queue_th_received);
	MVNETA_WRITE(sc, MVNETA_PRXDQTH(q), reg);

	reg  = MVNETA_PRXITTH_RITT(rx->queue_th_time);
	MVNETA_WRITE(sc, MVNETA_PRXITTH(q), reg);

	/* Unmask RXTX_TH Intr. */
	reg = MVNETA_READ(sc, MVNETA_PRXTXTIM);
	reg |= MVNETA_PRXTXTI_RBICTAPQ(q); /* Rx Buffer Interrupt Coalese */
	MVNETA_WRITE(sc, MVNETA_PRXTXTIM, reg);

	/* Enable Rx queue */
	reg = MVNETA_READ(sc, MVNETA_RQC) & MVNETA_RQC_EN_MASK;
	reg |= MVNETA_RQC_ENQ(q);
	MVNETA_WRITE(sc, MVNETA_RQC, reg);

	rx->queue_status = MVNETA_QUEUE_WORKING;
	return (0);
}

STATIC int
mvneta_tx_queue_enable(struct ifnet *ifp, int q)
{
	struct mvneta_softc *sc;
	struct mvneta_tx_ring *tx;

	sc = ifp->if_softc;
	tx = MVNETA_TX_RING(sc, q);
	KASSERT_TX_MTX(sc, q);

	/* Enable Tx queue */
	MVNETA_WRITE(sc, MVNETA_TQC, MVNETA_TQC_ENQ(q));

	tx->queue_status = MVNETA_QUEUE_IDLE;
	tx->queue_hung = FALSE;
	return (0);
}

STATIC __inline void
mvneta_rx_lockq(struct mvneta_softc *sc, int q)
{

	DASSERT(q >= 0);
	DASSERT(q < MVNETA_RX_QNUM_MAX);
	mtx_lock(&sc->rx_ring[q].ring_mtx);
}

STATIC __inline void
mvneta_rx_unlockq(struct mvneta_softc *sc, int q)
{

	DASSERT(q >= 0);
	DASSERT(q < MVNETA_RX_QNUM_MAX);
	mtx_unlock(&sc->rx_ring[q].ring_mtx);
}

STATIC __inline int __unused
mvneta_tx_trylockq(struct mvneta_softc *sc, int q)
{

	DASSERT(q >= 0);
	DASSERT(q < MVNETA_TX_QNUM_MAX);
	return (mtx_trylock(&sc->tx_ring[q].ring_mtx));
}

STATIC __inline void
mvneta_tx_lockq(struct mvneta_softc *sc, int q)
{

	DASSERT(q >= 0);
	DASSERT(q < MVNETA_TX_QNUM_MAX);
	mtx_lock(&sc->tx_ring[q].ring_mtx);
}

STATIC __inline void
mvneta_tx_unlockq(struct mvneta_softc *sc, int q)
{

	DASSERT(q >= 0);
	DASSERT(q < MVNETA_TX_QNUM_MAX);
	mtx_unlock(&sc->tx_ring[q].ring_mtx);
}

/*
 * Interrupt Handlers
 */
STATIC void
mvneta_disable_intr(struct mvneta_softc *sc)
{

	MVNETA_WRITE(sc, MVNETA_EUIM, 0);
	MVNETA_WRITE(sc, MVNETA_EUIC, 0);
	MVNETA_WRITE(sc, MVNETA_PRXTXTIM, 0);
	MVNETA_WRITE(sc, MVNETA_PRXTXTIC, 0);
	MVNETA_WRITE(sc, MVNETA_PRXTXIM, 0);
	MVNETA_WRITE(sc, MVNETA_PRXTXIC, 0);
	MVNETA_WRITE(sc, MVNETA_PMIM, 0);
	MVNETA_WRITE(sc, MVNETA_PMIC, 0);
	MVNETA_WRITE(sc, MVNETA_PIE, 0);
}

STATIC void
mvneta_enable_intr(struct mvneta_softc *sc)
{
	uint32_t reg;

	/* Enable Summary Bit to check all interrupt cause. */
	reg = MVNETA_READ(sc, MVNETA_PRXTXTIM);
	reg |= MVNETA_PRXTXTI_PMISCICSUMMARY;
	MVNETA_WRITE(sc, MVNETA_PRXTXTIM, reg);

	if (sc->use_inband_status) {
		/* Enable Port MISC Intr. (via RXTX_TH_Summary bit) */
		MVNETA_WRITE(sc, MVNETA_PMIM, MVNETA_PMI_PHYSTATUSCHNG |
		    MVNETA_PMI_LINKCHANGE | MVNETA_PMI_PSCSYNCCHANGE);
	}

	/* Enable All Queue Interrupt */
	reg  = MVNETA_READ(sc, MVNETA_PIE);
	reg |= MVNETA_PIE_RXPKTINTRPTENB_MASK;
	reg |= MVNETA_PIE_TXPKTINTRPTENB_MASK;
	MVNETA_WRITE(sc, MVNETA_PIE, reg);
}

STATIC void
mvneta_rxtxth_intr(void *arg)
{
	struct mvneta_softc *sc;
	struct ifnet *ifp;
	uint32_t ic, queues;

	sc = arg;
	ifp = sc->ifp;
#ifdef MVNETA_KTR
	CTR1(KTR_SPARE2, "%s got RXTX_TH_Intr", ifp->if_xname);
#endif
	ic = MVNETA_READ(sc, MVNETA_PRXTXTIC);
	if (ic == 0)
		return;
	MVNETA_WRITE(sc, MVNETA_PRXTXTIC, ~ic);

	/* Ack maintance interrupt first */
	if (__predict_false((ic & MVNETA_PRXTXTI_PMISCICSUMMARY) &&
	    sc->use_inband_status)) {
		mvneta_sc_lock(sc);
		mvneta_misc_intr(sc);
		mvneta_sc_unlock(sc);
	}
	if (__predict_false(!(ifp->if_drv_flags & IFF_DRV_RUNNING)))
		return;
	/* RxTxTH interrupt */
	queues = MVNETA_PRXTXTI_GET_RBICTAPQ(ic);
	if (__predict_true(queues)) {
#ifdef MVNETA_KTR
		CTR1(KTR_SPARE2, "%s got PRXTXTIC: +RXEOF", ifp->if_xname);
#endif
		/* At the moment the driver support only one RX queue. */
		DASSERT(MVNETA_IS_QUEUE_SET(queues, 0));
		mvneta_rx(sc, 0, 0);
	}
}

STATIC int
mvneta_misc_intr(struct mvneta_softc *sc)
{
	uint32_t ic;
	int claimed = 0;

#ifdef MVNETA_KTR
	CTR1(KTR_SPARE2, "%s got MISC_INTR", sc->ifp->if_xname);
#endif
	KASSERT_SC_MTX(sc);

	for (;;) {
		ic = MVNETA_READ(sc, MVNETA_PMIC);
		ic &= MVNETA_READ(sc, MVNETA_PMIM);
		if (ic == 0)
			break;
		MVNETA_WRITE(sc, MVNETA_PMIC, ~ic);
		claimed = 1;

		if (ic & (MVNETA_PMI_PHYSTATUSCHNG |
		    MVNETA_PMI_LINKCHANGE | MVNETA_PMI_PSCSYNCCHANGE))
			mvneta_link_isr(sc);
	}
	return (claimed);
}

STATIC void
mvneta_tick(void *arg)
{
	struct mvneta_softc *sc;
	struct mvneta_tx_ring *tx;
	struct mvneta_rx_ring *rx;
	int q;
	uint32_t fc_prev, fc_curr;

	sc = arg;

	/*
	 * This is done before mib update to get the right stats
	 * for this tick.
	 */
	mvneta_tx_drain(sc);

	/* Extract previous flow-control frame received counter. */
	fc_prev = sc->sysctl_mib[MVNETA_MIB_FC_GOOD_IDX].counter;
	/* Read mib registers (clear by read). */
	mvneta_update_mib(sc);
	/* Extract current flow-control frame received counter. */
	fc_curr = sc->sysctl_mib[MVNETA_MIB_FC_GOOD_IDX].counter;


	if (sc->phy_attached && sc->ifp->if_flags & IFF_UP) {
		mvneta_sc_lock(sc);
		mii_tick(sc->mii);

		/* Adjust MAC settings */
		mvneta_adjust_link(sc);
		mvneta_sc_unlock(sc);
	}

	/*
	 * We were unable to refill the rx queue and left the rx func, leaving
	 * the ring without mbuf and no way to call the refill func.
	 */
	for (q = 0; q < MVNETA_RX_QNUM_MAX; q++) {
		rx = MVNETA_RX_RING(sc, q);
		if (rx->needs_refill == TRUE) {
			mvneta_rx_lockq(sc, q);
			mvneta_rx_queue_refill(sc, q);
			mvneta_rx_unlockq(sc, q);
		}
	}

	/*
	 * Watchdog:
	 * - check if queue is mark as hung.
	 * - ignore hung status if we received some pause frame
	 *   as hardware may have paused packet transmit.
	 */
	for (q = 0; q < MVNETA_TX_QNUM_MAX; q++) {
		/*
		 * We should take queue lock, but as we only read
		 * queue status we can do it without lock, we may
		 * only missdetect queue status for one tick.
		 */
		tx = MVNETA_TX_RING(sc, q);

		if (tx->queue_hung && (fc_curr - fc_prev) == 0)
			goto timeout;
	}

	callout_schedule(&sc->tick_ch, hz);
	return;

timeout:
	if_printf(sc->ifp, "watchdog timeout\n");

	mvneta_sc_lock(sc);
	sc->counter_watchdog++;
	sc->counter_watchdog_mib++;
	/* Trigger reinitialize sequence. */
	mvneta_stop_locked(sc);
	mvneta_init_locked(sc);
	mvneta_sc_unlock(sc);
}

STATIC void
mvneta_qflush(struct ifnet *ifp)
{
#ifdef MVNETA_MULTIQUEUE
	struct mvneta_softc *sc;
	struct mvneta_tx_ring *tx;
	struct mbuf *m;
	size_t q;

	sc = ifp->if_softc;

	for (q = 0; q < MVNETA_TX_QNUM_MAX; q++) {
		tx = MVNETA_TX_RING(sc, q);
		mvneta_tx_lockq(sc, q);
		while ((m = buf_ring_dequeue_sc(tx->br)) != NULL)
			m_freem(m);
		mvneta_tx_unlockq(sc, q);
	}
#endif
	if_qflush(ifp);
}

STATIC void
mvneta_tx_task(void *arg, int pending)
{
	struct mvneta_softc *sc;
	struct mvneta_tx_ring *tx;
	struct ifnet *ifp;
	int error;

	tx = arg;
	ifp = tx->ifp;
	sc = ifp->if_softc;

	mvneta_tx_lockq(sc, tx->qidx);
	error = mvneta_xmit_locked(sc, tx->qidx);
	mvneta_tx_unlockq(sc, tx->qidx);

	/* Try again */
	if (__predict_false(error != 0 && error != ENETDOWN)) {
		pause("mvneta_tx_task_sleep", 1);
		taskqueue_enqueue(tx->taskq, &tx->task);
	}
}

STATIC int
mvneta_xmitfast_locked(struct mvneta_softc *sc, int q, struct mbuf **m)
{
	struct mvneta_tx_ring *tx;
	struct ifnet *ifp;
	int error;

	KASSERT_TX_MTX(sc, q);
	tx = MVNETA_TX_RING(sc, q);
	error = 0;

	ifp = sc->ifp;

	/* Dont enqueue packet if the queue is disabled. */
	if (__predict_false(tx->queue_status == MVNETA_QUEUE_DISABLED)) {
		m_freem(*m);
		*m = NULL;
		return (ENETDOWN);
	}

	/* Reclaim mbuf if above threshold. */
	if (__predict_true(tx->used > MVNETA_TX_RECLAIM_COUNT))
		mvneta_tx_queue_complete(sc, q);

	/* Do not call transmit path if queue is already too full. */
	if (__predict_false(tx->used >
	    MVNETA_TX_RING_CNT - MVNETA_TX_SEGLIMIT))
		return (ENOBUFS);

	error = mvneta_tx_queue(sc, m, q);
	if (__predict_false(error != 0))
		return (error);

	/* Send a copy of the frame to the BPF listener */
	ETHER_BPF_MTAP(ifp, *m);

	/* Set watchdog on */
	tx->watchdog_time = ticks;
	tx->queue_status = MVNETA_QUEUE_WORKING;

	return (error);
}

#ifdef MVNETA_MULTIQUEUE
STATIC int
mvneta_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct mvneta_softc *sc;
	struct mvneta_tx_ring *tx;
	int error;
	int q;

	sc = ifp->if_softc;

	/* Use default queue if there is no flow id as thread can migrate. */
	if (__predict_true(M_HASHTYPE_GET(m) != M_HASHTYPE_NONE))
		q = m->m_pkthdr.flowid % MVNETA_TX_QNUM_MAX;
	else
		q = 0;

	tx = MVNETA_TX_RING(sc, q);

	/* If buf_ring is full start transmit immediatly. */
	if (buf_ring_full(tx->br)) {
		mvneta_tx_lockq(sc, q);
		mvneta_xmit_locked(sc, q);
		mvneta_tx_unlockq(sc, q);
	}

	/*
	 * If the buf_ring is empty we will not reorder packets.
	 * If the lock is available transmit without using buf_ring.
	 */
	if (buf_ring_empty(tx->br) && mvneta_tx_trylockq(sc, q) != 0) {
		error = mvneta_xmitfast_locked(sc, q, &m);
		mvneta_tx_unlockq(sc, q);
		if (__predict_true(error == 0))
			return (0);

		/* Transmit can fail in fastpath. */
		if (__predict_false(m == NULL))
			return (error);
	}

	/* Enqueue then schedule taskqueue. */
	error = drbr_enqueue(ifp, tx->br, m);
	if (__predict_false(error != 0))
		return (error);

	taskqueue_enqueue(tx->taskq, &tx->task);
	return (0);
}

STATIC int
mvneta_xmit_locked(struct mvneta_softc *sc, int q)
{
	struct ifnet *ifp;
	struct mvneta_tx_ring *tx;
	struct mbuf *m;
	int error;

	KASSERT_TX_MTX(sc, q);
	ifp = sc->ifp;
	tx = MVNETA_TX_RING(sc, q);
	error = 0;

	while ((m = drbr_peek(ifp, tx->br)) != NULL) {
		error = mvneta_xmitfast_locked(sc, q, &m);
		if (__predict_false(error != 0)) {
			if (m != NULL)
				drbr_putback(ifp, tx->br, m);
			else
				drbr_advance(ifp, tx->br);
			break;
		}
		drbr_advance(ifp, tx->br);
	}

	return (error);
}
#else /* !MVNETA_MULTIQUEUE */
STATIC void
mvneta_start(struct ifnet *ifp)
{
	struct mvneta_softc *sc;
	struct mvneta_tx_ring *tx;
	int error;

	sc = ifp->if_softc;
	tx = MVNETA_TX_RING(sc, 0);

	mvneta_tx_lockq(sc, 0);
	error = mvneta_xmit_locked(sc, 0);
	mvneta_tx_unlockq(sc, 0);
	/* Handle retransmit in the background taskq. */
	if (__predict_false(error != 0 && error != ENETDOWN))
		taskqueue_enqueue(tx->taskq, &tx->task);
}

STATIC int
mvneta_xmit_locked(struct mvneta_softc *sc, int q)
{
	struct ifnet *ifp;
	struct mvneta_tx_ring *tx;
	struct mbuf *m;
	int error;

	KASSERT_TX_MTX(sc, q);
	ifp = sc->ifp;
	tx = MVNETA_TX_RING(sc, 0);
	error = 0;

	while (!IFQ_DRV_IS_EMPTY(&ifp->if_snd)) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;

		error = mvneta_xmitfast_locked(sc, q, &m);
		if (__predict_false(error != 0)) {
			if (m != NULL)
				IFQ_DRV_PREPEND(&ifp->if_snd, m);
			break;
		}
	}

	return (error);
}
#endif

STATIC int
mvneta_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct mvneta_softc *sc;
	struct mvneta_rx_ring *rx;
	struct ifreq *ifr;
	int error, mask;
	uint32_t flags;
	int q;

	error = 0;
	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	switch (cmd) {
	case SIOCSIFFLAGS:
		mvneta_sc_lock(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				flags = ifp->if_flags ^ sc->mvneta_if_flags;

				if (flags != 0)
					sc->mvneta_if_flags = ifp->if_flags;

				if ((flags & IFF_PROMISC) != 0)
					mvneta_filter_setup(sc);
			} else {
				mvneta_init_locked(sc);
				sc->mvneta_if_flags = ifp->if_flags;
				if (sc->phy_attached)
					mii_mediachg(sc->mii);
				mvneta_sc_unlock(sc);
				break;
			}
		} else if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			mvneta_stop_locked(sc);

		sc->mvneta_if_flags = ifp->if_flags;
		mvneta_sc_unlock(sc);
		break;
	case SIOCSIFCAP:
		if (ifp->if_mtu > MVNETA_MAX_CSUM_MTU &&
		    ifr->ifr_reqcap & IFCAP_TXCSUM)
			ifr->ifr_reqcap &= ~IFCAP_TXCSUM;
		mask = ifp->if_capenable ^ ifr->ifr_reqcap;
		if (mask & IFCAP_HWCSUM) {
			ifp->if_capenable &= ~IFCAP_HWCSUM;
			ifp->if_capenable |= IFCAP_HWCSUM & ifr->ifr_reqcap;
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist = CSUM_IP | CSUM_TCP |
				    CSUM_UDP;
			else
				ifp->if_hwassist = 0;
		}
		if (mask & IFCAP_LRO) {
			mvneta_sc_lock(sc);
			ifp->if_capenable ^= IFCAP_LRO;
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				for (q = 0; q < MVNETA_RX_QNUM_MAX; q++) {
					rx = MVNETA_RX_RING(sc, q);
					rx->lro_enabled = !rx->lro_enabled;
				}
			}
			mvneta_sc_unlock(sc);
		}
		VLAN_CAPABILITIES(ifp);
		break;
	case SIOCSIFMEDIA:
		if ((IFM_SUBTYPE(ifr->ifr_media) == IFM_1000_T ||
		    IFM_SUBTYPE(ifr->ifr_media) == IFM_2500_T) &&
		    (ifr->ifr_media & IFM_FDX) == 0) {
			device_printf(sc->dev,
			    "%s half-duplex unsupported\n",
			    IFM_SUBTYPE(ifr->ifr_media) == IFM_1000_T ?
			    "1000Base-T" :
			    "2500Base-T");
			error = EINVAL;
			break;
		}
	case SIOCGIFMEDIA: /* FALLTHROUGH */
	case SIOCGIFXMEDIA:
		if (!sc->phy_attached)
			error = ifmedia_ioctl(ifp, ifr, &sc->mvneta_ifmedia,
			    cmd);
		else
			error = ifmedia_ioctl(ifp, ifr, &sc->mii->mii_media,
			    cmd);
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < 68 || ifr->ifr_mtu > MVNETA_MAX_FRAME -
		    MVNETA_ETHER_SIZE) {
			error = EINVAL;
		} else {
			ifp->if_mtu = ifr->ifr_mtu;
			mvneta_sc_lock(sc);
			if (ifp->if_mtu > MVNETA_MAX_CSUM_MTU) {
				ifp->if_capenable &= ~IFCAP_TXCSUM;
				ifp->if_hwassist = 0;
			} else {
				ifp->if_capenable |= IFCAP_TXCSUM;
				ifp->if_hwassist = CSUM_IP | CSUM_TCP |
					CSUM_UDP;
			}

			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				/* Trigger reinitialize sequence */
				mvneta_stop_locked(sc);
				mvneta_init_locked(sc);
			}
			mvneta_sc_unlock(sc);
                }
                break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

STATIC void
mvneta_init_locked(void *arg)
{
	struct mvneta_softc *sc;
	struct ifnet *ifp;
	uint32_t reg;
	int q, cpu;

	sc = arg;
	ifp = sc->ifp;

	if (!device_is_attached(sc->dev) ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	mvneta_disable_intr(sc);
	callout_stop(&sc->tick_ch);

	/* Get the latest mac address */
	bcopy(IF_LLADDR(ifp), sc->enaddr, ETHER_ADDR_LEN);
	mvneta_set_mac_address(sc, sc->enaddr);
	mvneta_filter_setup(sc);

	/* Start DMA Engine */
	MVNETA_WRITE(sc, MVNETA_PRXINIT, 0x00000000);
	MVNETA_WRITE(sc, MVNETA_PTXINIT, 0x00000000);
	MVNETA_WRITE(sc, MVNETA_PACC, MVNETA_PACC_ACCELERATIONMODE_EDM);

	/* Enable port */
	reg  = MVNETA_READ(sc, MVNETA_PMACC0);
	reg |= MVNETA_PMACC0_PORTEN;
	MVNETA_WRITE(sc, MVNETA_PMACC0, reg);

	/* Allow access to each TXQ/RXQ from both CPU's */
	for (cpu = 0; cpu < mp_ncpus; ++cpu)
		MVNETA_WRITE(sc, MVNETA_PCP2Q(cpu),
		    MVNETA_PCP2Q_TXQEN_MASK | MVNETA_PCP2Q_RXQEN_MASK);

	for (q = 0; q < MVNETA_RX_QNUM_MAX; q++) {
		mvneta_rx_lockq(sc, q);
		mvneta_rx_queue_refill(sc, q);
		mvneta_rx_unlockq(sc, q);
	}

	if (!sc->phy_attached)
		mvneta_linkup(sc);

	/* Enable interrupt */
	mvneta_enable_intr(sc);

	/* Set Counter */
	callout_schedule(&sc->tick_ch, hz);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
}

STATIC void
mvneta_init(void *arg)
{
	struct mvneta_softc *sc;

	sc = arg;
	mvneta_sc_lock(sc);
	mvneta_init_locked(sc);
	if (sc->phy_attached)
		mii_mediachg(sc->mii);
	mvneta_sc_unlock(sc);
}

/* ARGSUSED */
STATIC void
mvneta_stop_locked(struct mvneta_softc *sc)
{
	struct ifnet *ifp;
	struct mvneta_rx_ring *rx;
	struct mvneta_tx_ring *tx;
	uint32_t reg;
	int q;

	ifp = sc->ifp;
	if (ifp == NULL || (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	mvneta_disable_intr(sc);

	callout_stop(&sc->tick_ch);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	/* Link down */
	if (sc->linkup == TRUE)
		mvneta_linkdown(sc);

	/* Reset the MAC Port Enable bit */
	reg = MVNETA_READ(sc, MVNETA_PMACC0);
	reg &= ~MVNETA_PMACC0_PORTEN;
	MVNETA_WRITE(sc, MVNETA_PMACC0, reg);

	/* Disable each of queue */
	for (q = 0; q < MVNETA_RX_QNUM_MAX; q++) {
		rx = MVNETA_RX_RING(sc, q);

		mvneta_rx_lockq(sc, q);
		mvneta_ring_flush_rx_queue(sc, q);
		mvneta_rx_unlockq(sc, q);
	}

	/*
	 * Hold Reset state of DMA Engine
	 * (must write 0x0 to restart it)
	 */
	MVNETA_WRITE(sc, MVNETA_PRXINIT, 0x00000001);
	MVNETA_WRITE(sc, MVNETA_PTXINIT, 0x00000001);

	for (q = 0; q < MVNETA_TX_QNUM_MAX; q++) {
		tx = MVNETA_TX_RING(sc, q);

		mvneta_tx_lockq(sc, q);
		mvneta_ring_flush_tx_queue(sc, q);
		mvneta_tx_unlockq(sc, q);
	}
}

STATIC void
mvneta_stop(struct mvneta_softc *sc)
{

	mvneta_sc_lock(sc);
	mvneta_stop_locked(sc);
	mvneta_sc_unlock(sc);
}

STATIC int
mvneta_mediachange(struct ifnet *ifp)
{
	struct mvneta_softc *sc;

	sc = ifp->if_softc;

	if (!sc->phy_attached && !sc->use_inband_status) {
		/* We shouldn't be here */
		if_printf(ifp, "Cannot change media in fixed-link mode!\n");
		return (0);
	}

	if (sc->use_inband_status) {
		mvneta_update_media(sc, sc->mvneta_ifmedia.ifm_media);
		return (0);
	}

	mvneta_sc_lock(sc);

	/* Update PHY */
	mii_mediachg(sc->mii);

	mvneta_sc_unlock(sc);

	return (0);
}

STATIC void
mvneta_get_media(struct mvneta_softc *sc, struct ifmediareq *ifmr)
{
	uint32_t psr;

	psr = MVNETA_READ(sc, MVNETA_PSR);

	/* Speed */
	if (psr & MVNETA_PSR_GMIISPEED)
		ifmr->ifm_active = IFM_ETHER_SUBTYPE_SET(IFM_1000_T);
	else if (psr & MVNETA_PSR_MIISPEED)
		ifmr->ifm_active = IFM_ETHER_SUBTYPE_SET(IFM_100_TX);
	else if (psr & MVNETA_PSR_LINKUP)
		ifmr->ifm_active = IFM_ETHER_SUBTYPE_SET(IFM_10_T);

	/* Duplex */
	if (psr & MVNETA_PSR_FULLDX)
		ifmr->ifm_active |= IFM_FDX;

	/* Link */
	ifmr->ifm_status = IFM_AVALID;
	if (psr & MVNETA_PSR_LINKUP)
		ifmr->ifm_status |= IFM_ACTIVE;
}

STATIC void
mvneta_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct mvneta_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;

	if (!sc->phy_attached && !sc->use_inband_status) {
		ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE;
		return;
	}

	mvneta_sc_lock(sc);

	if (sc->use_inband_status) {
		mvneta_get_media(sc, ifmr);
		mvneta_sc_unlock(sc);
		return;
	}

	mii = sc->mii;
	mii_pollstat(mii);

	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;

	mvneta_sc_unlock(sc);
}

/*
 * Link State Notify
 */
STATIC void
mvneta_update_autoneg(struct mvneta_softc *sc, int enable)
{
	int reg;

	if (enable) {
		reg = MVNETA_READ(sc, MVNETA_PANC);
		reg &= ~(MVNETA_PANC_FORCELINKFAIL | MVNETA_PANC_FORCELINKPASS |
		    MVNETA_PANC_ANFCEN);
		reg |= MVNETA_PANC_ANDUPLEXEN | MVNETA_PANC_ANSPEEDEN |
		    MVNETA_PANC_INBANDANEN;
		MVNETA_WRITE(sc, MVNETA_PANC, reg);

		reg = MVNETA_READ(sc, MVNETA_PMACC2);
		reg |= MVNETA_PMACC2_INBANDANMODE;
		MVNETA_WRITE(sc, MVNETA_PMACC2, reg);

		reg = MVNETA_READ(sc, MVNETA_PSOMSCD);
		reg |= MVNETA_PSOMSCD_ENABLE;
		MVNETA_WRITE(sc, MVNETA_PSOMSCD, reg);
	} else {
		reg = MVNETA_READ(sc, MVNETA_PANC);
		reg &= ~(MVNETA_PANC_FORCELINKFAIL | MVNETA_PANC_FORCELINKPASS |
		    MVNETA_PANC_ANDUPLEXEN | MVNETA_PANC_ANSPEEDEN |
		    MVNETA_PANC_INBANDANEN);
		MVNETA_WRITE(sc, MVNETA_PANC, reg);

		reg = MVNETA_READ(sc, MVNETA_PMACC2);
		reg &= ~MVNETA_PMACC2_INBANDANMODE;
		MVNETA_WRITE(sc, MVNETA_PMACC2, reg);

		reg = MVNETA_READ(sc, MVNETA_PSOMSCD);
		reg &= ~MVNETA_PSOMSCD_ENABLE;
		MVNETA_WRITE(sc, MVNETA_PSOMSCD, reg);
	}
}

STATIC int
mvneta_update_media(struct mvneta_softc *sc, int media)
{
	int reg, err;
	boolean_t running;

	err = 0;

	mvneta_sc_lock(sc);

	mvneta_linkreset(sc);

	running = (sc->ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
	if (running)
		mvneta_stop_locked(sc);

	sc->autoneg = (IFM_SUBTYPE(media) == IFM_AUTO);

	if (sc->use_inband_status)
		mvneta_update_autoneg(sc, IFM_SUBTYPE(media) == IFM_AUTO);

	mvneta_update_eee(sc);
	mvneta_update_fc(sc);

	if (IFM_SUBTYPE(media) != IFM_AUTO) {
		reg = MVNETA_READ(sc, MVNETA_PANC);
		reg &= ~(MVNETA_PANC_SETGMIISPEED |
		    MVNETA_PANC_SETMIISPEED |
		    MVNETA_PANC_SETFULLDX);
		if (IFM_SUBTYPE(media) == IFM_1000_T ||
		    IFM_SUBTYPE(media) == IFM_2500_T) {
			if ((media & IFM_FDX) == 0) {
				device_printf(sc->dev,
				    "%s half-duplex unsupported\n",
				    IFM_SUBTYPE(media) == IFM_1000_T ?
				    "1000Base-T" :
				    "2500Base-T");
				err = EINVAL;
				goto out;
			}
			reg |= MVNETA_PANC_SETGMIISPEED;
		} else if (IFM_SUBTYPE(media) == IFM_100_TX)
			reg |= MVNETA_PANC_SETMIISPEED;

		if (media & IFM_FDX)
			reg |= MVNETA_PANC_SETFULLDX;

		MVNETA_WRITE(sc, MVNETA_PANC, reg);
	}
out:
	if (running)
		mvneta_init_locked(sc);
	mvneta_sc_unlock(sc);
	return (err);
}

STATIC void
mvneta_adjust_link(struct mvneta_softc *sc)
{
	boolean_t phy_linkup;
	int reg;

	/* Update eee/fc */
	mvneta_update_eee(sc);
	mvneta_update_fc(sc);

	/* Check for link change */
	phy_linkup = (sc->mii->mii_media_status &
	    (IFM_AVALID | IFM_ACTIVE)) == (IFM_AVALID | IFM_ACTIVE);

	if (sc->linkup != phy_linkup)
		mvneta_linkupdate(sc, phy_linkup);

	/* Don't update media on disabled link */
	if (!phy_linkup)
		return;

	/* Check for media type change */
	if (sc->mvneta_media != sc->mii->mii_media_active) {
		sc->mvneta_media = sc->mii->mii_media_active;

		reg = MVNETA_READ(sc, MVNETA_PANC);
		reg &= ~(MVNETA_PANC_SETGMIISPEED |
		    MVNETA_PANC_SETMIISPEED |
		    MVNETA_PANC_SETFULLDX);
		if (IFM_SUBTYPE(sc->mvneta_media) == IFM_1000_T ||
		    IFM_SUBTYPE(sc->mvneta_media) == IFM_2500_T) {
			reg |= MVNETA_PANC_SETGMIISPEED;
		} else if (IFM_SUBTYPE(sc->mvneta_media) == IFM_100_TX)
			reg |= MVNETA_PANC_SETMIISPEED;

		if (sc->mvneta_media & IFM_FDX)
			reg |= MVNETA_PANC_SETFULLDX;

		MVNETA_WRITE(sc, MVNETA_PANC, reg);
	}
}

STATIC void
mvneta_link_isr(struct mvneta_softc *sc)
{
	int linkup;

	KASSERT_SC_MTX(sc);

	linkup = MVNETA_IS_LINKUP(sc) ? TRUE : FALSE;
	if (sc->linkup == linkup)
		return;

	if (linkup == TRUE)
		mvneta_linkup(sc);
	else
		mvneta_linkdown(sc);

#ifdef DEBUG
	log(LOG_DEBUG,
	    "%s: link %s\n", device_xname(sc->dev), linkup ? "up" : "down");
#endif
}

STATIC void
mvneta_linkupdate(struct mvneta_softc *sc, boolean_t linkup)
{

	KASSERT_SC_MTX(sc);

	if (linkup == TRUE)
		mvneta_linkup(sc);
	else
		mvneta_linkdown(sc);

#ifdef DEBUG
	log(LOG_DEBUG,
	    "%s: link %s\n", device_xname(sc->dev), linkup ? "up" : "down");
#endif
}

STATIC void
mvneta_update_eee(struct mvneta_softc *sc)
{
	uint32_t reg;

	KASSERT_SC_MTX(sc);

	/* set EEE parameters */
	reg = MVNETA_READ(sc, MVNETA_LPIC1);
	if (sc->cf_lpi)
		reg |= MVNETA_LPIC1_LPIRE;
	else
		reg &= ~MVNETA_LPIC1_LPIRE;
	MVNETA_WRITE(sc, MVNETA_LPIC1, reg);
}

STATIC void
mvneta_update_fc(struct mvneta_softc *sc)
{
	uint32_t reg;

	KASSERT_SC_MTX(sc);

	reg  = MVNETA_READ(sc, MVNETA_PANC);
	if (sc->cf_fc) {
		/* Flow control negotiation */
		reg |= MVNETA_PANC_PAUSEADV;
		reg |= MVNETA_PANC_ANFCEN;
	} else {
		/* Disable flow control negotiation */
		reg &= ~MVNETA_PANC_PAUSEADV;
		reg &= ~MVNETA_PANC_ANFCEN;
	}

	MVNETA_WRITE(sc, MVNETA_PANC, reg);
}

STATIC void
mvneta_linkup(struct mvneta_softc *sc)
{
	uint32_t reg;

	KASSERT_SC_MTX(sc);

	if (!sc->use_inband_status) {
		reg  = MVNETA_READ(sc, MVNETA_PANC);
		reg |= MVNETA_PANC_FORCELINKPASS;
		reg &= ~MVNETA_PANC_FORCELINKFAIL;
		MVNETA_WRITE(sc, MVNETA_PANC, reg);
	}

	mvneta_qflush(sc->ifp);
	mvneta_portup(sc);
	sc->linkup = TRUE;
	if_link_state_change(sc->ifp, LINK_STATE_UP);
}

STATIC void
mvneta_linkdown(struct mvneta_softc *sc)
{
	uint32_t reg;

	KASSERT_SC_MTX(sc);

	if (!sc->use_inband_status) {
		reg  = MVNETA_READ(sc, MVNETA_PANC);
		reg &= ~MVNETA_PANC_FORCELINKPASS;
		reg |= MVNETA_PANC_FORCELINKFAIL;
		MVNETA_WRITE(sc, MVNETA_PANC, reg);
	}

	mvneta_portdown(sc);
	mvneta_qflush(sc->ifp);
	sc->linkup = FALSE;
	if_link_state_change(sc->ifp, LINK_STATE_DOWN);
}

STATIC void
mvneta_linkreset(struct mvneta_softc *sc)
{
	struct mii_softc *mii;

	if (sc->phy_attached) {
		/* Force reset PHY */
		mii = LIST_FIRST(&sc->mii->mii_phys);
		if (mii)
			mii_phy_reset(mii);
	}
}

/*
 * Tx Subroutines
 */
STATIC int
mvneta_tx_queue(struct mvneta_softc *sc, struct mbuf **mbufp, int q)
{
	struct ifnet *ifp;
	bus_dma_segment_t txsegs[MVNETA_TX_SEGLIMIT];
	struct mbuf *mtmp, *mbuf;
	struct mvneta_tx_ring *tx;
	struct mvneta_buf *txbuf;
	struct mvneta_tx_desc *t;
	uint32_t ptxsu;
	int start, used, error, i, txnsegs;

	mbuf = *mbufp;
	tx = MVNETA_TX_RING(sc, q);
	DASSERT(tx->used >= 0);
	DASSERT(tx->used <= MVNETA_TX_RING_CNT);
	t = NULL;
	ifp = sc->ifp;

	if (__predict_false(mbuf->m_flags & M_VLANTAG)) {
		mbuf = ether_vlanencap(mbuf, mbuf->m_pkthdr.ether_vtag);
		if (mbuf == NULL) {
			tx->drv_error++;
			*mbufp = NULL;
			return (ENOBUFS);
		}
		mbuf->m_flags &= ~M_VLANTAG;
		*mbufp = mbuf;
	}

	if (__predict_false(mbuf->m_next != NULL &&
	    (mbuf->m_pkthdr.csum_flags &
	    (CSUM_IP | CSUM_TCP | CSUM_UDP)) != 0)) {
		if (M_WRITABLE(mbuf) == 0) {
			mtmp = m_dup(mbuf, M_NOWAIT);
			m_freem(mbuf);
			if (mtmp == NULL) {
				tx->drv_error++;
				*mbufp = NULL;
				return (ENOBUFS);
			}
			*mbufp = mbuf = mtmp;
		}
	}

	/* load mbuf using dmamap of 1st descriptor */
	txbuf = &tx->txbuf[tx->cpu];
	error = bus_dmamap_load_mbuf_sg(sc->txmbuf_dtag,
	    txbuf->dmap, mbuf, txsegs, &txnsegs,
	    BUS_DMA_NOWAIT);
	if (__predict_false(error != 0)) {
#ifdef MVNETA_KTR
		CTR3(KTR_SPARE2, "%s:%u bus_dmamap_load_mbuf_sg error=%d", ifp->if_xname, q, error);
#endif
		/* This is the only recoverable error (except EFBIG). */
		if (error != ENOMEM) {
			tx->drv_error++;
			m_freem(mbuf);
			*mbufp = NULL;
			return (ENOBUFS);
		}
		return (error);
	}

	if (__predict_false(txnsegs <= 0
	    || (txnsegs + tx->used) > MVNETA_TX_RING_CNT)) {
		/* we have no enough descriptors or mbuf is broken */
#ifdef MVNETA_KTR
		CTR3(KTR_SPARE2, "%s:%u not enough descriptors txnsegs=%d",
		    ifp->if_xname, q, txnsegs);
#endif
		bus_dmamap_unload(sc->txmbuf_dtag, txbuf->dmap);
		return (ENOBUFS);
	}
	DASSERT(txbuf->m == NULL);

	/* remember mbuf using 1st descriptor */
	txbuf->m = mbuf;
	bus_dmamap_sync(sc->txmbuf_dtag, txbuf->dmap,
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	/* load to tx descriptors */
	start = tx->cpu;
	used = 0;
	for (i = 0; i < txnsegs; i++) {
		t = &tx->desc[tx->cpu];
		t->command = 0;
		t->l4ichk = 0;
		t->flags = 0;
		if (__predict_true(i == 0)) {
			/* 1st descriptor */
			t->command |= MVNETA_TX_CMD_W_PACKET_OFFSET(0);
			t->command |= MVNETA_TX_CMD_F;
			mvneta_tx_set_csumflag(ifp, t, mbuf);
		}
		t->bufptr_pa = txsegs[i].ds_addr;
		t->bytecnt = txsegs[i].ds_len;
		tx->cpu = tx_counter_adv(tx->cpu, 1);

		tx->used++;
		used++;
	}
	/* t is last descriptor here */
	DASSERT(t != NULL);
	t->command |= MVNETA_TX_CMD_L|MVNETA_TX_CMD_PADDING;

	bus_dmamap_sync(sc->tx_dtag, tx->desc_map,
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	while (__predict_false(used > 255)) {
		ptxsu = MVNETA_PTXSU_NOWD(255);
		MVNETA_WRITE(sc, MVNETA_PTXSU(q), ptxsu);
		used -= 255;
	}
	if (__predict_true(used > 0)) {
		ptxsu = MVNETA_PTXSU_NOWD(used);
		MVNETA_WRITE(sc, MVNETA_PTXSU(q), ptxsu);
	}
	return (0);
}

STATIC void
mvneta_tx_set_csumflag(struct ifnet *ifp,
    struct mvneta_tx_desc *t, struct mbuf *m)
{
	struct ether_header *eh;
	int csum_flags;
	uint32_t iphl, ipoff;
	struct ip *ip;

	iphl = ipoff = 0;
	csum_flags = ifp->if_hwassist & m->m_pkthdr.csum_flags;
	eh = mtod(m, struct ether_header *);
	switch (ntohs(eh->ether_type)) {
	case ETHERTYPE_IP:
		ipoff = ETHER_HDR_LEN;
		break;
	case ETHERTYPE_IPV6:
		return;
	case ETHERTYPE_VLAN:
		ipoff = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
		break;
	}

	if (__predict_true(csum_flags & (CSUM_IP|CSUM_IP_TCP|CSUM_IP_UDP))) {
		ip = (struct ip *)(m->m_data + ipoff);
		iphl = ip->ip_hl<<2;
		t->command |= MVNETA_TX_CMD_L3_IP4;
	} else {
		t->command |= MVNETA_TX_CMD_L4_CHECKSUM_NONE;
		return;
	}


	/* L3 */
	if (csum_flags & CSUM_IP) {
		t->command |= MVNETA_TX_CMD_IP4_CHECKSUM;
	}

	/* L4 */
	if (csum_flags & CSUM_IP_TCP) {
		t->command |= MVNETA_TX_CMD_L4_CHECKSUM_NOFRAG;
		t->command |= MVNETA_TX_CMD_L4_TCP;
	} else if (csum_flags & CSUM_IP_UDP) {
		t->command |= MVNETA_TX_CMD_L4_CHECKSUM_NOFRAG;
		t->command |= MVNETA_TX_CMD_L4_UDP;
	} else
		t->command |= MVNETA_TX_CMD_L4_CHECKSUM_NONE;

	t->l4ichk = 0;
	t->command |= MVNETA_TX_CMD_IP_HEADER_LEN(iphl >> 2);
	t->command |= MVNETA_TX_CMD_L3_OFFSET(ipoff);
}

STATIC void
mvneta_tx_queue_complete(struct mvneta_softc *sc, int q)
{
	struct mvneta_tx_ring *tx;
	struct mvneta_buf *txbuf;
	struct mvneta_tx_desc *t;
	uint32_t ptxs, ptxsu, ndesc;
	int i;

	KASSERT_TX_MTX(sc, q);

	tx = MVNETA_TX_RING(sc, q);
	if (__predict_false(tx->queue_status == MVNETA_QUEUE_DISABLED))
		return;

	ptxs = MVNETA_READ(sc, MVNETA_PTXS(q));
	ndesc = MVNETA_PTXS_GET_TBC(ptxs);

	if (__predict_false(ndesc == 0)) {
		if (tx->used == 0)
			tx->queue_status = MVNETA_QUEUE_IDLE;
		else if (tx->queue_status == MVNETA_QUEUE_WORKING &&
		    ((ticks - tx->watchdog_time) > MVNETA_WATCHDOG))
			tx->queue_hung = TRUE;
		return;
	}

#ifdef MVNETA_KTR
	CTR3(KTR_SPARE2, "%s:%u tx_complete begin ndesc=%u",
	    sc->ifp->if_xname, q, ndesc);
#endif

	bus_dmamap_sync(sc->tx_dtag, tx->desc_map,
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	for (i = 0; i < ndesc; i++) {
		t = &tx->desc[tx->dma];
#ifdef MVNETA_KTR
		if (t->flags & MVNETA_TX_F_ES)
			CTR3(KTR_SPARE2, "%s tx error queue %d desc %d",
			    sc->ifp->if_xname, q, tx->dma);
#endif
		txbuf = &tx->txbuf[tx->dma];
		if (__predict_true(txbuf->m != NULL)) {
			DASSERT((t->command & MVNETA_TX_CMD_F) != 0);
			bus_dmamap_unload(sc->txmbuf_dtag, txbuf->dmap);
			m_freem(txbuf->m);
			txbuf->m = NULL;
		}
		else
			DASSERT((t->flags & MVNETA_TX_CMD_F) == 0);
		tx->dma = tx_counter_adv(tx->dma, 1);
		tx->used--;
	}
	DASSERT(tx->used >= 0);
	DASSERT(tx->used <= MVNETA_TX_RING_CNT);
	while (__predict_false(ndesc > 255)) {
		ptxsu = MVNETA_PTXSU_NORB(255);
		MVNETA_WRITE(sc, MVNETA_PTXSU(q), ptxsu);
		ndesc -= 255;
	}
	if (__predict_true(ndesc > 0)) {
		ptxsu = MVNETA_PTXSU_NORB(ndesc);
		MVNETA_WRITE(sc, MVNETA_PTXSU(q), ptxsu);
	}
#ifdef MVNETA_KTR
	CTR5(KTR_SPARE2, "%s:%u tx_complete tx_cpu=%d tx_dma=%d tx_used=%d",
	    sc->ifp->if_xname, q, tx->cpu, tx->dma, tx->used);
#endif

	tx->watchdog_time = ticks;

	if (tx->used == 0)
		tx->queue_status = MVNETA_QUEUE_IDLE;
}

/*
 * Do a final TX complete when TX is idle.
 */
STATIC void
mvneta_tx_drain(struct mvneta_softc *sc)
{
	struct mvneta_tx_ring *tx;
	int q;

	/*
	 * Handle trailing mbuf on TX queue.
	 * Check is done lockess to avoid TX path contention.
	 */
	for (q = 0; q < MVNETA_TX_QNUM_MAX; q++) {
		tx = MVNETA_TX_RING(sc, q);
		if ((ticks - tx->watchdog_time) > MVNETA_WATCHDOG_TXCOMP &&
		    tx->used > 0) {
			mvneta_tx_lockq(sc, q);
			mvneta_tx_queue_complete(sc, q);
			mvneta_tx_unlockq(sc, q);
		}
	}
}

/*
 * Rx Subroutines
 */
STATIC int
mvneta_rx(struct mvneta_softc *sc, int q, int count)
{
	uint32_t prxs, npkt;
	int more;

	more = 0;
	mvneta_rx_lockq(sc, q);
	prxs = MVNETA_READ(sc, MVNETA_PRXS(q));
	npkt = MVNETA_PRXS_GET_ODC(prxs);
	if (__predict_false(npkt == 0))
		goto out;

	if (count > 0 && npkt > count) {
		more = 1;
		npkt = count;
	}
	mvneta_rx_queue(sc, q, npkt);
out:
	mvneta_rx_unlockq(sc, q);
	return more;
}

/*
 * Helper routine for updating PRXSU register of a given queue.
 * Handles number of processed descriptors bigger than maximum acceptable value.
 */
STATIC __inline void
mvneta_prxsu_update(struct mvneta_softc *sc, int q, int processed)
{
	uint32_t prxsu;

	while (__predict_false(processed > 255)) {
		prxsu = MVNETA_PRXSU_NOOFPROCESSEDDESCRIPTORS(255);
		MVNETA_WRITE(sc, MVNETA_PRXSU(q), prxsu);
		processed -= 255;
	}
	prxsu = MVNETA_PRXSU_NOOFPROCESSEDDESCRIPTORS(processed);
	MVNETA_WRITE(sc, MVNETA_PRXSU(q), prxsu);
}

static __inline void
mvneta_prefetch(void *p)
{

	__builtin_prefetch(p);
}

STATIC void
mvneta_rx_queue(struct mvneta_softc *sc, int q, int npkt)
{
	struct ifnet *ifp;
	struct mvneta_rx_ring *rx;
	struct mvneta_rx_desc *r;
	struct mvneta_buf *rxbuf;
	struct mbuf *m;
	struct lro_ctrl *lro;
	struct lro_entry *queued;
	void *pktbuf;
	int i, pktlen, processed, ndma;

	KASSERT_RX_MTX(sc, q);

	ifp = sc->ifp;
	rx = MVNETA_RX_RING(sc, q);
	processed = 0;

	if (__predict_false(rx->queue_status == MVNETA_QUEUE_DISABLED))
		return;

	bus_dmamap_sync(sc->rx_dtag, rx->desc_map,
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	for (i = 0; i < npkt; i++) {
		/* Prefetch next desc, rxbuf. */
		ndma = rx_counter_adv(rx->dma, 1);
		mvneta_prefetch(&rx->desc[ndma]);
		mvneta_prefetch(&rx->rxbuf[ndma]);

		/* get descriptor and packet */
		r = &rx->desc[rx->dma];
		rxbuf = &rx->rxbuf[rx->dma];
		m = rxbuf->m;
		rxbuf->m = NULL;
		DASSERT(m != NULL);
		bus_dmamap_sync(sc->rxbuf_dtag, rxbuf->dmap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->rxbuf_dtag, rxbuf->dmap);
		/* Prefetch mbuf header. */
		mvneta_prefetch(m);

		processed++;
		/* Drop desc with error status or not in a single buffer. */
		DASSERT((r->status & (MVNETA_RX_F|MVNETA_RX_L)) ==
		    (MVNETA_RX_F|MVNETA_RX_L));
		if (__predict_false((r->status & MVNETA_RX_ES) ||
		    (r->status & (MVNETA_RX_F|MVNETA_RX_L)) !=
		    (MVNETA_RX_F|MVNETA_RX_L)))
			goto rx_error;

		/*
		 * [ OFF | MH | PKT | CRC ]
		 * bytecnt cover MH, PKT, CRC
		 */
		pktlen = r->bytecnt - ETHER_CRC_LEN - MVNETA_HWHEADER_SIZE;
		pktbuf = (uint8_t *)rx->rxbuf_virt_addr[rx->dma] + MVNETA_PACKET_OFFSET +
                    MVNETA_HWHEADER_SIZE;

		/* Prefetch mbuf data. */
		mvneta_prefetch(pktbuf);

		/* Write value to mbuf (avoid read). */
		m->m_data = pktbuf;
		m->m_len = m->m_pkthdr.len = pktlen;
		m->m_pkthdr.rcvif = ifp;
		mvneta_rx_set_csumflag(ifp, r, m);

		/* Increase rx_dma before releasing the lock. */
		rx->dma = ndma;

		if (__predict_false(rx->lro_enabled &&
		    ((r->status & MVNETA_RX_L3_IP) != 0) &&
		    ((r->status & MVNETA_RX_L4_MASK) == MVNETA_RX_L4_TCP) &&
		    (m->m_pkthdr.csum_flags &
		    (CSUM_DATA_VALID | CSUM_PSEUDO_HDR)) ==
		    (CSUM_DATA_VALID | CSUM_PSEUDO_HDR))) {
			if (rx->lro.lro_cnt != 0) {
				if (tcp_lro_rx(&rx->lro, m, 0) == 0)
					goto rx_done;
			}
		}

		mvneta_rx_unlockq(sc, q);
		(*ifp->if_input)(ifp, m);
		mvneta_rx_lockq(sc, q);
		/*
		 * Check whether this queue has been disabled in the
		 * meantime. If yes, then clear LRO and exit.
		 */
		if(__predict_false(rx->queue_status == MVNETA_QUEUE_DISABLED))
			goto rx_lro;
rx_done:
		/* Refresh receive ring to avoid stall and minimize jitter. */
		if (processed >= MVNETA_RX_REFILL_COUNT) {
			mvneta_prxsu_update(sc, q, processed);
			mvneta_rx_queue_refill(sc, q);
			processed = 0;
		}
		continue;
rx_error:
		m_freem(m);
		rx->dma = ndma;
		/* Refresh receive ring to avoid stall and minimize jitter. */
		if (processed >= MVNETA_RX_REFILL_COUNT) {
			mvneta_prxsu_update(sc, q, processed);
			mvneta_rx_queue_refill(sc, q);
			processed = 0;
		}
	}
#ifdef MVNETA_KTR
	CTR3(KTR_SPARE2, "%s:%u %u packets received", ifp->if_xname, q, npkt);
#endif
	/* DMA status update */
	mvneta_prxsu_update(sc, q, processed);
	/* Refill the rest of buffers if there are any to refill */
	mvneta_rx_queue_refill(sc, q);

rx_lro:
	/*
	 * Flush any outstanding LRO work
	 */
	lro = &rx->lro;
	while (__predict_false((queued = LIST_FIRST(&lro->lro_active)) != NULL)) {
		LIST_REMOVE(LIST_FIRST((&lro->lro_active)), next);
		tcp_lro_flush(lro, queued);
	}
}

STATIC void
mvneta_rx_buf_free(struct mvneta_softc *sc, struct mvneta_buf *rxbuf)
{

	bus_dmamap_unload(sc->rxbuf_dtag, rxbuf->dmap);
	/* This will remove all data at once */
	m_freem(rxbuf->m);
}

STATIC void
mvneta_rx_queue_refill(struct mvneta_softc *sc, int q)
{
	struct mvneta_rx_ring *rx;
	struct mvneta_rx_desc *r;
	struct mvneta_buf *rxbuf;
	bus_dma_segment_t segs;
	struct mbuf *m;
	uint32_t prxs, prxsu, ndesc;
	int npkt, refill, nsegs, error;

	KASSERT_RX_MTX(sc, q);

	rx = MVNETA_RX_RING(sc, q);
	prxs = MVNETA_READ(sc, MVNETA_PRXS(q));
	ndesc = MVNETA_PRXS_GET_NODC(prxs) + MVNETA_PRXS_GET_ODC(prxs);
	refill = MVNETA_RX_RING_CNT - ndesc;
#ifdef MVNETA_KTR
	CTR3(KTR_SPARE2, "%s:%u refill %u packets", sc->ifp->if_xname, q,
	    refill);
#endif
	if (__predict_false(refill <= 0))
		return;

	for (npkt = 0; npkt < refill; npkt++) {
		rxbuf = &rx->rxbuf[rx->cpu];
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (__predict_false(m == NULL)) {
			error = ENOBUFS;
			break;
		}
		m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;

		error = bus_dmamap_load_mbuf_sg(sc->rxbuf_dtag, rxbuf->dmap,
		    m, &segs, &nsegs, BUS_DMA_NOWAIT);
		if (__predict_false(error != 0 || nsegs != 1)) {
			KASSERT(1, ("Failed to load Rx mbuf DMA map"));
			m_freem(m);
			break;
		}

		/* Add the packet to the ring */
		rxbuf->m = m;
		r = &rx->desc[rx->cpu];
		r->bufptr_pa = segs.ds_addr;
		rx->rxbuf_virt_addr[rx->cpu] = m->m_data;

		rx->cpu = rx_counter_adv(rx->cpu, 1);
	}
	if (npkt == 0) {
		if (refill == MVNETA_RX_RING_CNT)
			rx->needs_refill = TRUE;
		return;
	}

	rx->needs_refill = FALSE;
	bus_dmamap_sync(sc->rx_dtag, rx->desc_map, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	while (__predict_false(npkt > 255)) {
		prxsu = MVNETA_PRXSU_NOOFNEWDESCRIPTORS(255);
		MVNETA_WRITE(sc, MVNETA_PRXSU(q), prxsu);
		npkt -= 255;
	}
	if (__predict_true(npkt > 0)) {
		prxsu = MVNETA_PRXSU_NOOFNEWDESCRIPTORS(npkt);
		MVNETA_WRITE(sc, MVNETA_PRXSU(q), prxsu);
	}
}

STATIC __inline void
mvneta_rx_set_csumflag(struct ifnet *ifp,
    struct mvneta_rx_desc *r, struct mbuf *m)
{
	uint32_t csum_flags;

	csum_flags = 0;
	if (__predict_false((r->status &
	    (MVNETA_RX_IP_HEADER_OK|MVNETA_RX_L3_IP)) == 0))
		return; /* not a IP packet */

	/* L3 */
	if (__predict_true((r->status & MVNETA_RX_IP_HEADER_OK) ==
	    MVNETA_RX_IP_HEADER_OK))
		csum_flags |= CSUM_L3_CALC|CSUM_L3_VALID;

	if (__predict_true((r->status & (MVNETA_RX_IP_HEADER_OK|MVNETA_RX_L3_IP)) ==
	    (MVNETA_RX_IP_HEADER_OK|MVNETA_RX_L3_IP))) {
		/* L4 */
		switch (r->status & MVNETA_RX_L4_MASK) {
		case MVNETA_RX_L4_TCP:
		case MVNETA_RX_L4_UDP:
			csum_flags |= CSUM_L4_CALC;
			if (__predict_true((r->status &
			    MVNETA_RX_L4_CHECKSUM_OK) == MVNETA_RX_L4_CHECKSUM_OK)) {
				csum_flags |= CSUM_L4_VALID;
				m->m_pkthdr.csum_data = htons(0xffff);
			}
			break;
		case MVNETA_RX_L4_OTH:
		default:
			break;
		}
	}
	m->m_pkthdr.csum_flags = csum_flags;
}

/*
 * MAC address filter
 */
STATIC void
mvneta_filter_setup(struct mvneta_softc *sc)
{
	struct ifnet *ifp;
	uint32_t dfut[MVNETA_NDFUT], dfsmt[MVNETA_NDFSMT], dfomt[MVNETA_NDFOMT];
	uint32_t pxc;
	int i;

	KASSERT_SC_MTX(sc);

	memset(dfut, 0, sizeof(dfut));
	memset(dfsmt, 0, sizeof(dfsmt));
	memset(dfomt, 0, sizeof(dfomt));

	ifp = sc->ifp;
	ifp->if_flags |= IFF_ALLMULTI;
	if (ifp->if_flags & (IFF_ALLMULTI|IFF_PROMISC)) {
		for (i = 0; i < MVNETA_NDFSMT; i++) {
			dfsmt[i] = dfomt[i] =
			    MVNETA_DF(0, MVNETA_DF_QUEUE(0) | MVNETA_DF_PASS) |
			    MVNETA_DF(1, MVNETA_DF_QUEUE(0) | MVNETA_DF_PASS) |
			    MVNETA_DF(2, MVNETA_DF_QUEUE(0) | MVNETA_DF_PASS) |
			    MVNETA_DF(3, MVNETA_DF_QUEUE(0) | MVNETA_DF_PASS);
		}
	}

	pxc = MVNETA_READ(sc, MVNETA_PXC);
	pxc &= ~(MVNETA_PXC_UPM | MVNETA_PXC_RXQ_MASK | MVNETA_PXC_RXQARP_MASK |
	    MVNETA_PXC_TCPQ_MASK | MVNETA_PXC_UDPQ_MASK | MVNETA_PXC_BPDUQ_MASK);
	pxc |= MVNETA_PXC_RXQ(MVNETA_RX_QNUM_MAX-1);
	pxc |= MVNETA_PXC_RXQARP(MVNETA_RX_QNUM_MAX-1);
	pxc |= MVNETA_PXC_TCPQ(MVNETA_RX_QNUM_MAX-1);
	pxc |= MVNETA_PXC_UDPQ(MVNETA_RX_QNUM_MAX-1);
	pxc |= MVNETA_PXC_BPDUQ(MVNETA_RX_QNUM_MAX-1);
	pxc |= MVNETA_PXC_RB | MVNETA_PXC_RBIP | MVNETA_PXC_RBARP;
	if (ifp->if_flags & IFF_BROADCAST) {
		pxc &= ~(MVNETA_PXC_RB | MVNETA_PXC_RBIP | MVNETA_PXC_RBARP);
	}
	if (ifp->if_flags & IFF_PROMISC) {
		pxc |= MVNETA_PXC_UPM;
	}
	MVNETA_WRITE(sc, MVNETA_PXC, pxc);

	/* Set Destination Address Filter Unicast Table */
	if (ifp->if_flags & IFF_PROMISC) {
		/* pass all unicast addresses */
		for (i = 0; i < MVNETA_NDFUT; i++) {
			dfut[i] =
			    MVNETA_DF(0, MVNETA_DF_QUEUE(0) | MVNETA_DF_PASS) |
			    MVNETA_DF(1, MVNETA_DF_QUEUE(0) | MVNETA_DF_PASS) |
			    MVNETA_DF(2, MVNETA_DF_QUEUE(0) | MVNETA_DF_PASS) |
			    MVNETA_DF(3, MVNETA_DF_QUEUE(0) | MVNETA_DF_PASS);
		}
	} else {
		i = sc->enaddr[5] & 0xf;		/* last nibble */
		dfut[i>>2] = MVNETA_DF(i&3, MVNETA_DF_QUEUE(0) | MVNETA_DF_PASS);
	}
	MVNETA_WRITE_REGION(sc, MVNETA_DFUT(0), dfut, MVNETA_NDFUT);

	/* Set Destination Address Filter Multicast Tables */
	MVNETA_WRITE_REGION(sc, MVNETA_DFSMT(0), dfsmt, MVNETA_NDFSMT);
	MVNETA_WRITE_REGION(sc, MVNETA_DFOMT(0), dfomt, MVNETA_NDFOMT);
}

/*
 * sysctl(9)
 */
STATIC int
sysctl_read_mib(SYSCTL_HANDLER_ARGS)
{
	struct mvneta_sysctl_mib *arg;
	struct mvneta_softc *sc;
	uint64_t val;

	arg = (struct mvneta_sysctl_mib *)arg1;
	if (arg == NULL)
		return (EINVAL);

	sc = arg->sc;
	if (sc == NULL)
		return (EINVAL);
	if (arg->index < 0 || arg->index > MVNETA_PORTMIB_NOCOUNTER)
		return (EINVAL);

	mvneta_sc_lock(sc);
	val = arg->counter;
	mvneta_sc_unlock(sc);
	return sysctl_handle_64(oidp, &val, 0, req);
}


STATIC int
sysctl_clear_mib(SYSCTL_HANDLER_ARGS)
{
	struct mvneta_softc *sc;
	int err, val;

	val = 0;
	sc = (struct mvneta_softc *)arg1;
	if (sc == NULL)
		return (EINVAL);

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0)
		return (err);

	if (val < 0 || val > 1)
		return (EINVAL);

	if (val == 1) {
		mvneta_sc_lock(sc);
		mvneta_clear_mib(sc);
		mvneta_sc_unlock(sc);
	}

	return (0);
}

STATIC int
sysctl_set_queue_rxthtime(SYSCTL_HANDLER_ARGS)
{
	struct mvneta_sysctl_queue *arg;
	struct mvneta_rx_ring *rx;
	struct mvneta_softc *sc;
	uint32_t reg, time_mvtclk;
	int err, time_us;

	rx = NULL;
	arg = (struct mvneta_sysctl_queue *)arg1;
	if (arg == NULL)
		return (EINVAL);
	if (arg->queue < 0 || arg->queue > MVNETA_RX_RING_CNT)
		return (EINVAL);
	if (arg->rxtx != MVNETA_SYSCTL_RX)
		return (EINVAL);

	sc = arg->sc;
	if (sc == NULL)
		return (EINVAL);

	/* read queue length */
	mvneta_sc_lock(sc);
	mvneta_rx_lockq(sc, arg->queue);
	rx = MVNETA_RX_RING(sc, arg->queue);
	time_mvtclk = rx->queue_th_time;
	time_us = ((uint64_t)time_mvtclk * 1000ULL * 1000ULL) / mvneta_get_clk();
	mvneta_rx_unlockq(sc, arg->queue);
	mvneta_sc_unlock(sc);

	err = sysctl_handle_int(oidp, &time_us, 0, req);
	if (err != 0)
		return (err);

	mvneta_sc_lock(sc);
	mvneta_rx_lockq(sc, arg->queue);

	/* update queue length (0[sec] - 1[sec]) */
	if (time_us < 0 || time_us > (1000 * 1000)) {
		mvneta_rx_unlockq(sc, arg->queue);
		mvneta_sc_unlock(sc);
		return (EINVAL);
	}
	time_mvtclk =
	    (uint64_t)mvneta_get_clk() * (uint64_t)time_us / (1000ULL * 1000ULL);
	rx->queue_th_time = time_mvtclk;
	reg = MVNETA_PRXITTH_RITT(rx->queue_th_time);
	MVNETA_WRITE(sc, MVNETA_PRXITTH(arg->queue), reg);
	mvneta_rx_unlockq(sc, arg->queue);
	mvneta_sc_unlock(sc);

	return (0);
}

STATIC void
sysctl_mvneta_init(struct mvneta_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *children;
	struct sysctl_oid_list *rxchildren;
	struct sysctl_oid_list *qchildren, *mchildren;
	struct sysctl_oid *tree;
	int i, q;
	struct mvneta_sysctl_queue *rxarg;
#define	MVNETA_SYSCTL_NAME(num) "queue" # num
	static const char *sysctl_queue_names[] = {
		MVNETA_SYSCTL_NAME(0), MVNETA_SYSCTL_NAME(1),
		MVNETA_SYSCTL_NAME(2), MVNETA_SYSCTL_NAME(3),
		MVNETA_SYSCTL_NAME(4), MVNETA_SYSCTL_NAME(5),
		MVNETA_SYSCTL_NAME(6), MVNETA_SYSCTL_NAME(7),
	};
#undef MVNETA_SYSCTL_NAME

#ifndef NO_SYSCTL_DESCR
#define	MVNETA_SYSCTL_DESCR(num) "configuration parameters for queue " # num
	static const char *sysctl_queue_descrs[] = {
		MVNETA_SYSCTL_DESCR(0), MVNETA_SYSCTL_DESCR(1),
		MVNETA_SYSCTL_DESCR(2), MVNETA_SYSCTL_DESCR(3),
		MVNETA_SYSCTL_DESCR(4), MVNETA_SYSCTL_DESCR(5),
		MVNETA_SYSCTL_DESCR(6), MVNETA_SYSCTL_DESCR(7),
	};
#undef MVNETA_SYSCTL_DESCR
#endif


	ctx = device_get_sysctl_ctx(sc->dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));

	tree = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "rx",
	    CTLFLAG_RD, 0, "NETA RX");
	rxchildren = SYSCTL_CHILDREN(tree);
	tree = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "mib",
	    CTLFLAG_RD, 0, "NETA MIB");
	mchildren = SYSCTL_CHILDREN(tree);


	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "flow_control",
	    CTLFLAG_RW, &sc->cf_fc, 0, "flow control");
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "lpi",
	    CTLFLAG_RW, &sc->cf_lpi, 0, "Low Power Idle");

	/*
	 * MIB access
	 */
	/* dev.mvneta.[unit].mib.<mibs> */
	for (i = 0; i < MVNETA_PORTMIB_NOCOUNTER; i++) {
		struct mvneta_sysctl_mib *mib_arg = &sc->sysctl_mib[i];

		mib_arg->sc = sc;
		mib_arg->index = i;
		SYSCTL_ADD_PROC(ctx, mchildren, OID_AUTO,
		    mvneta_mib_list[i].sysctl_name,
		    CTLTYPE_U64|CTLFLAG_RD, (void *)mib_arg, 0,
		    sysctl_read_mib, "I", mvneta_mib_list[i].desc);
	}
	SYSCTL_ADD_UQUAD(ctx, mchildren, OID_AUTO, "rx_discard",
	    CTLFLAG_RD, &sc->counter_pdfc, "Port Rx Discard Frame Counter");
	SYSCTL_ADD_UQUAD(ctx, mchildren, OID_AUTO, "overrun",
	    CTLFLAG_RD, &sc->counter_pofc, "Port Overrun Frame Counter");
	SYSCTL_ADD_UINT(ctx, mchildren, OID_AUTO, "watchdog",
	    CTLFLAG_RD, &sc->counter_watchdog, 0, "TX Watchdog Counter");

	SYSCTL_ADD_PROC(ctx, mchildren, OID_AUTO, "reset",
	    CTLTYPE_INT|CTLFLAG_RW, (void *)sc, 0,
	    sysctl_clear_mib, "I", "Reset MIB counters");

	for (q = 0; q < MVNETA_RX_QNUM_MAX; q++) {
		rxarg = &sc->sysctl_rx_queue[q];

		rxarg->sc = sc;
		rxarg->queue = q;
		rxarg->rxtx = MVNETA_SYSCTL_RX;

		/* hw.mvneta.mvneta[unit].rx.[queue] */
		tree = SYSCTL_ADD_NODE(ctx, rxchildren, OID_AUTO,
		    sysctl_queue_names[q], CTLFLAG_RD, 0,
		    sysctl_queue_descrs[q]);
		qchildren = SYSCTL_CHILDREN(tree);

		/* hw.mvneta.mvneta[unit].rx.[queue].threshold_timer_us */
		SYSCTL_ADD_PROC(ctx, qchildren, OID_AUTO, "threshold_timer_us",
		    CTLTYPE_UINT | CTLFLAG_RW, rxarg, 0,
		    sysctl_set_queue_rxthtime, "I",
		    "interrupt coalescing threshold timer [us]");
	}
}

/*
 * MIB
 */
STATIC void
mvneta_clear_mib(struct mvneta_softc *sc)
{
	int i;

	KASSERT_SC_MTX(sc);

	for (i = 0; i < nitems(mvneta_mib_list); i++) {
		if (mvneta_mib_list[i].reg64)
			MVNETA_READ_MIB_8(sc, mvneta_mib_list[i].regnum);
		else
			MVNETA_READ_MIB_4(sc, mvneta_mib_list[i].regnum);
		sc->sysctl_mib[i].counter = 0;
	}
	MVNETA_READ(sc, MVNETA_PDFC);
	sc->counter_pdfc = 0;
	MVNETA_READ(sc, MVNETA_POFC);
	sc->counter_pofc = 0;
	sc->counter_watchdog = 0;
}

STATIC void
mvneta_update_mib(struct mvneta_softc *sc)
{
	struct mvneta_tx_ring *tx;
	int i;
	uint64_t val;
	uint32_t reg;

	for (i = 0; i < nitems(mvneta_mib_list); i++) {

		if (mvneta_mib_list[i].reg64)
			val = MVNETA_READ_MIB_8(sc, mvneta_mib_list[i].regnum);
		else
			val = MVNETA_READ_MIB_4(sc, mvneta_mib_list[i].regnum);

		if (val == 0)
			continue;

		sc->sysctl_mib[i].counter += val;
		switch (mvneta_mib_list[i].regnum) {
			case MVNETA_MIB_RX_GOOD_OCT:
				if_inc_counter(sc->ifp, IFCOUNTER_IBYTES, val);
				break;
			case MVNETA_MIB_RX_BAD_FRAME:
				if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, val);
				break;
			case MVNETA_MIB_RX_GOOD_FRAME:
				if_inc_counter(sc->ifp, IFCOUNTER_IPACKETS, val);
				break;
			case MVNETA_MIB_RX_MCAST_FRAME:
				if_inc_counter(sc->ifp, IFCOUNTER_IMCASTS, val);
				break;
			case MVNETA_MIB_TX_GOOD_OCT:
				if_inc_counter(sc->ifp, IFCOUNTER_OBYTES, val);
				break;
			case MVNETA_MIB_TX_GOOD_FRAME:
				if_inc_counter(sc->ifp, IFCOUNTER_OPACKETS, val);
				break;
			case MVNETA_MIB_TX_MCAST_FRAME:
				if_inc_counter(sc->ifp, IFCOUNTER_OMCASTS, val);
				break;
			case MVNETA_MIB_MAC_COL:
				if_inc_counter(sc->ifp, IFCOUNTER_COLLISIONS, val);
				break;
			case MVNETA_MIB_TX_MAC_TRNS_ERR:
			case MVNETA_MIB_TX_EXCES_COL:
			case MVNETA_MIB_MAC_LATE_COL:
				if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, val);
				break;
		}
	}

	reg = MVNETA_READ(sc, MVNETA_PDFC);
	sc->counter_pdfc += reg;
	if_inc_counter(sc->ifp, IFCOUNTER_IQDROPS, reg);
	reg = MVNETA_READ(sc, MVNETA_POFC);
	sc->counter_pofc += reg;
	if_inc_counter(sc->ifp, IFCOUNTER_IQDROPS, reg);

	/* TX watchdog. */
	if (sc->counter_watchdog_mib > 0) {
		if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, sc->counter_watchdog_mib);
		sc->counter_watchdog_mib = 0;
	}
	/*
	 * TX driver errors:
	 * We do not take queue locks to not disrupt TX path.
	 * We may only miss one drv error which will be fixed at
	 * next mib update. We may also clear counter when TX path
	 * is incrementing it but we only do it if counter was not zero
	 * thus we may only loose one error.
	 */
	for (i = 0; i < MVNETA_TX_QNUM_MAX; i++) {
		tx = MVNETA_TX_RING(sc, i);

		if (tx->drv_error > 0) {
			if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, tx->drv_error);
			tx->drv_error = 0;
		}
	}
}

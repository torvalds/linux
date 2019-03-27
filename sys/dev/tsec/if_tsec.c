/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2007-2008 Semihalf, Rafal Jaworowski
 * Copyright (C) 2006-2007 Semihalf, Piotr Kruszynski
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
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Freescale integrated Three-Speed Ethernet Controller (TSEC) driver.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <machine/bus.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/tsec/if_tsec.h>
#include <dev/tsec/if_tsecreg.h>

static int	tsec_alloc_dma_desc(device_t dev, bus_dma_tag_t *dtag,
    bus_dmamap_t *dmap, bus_size_t dsize, void **vaddr, void *raddr,
    const char *dname);
static void	tsec_dma_ctl(struct tsec_softc *sc, int state);
static void	 tsec_encap(struct ifnet *ifp, struct tsec_softc *sc,
    struct mbuf *m0, uint16_t fcb_flags, int *start_tx);
static void	tsec_free_dma(struct tsec_softc *sc);
static void	tsec_free_dma_desc(bus_dma_tag_t dtag, bus_dmamap_t dmap, void *vaddr);
static int	tsec_ifmedia_upd(struct ifnet *ifp);
static void	tsec_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr);
static int	tsec_new_rxbuf(bus_dma_tag_t tag, bus_dmamap_t map,
    struct mbuf **mbufp, uint32_t *paddr);
static void	tsec_map_dma_addr(void *arg, bus_dma_segment_t *segs,
    int nseg, int error);
static void	tsec_intrs_ctl(struct tsec_softc *sc, int state);
static void	tsec_init(void *xsc);
static void	tsec_init_locked(struct tsec_softc *sc);
static int	tsec_ioctl(struct ifnet *ifp, u_long command, caddr_t data);
static void	tsec_reset_mac(struct tsec_softc *sc);
static void	tsec_setfilter(struct tsec_softc *sc);
static void	tsec_set_mac_address(struct tsec_softc *sc);
static void	tsec_start(struct ifnet *ifp);
static void	tsec_start_locked(struct ifnet *ifp);
static void	tsec_stop(struct tsec_softc *sc);
static void	tsec_tick(void *arg);
static void	tsec_watchdog(struct tsec_softc *sc);
static void	tsec_add_sysctls(struct tsec_softc *sc);
static int	tsec_sysctl_ic_time(SYSCTL_HANDLER_ARGS);
static int	tsec_sysctl_ic_count(SYSCTL_HANDLER_ARGS);
static void	tsec_set_rxic(struct tsec_softc *sc);
static void	tsec_set_txic(struct tsec_softc *sc);
static int	tsec_receive_intr_locked(struct tsec_softc *sc, int count);
static void	tsec_transmit_intr_locked(struct tsec_softc *sc);
static void	tsec_error_intr_locked(struct tsec_softc *sc, int count);
static void	tsec_offload_setup(struct tsec_softc *sc);
static void	tsec_offload_process_frame(struct tsec_softc *sc,
    struct mbuf *m);
static void	tsec_setup_multicast(struct tsec_softc *sc);
static int	tsec_set_mtu(struct tsec_softc *sc, unsigned int mtu);

devclass_t tsec_devclass;
DRIVER_MODULE(miibus, tsec, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(tsec, ether, 1, 1, 1);
MODULE_DEPEND(tsec, miibus, 1, 1, 1);

struct mtx tsec_phy_mtx;

int
tsec_attach(struct tsec_softc *sc)
{
	uint8_t hwaddr[ETHER_ADDR_LEN];
	struct ifnet *ifp;
	int error = 0;
	int i;

	/* Initialize global (because potentially shared) MII lock */
	if (!mtx_initialized(&tsec_phy_mtx))
		mtx_init(&tsec_phy_mtx, "tsec mii", NULL, MTX_DEF);

	/* Reset all TSEC counters */
	TSEC_TX_RX_COUNTERS_INIT(sc);

	/* Stop DMA engine if enabled by firmware */
	tsec_dma_ctl(sc, 0);

	/* Reset MAC */
	tsec_reset_mac(sc);

	/* Disable interrupts for now */
	tsec_intrs_ctl(sc, 0);

	/* Configure defaults for interrupts coalescing */
	sc->rx_ic_time = 768;
	sc->rx_ic_count = 16;
	sc->tx_ic_time = 768;
	sc->tx_ic_count = 16;
	tsec_set_rxic(sc);
	tsec_set_txic(sc);
	tsec_add_sysctls(sc);

	/* Allocate a busdma tag and DMA safe memory for TX descriptors. */
	error = tsec_alloc_dma_desc(sc->dev, &sc->tsec_tx_dtag,
	    &sc->tsec_tx_dmap, sizeof(*sc->tsec_tx_vaddr) * TSEC_TX_NUM_DESC,
	    (void **)&sc->tsec_tx_vaddr, &sc->tsec_tx_raddr, "TX");

	if (error) {
		tsec_detach(sc);
		return (ENXIO);
	}

	/* Allocate a busdma tag and DMA safe memory for RX descriptors. */
	error = tsec_alloc_dma_desc(sc->dev, &sc->tsec_rx_dtag,
	    &sc->tsec_rx_dmap, sizeof(*sc->tsec_rx_vaddr) * TSEC_RX_NUM_DESC,
	    (void **)&sc->tsec_rx_vaddr, &sc->tsec_rx_raddr, "RX");
	if (error) {
		tsec_detach(sc);
		return (ENXIO);
	}

	/* Allocate a busdma tag for TX mbufs. */
	error = bus_dma_tag_create(NULL,	/* parent */
	    TSEC_TXBUFFER_ALIGNMENT, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    MCLBYTES * (TSEC_TX_NUM_DESC - 1),	/* maxsize */
	    TSEC_TX_MAX_DMA_SEGS,		/* nsegments */
	    MCLBYTES, 0,			/* maxsegsz, flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &sc->tsec_tx_mtag);			/* dmat */
	if (error) {
		device_printf(sc->dev, "failed to allocate busdma tag "
		    "(tx mbufs)\n");
		tsec_detach(sc);
		return (ENXIO);
	}

	/* Allocate a busdma tag for RX mbufs. */
	error = bus_dma_tag_create(NULL,	/* parent */
	    TSEC_RXBUFFER_ALIGNMENT, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    MCLBYTES,				/* maxsize */
	    1,					/* nsegments */
	    MCLBYTES, 0,			/* maxsegsz, flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &sc->tsec_rx_mtag);			/* dmat */
	if (error) {
		device_printf(sc->dev, "failed to allocate busdma tag "
		    "(rx mbufs)\n");
		tsec_detach(sc);
		return (ENXIO);
	}

	/* Create TX busdma maps */
	for (i = 0; i < TSEC_TX_NUM_DESC; i++) {
		error = bus_dmamap_create(sc->tsec_tx_mtag, 0,
		   &sc->tx_bufmap[i].map);
		if (error) {
			device_printf(sc->dev, "failed to init TX ring\n");
			tsec_detach(sc);
			return (ENXIO);
		}
		sc->tx_bufmap[i].map_initialized = 1;
	}

	/* Create RX busdma maps and zero mbuf handlers */
	for (i = 0; i < TSEC_RX_NUM_DESC; i++) {
		error = bus_dmamap_create(sc->tsec_rx_mtag, 0,
		    &sc->rx_data[i].map);
		if (error) {
			device_printf(sc->dev, "failed to init RX ring\n");
			tsec_detach(sc);
			return (ENXIO);
		}
		sc->rx_data[i].mbuf = NULL;
	}

	/* Create mbufs for RX buffers */
	for (i = 0; i < TSEC_RX_NUM_DESC; i++) {
		error = tsec_new_rxbuf(sc->tsec_rx_mtag, sc->rx_data[i].map,
		    &sc->rx_data[i].mbuf, &sc->rx_data[i].paddr);
		if (error) {
			device_printf(sc->dev, "can't load rx DMA map %d, "
			    "error = %d\n", i, error);
			tsec_detach(sc);
			return (error);
		}
	}

	/* Create network interface for upper layers */
	ifp = sc->tsec_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(sc->dev, "if_alloc() failed\n");
		tsec_detach(sc);
		return (ENOMEM);
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(sc->dev), device_get_unit(sc->dev));
	ifp->if_flags = IFF_SIMPLEX | IFF_MULTICAST | IFF_BROADCAST;
	ifp->if_init = tsec_init;
	ifp->if_start = tsec_start;
	ifp->if_ioctl = tsec_ioctl;

	IFQ_SET_MAXLEN(&ifp->if_snd, TSEC_TX_NUM_DESC - 1);
	ifp->if_snd.ifq_drv_maxlen = TSEC_TX_NUM_DESC - 1;
	IFQ_SET_READY(&ifp->if_snd);

	ifp->if_capabilities = IFCAP_VLAN_MTU;
	if (sc->is_etsec)
		ifp->if_capabilities |= IFCAP_HWCSUM;

	ifp->if_capenable = ifp->if_capabilities;

#ifdef DEVICE_POLLING
	/* Advertise that polling is supported */
	ifp->if_capabilities |= IFCAP_POLLING;
#endif
	
	/* Attach PHY(s) */
	error = mii_attach(sc->dev, &sc->tsec_miibus, ifp, tsec_ifmedia_upd,
	    tsec_ifmedia_sts, BMSR_DEFCAPMASK, sc->phyaddr, MII_OFFSET_ANY,
	    0);
	if (error) {
		device_printf(sc->dev, "attaching PHYs failed\n");
		if_free(ifp);
		sc->tsec_ifp = NULL;
		tsec_detach(sc);
		return (error);
	}
	sc->tsec_mii = device_get_softc(sc->tsec_miibus);

	/* Set MAC address */
	tsec_get_hwaddr(sc, hwaddr);
	ether_ifattach(ifp, hwaddr);

	return (0);
}

int
tsec_detach(struct tsec_softc *sc)
{

	if (sc->tsec_ifp != NULL) {
#ifdef DEVICE_POLLING
		if (sc->tsec_ifp->if_capenable & IFCAP_POLLING)
			ether_poll_deregister(sc->tsec_ifp);
#endif

		/* Stop TSEC controller and free TX queue */
		if (sc->sc_rres)
			tsec_shutdown(sc->dev);

		/* Detach network interface */
		ether_ifdetach(sc->tsec_ifp);
		if_free(sc->tsec_ifp);
		sc->tsec_ifp = NULL;
	}

	/* Free DMA resources */
	tsec_free_dma(sc);

	return (0);
}

int
tsec_shutdown(device_t dev)
{
	struct tsec_softc *sc;

	sc = device_get_softc(dev);

	TSEC_GLOBAL_LOCK(sc);
	tsec_stop(sc);
	TSEC_GLOBAL_UNLOCK(sc);
	return (0);
}

int
tsec_suspend(device_t dev)
{

	/* TODO not implemented! */
	return (0);
}

int
tsec_resume(device_t dev)
{

	/* TODO not implemented! */
	return (0);
}

static void
tsec_init(void *xsc)
{
	struct tsec_softc *sc = xsc;

	TSEC_GLOBAL_LOCK(sc);
	tsec_init_locked(sc);
	TSEC_GLOBAL_UNLOCK(sc);
}

static int
tsec_mii_wait(struct tsec_softc *sc, uint32_t flags)
{
	int timeout;

	/*
	 * The status indicators are not set immediatly after a command.
	 * Discard the first value.
	 */
	TSEC_PHY_READ(sc, TSEC_REG_MIIMIND);

	timeout = TSEC_READ_RETRY;
	while ((TSEC_PHY_READ(sc, TSEC_REG_MIIMIND) & flags) && --timeout)
		DELAY(TSEC_READ_DELAY);

	return (timeout == 0);
}


static void
tsec_init_locked(struct tsec_softc *sc)
{
	struct tsec_desc *tx_desc = sc->tsec_tx_vaddr;
	struct tsec_desc *rx_desc = sc->tsec_rx_vaddr;
	struct ifnet *ifp = sc->tsec_ifp;
	uint32_t val, i;
	int timeout;

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	TSEC_GLOBAL_LOCK_ASSERT(sc);
	tsec_stop(sc);

	/*
	 * These steps are according to the MPC8555E PowerQUICCIII RM:
	 * 14.7 Initialization/Application Information
	 */

	/* Step 1: soft reset MAC */
	tsec_reset_mac(sc);

	/* Step 2: Initialize MACCFG2 */
	TSEC_WRITE(sc, TSEC_REG_MACCFG2,
	    TSEC_MACCFG2_FULLDUPLEX |	/* Full Duplex = 1 */
	    TSEC_MACCFG2_PADCRC |	/* PAD/CRC append */
	    TSEC_MACCFG2_GMII |		/* I/F Mode bit */
	    TSEC_MACCFG2_PRECNT		/* Preamble count = 7 */
	);

	/* Step 3: Initialize ECNTRL
	 * While the documentation states that R100M is ignored if RPM is
	 * not set, it does seem to be needed to get the orange boxes to
	 * work (which have a Marvell 88E1111 PHY). Go figure.
	 */

	/*
	 * XXX kludge - use circumstancial evidence to program ECNTRL
	 * correctly. Ideally we need some board information to guide
	 * us here.
	 */
	i = TSEC_READ(sc, TSEC_REG_ID2);
	val = (i & 0xffff)
	    ? (TSEC_ECNTRL_TBIM | TSEC_ECNTRL_SGMIIM)	/* Sumatra */
	    : TSEC_ECNTRL_R100M;			/* Orange + CDS */
	TSEC_WRITE(sc, TSEC_REG_ECNTRL, TSEC_ECNTRL_STEN | val);

	/* Step 4: Initialize MAC station address */
	tsec_set_mac_address(sc);

	/*
	 * Step 5: Assign a Physical address to the TBI so as to not conflict
	 * with the external PHY physical address
	 */
	TSEC_WRITE(sc, TSEC_REG_TBIPA, 5);

	TSEC_PHY_LOCK(sc);

	/* Step 6: Reset the management interface */
	TSEC_PHY_WRITE(sc, TSEC_REG_MIIMCFG, TSEC_MIIMCFG_RESETMGMT);

	/* Step 7: Setup the MII Mgmt clock speed */
	TSEC_PHY_WRITE(sc, TSEC_REG_MIIMCFG, TSEC_MIIMCFG_CLKDIV28);

	/* Step 8: Read MII Mgmt indicator register and check for Busy = 0 */
	timeout = tsec_mii_wait(sc, TSEC_MIIMIND_BUSY);

	TSEC_PHY_UNLOCK(sc);
	if (timeout) {
		if_printf(ifp, "tsec_init_locked(): Mgmt busy timeout\n");
		return;
	}

	/* Step 9: Setup the MII Mgmt */
	mii_mediachg(sc->tsec_mii);

	/* Step 10: Clear IEVENT register */
	TSEC_WRITE(sc, TSEC_REG_IEVENT, 0xffffffff);

	/* Step 11: Enable interrupts */
#ifdef DEVICE_POLLING
	/*
	 * ...only if polling is not turned on. Disable interrupts explicitly
	 * if polling is enabled.
	 */
	if (ifp->if_capenable & IFCAP_POLLING )
		tsec_intrs_ctl(sc, 0);
	else
#endif /* DEVICE_POLLING */
	tsec_intrs_ctl(sc, 1);

	/* Step 12: Initialize IADDRn */
	TSEC_WRITE(sc, TSEC_REG_IADDR0, 0);
	TSEC_WRITE(sc, TSEC_REG_IADDR1, 0);
	TSEC_WRITE(sc, TSEC_REG_IADDR2, 0);
	TSEC_WRITE(sc, TSEC_REG_IADDR3, 0);
	TSEC_WRITE(sc, TSEC_REG_IADDR4, 0);
	TSEC_WRITE(sc, TSEC_REG_IADDR5, 0);
	TSEC_WRITE(sc, TSEC_REG_IADDR6, 0);
	TSEC_WRITE(sc, TSEC_REG_IADDR7, 0);

	/* Step 13: Initialize GADDRn */
	TSEC_WRITE(sc, TSEC_REG_GADDR0, 0);
	TSEC_WRITE(sc, TSEC_REG_GADDR1, 0);
	TSEC_WRITE(sc, TSEC_REG_GADDR2, 0);
	TSEC_WRITE(sc, TSEC_REG_GADDR3, 0);
	TSEC_WRITE(sc, TSEC_REG_GADDR4, 0);
	TSEC_WRITE(sc, TSEC_REG_GADDR5, 0);
	TSEC_WRITE(sc, TSEC_REG_GADDR6, 0);
	TSEC_WRITE(sc, TSEC_REG_GADDR7, 0);

	/* Step 14: Initialize RCTRL */
	TSEC_WRITE(sc, TSEC_REG_RCTRL, 0);

	/* Step 15: Initialize DMACTRL */
	tsec_dma_ctl(sc, 1);

	/* Step 16: Initialize FIFO_PAUSE_CTRL */
	TSEC_WRITE(sc, TSEC_REG_FIFO_PAUSE_CTRL, TSEC_FIFO_PAUSE_CTRL_EN);

	/*
	 * Step 17: Initialize transmit/receive descriptor rings.
	 * Initialize TBASE and RBASE.
	 */
	TSEC_WRITE(sc, TSEC_REG_TBASE, sc->tsec_tx_raddr);
	TSEC_WRITE(sc, TSEC_REG_RBASE, sc->tsec_rx_raddr);

	for (i = 0; i < TSEC_TX_NUM_DESC; i++) {
		tx_desc[i].bufptr = 0;
		tx_desc[i].length = 0;
		tx_desc[i].flags = ((i == TSEC_TX_NUM_DESC - 1) ?
		    TSEC_TXBD_W : 0);
	}
	bus_dmamap_sync(sc->tsec_tx_dtag, sc->tsec_tx_dmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	for (i = 0; i < TSEC_RX_NUM_DESC; i++) {
		rx_desc[i].bufptr = sc->rx_data[i].paddr;
		rx_desc[i].length = 0;
		rx_desc[i].flags = TSEC_RXBD_E | TSEC_RXBD_I |
		    ((i == TSEC_RX_NUM_DESC - 1) ? TSEC_RXBD_W : 0);
	}
	bus_dmamap_sync(sc->tsec_rx_dtag, sc->tsec_rx_dmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Step 18: Initialize the maximum receive buffer length */
	TSEC_WRITE(sc, TSEC_REG_MRBLR, MCLBYTES);

	/* Step 19: Configure ethernet frame sizes */
	TSEC_WRITE(sc, TSEC_REG_MINFLR, TSEC_MIN_FRAME_SIZE);
	tsec_set_mtu(sc, ifp->if_mtu);

	/* Step 20: Enable Rx and RxBD sdata snooping */
	TSEC_WRITE(sc, TSEC_REG_ATTR, TSEC_ATTR_RDSEN | TSEC_ATTR_RBDSEN);
	TSEC_WRITE(sc, TSEC_REG_ATTRELI, 0);

	/* Step 21: Reset collision counters in hardware */
	TSEC_WRITE(sc, TSEC_REG_MON_TSCL, 0);
	TSEC_WRITE(sc, TSEC_REG_MON_TMCL, 0);
	TSEC_WRITE(sc, TSEC_REG_MON_TLCL, 0);
	TSEC_WRITE(sc, TSEC_REG_MON_TXCL, 0);
	TSEC_WRITE(sc, TSEC_REG_MON_TNCL, 0);

	/* Step 22: Mask all CAM interrupts */
	TSEC_WRITE(sc, TSEC_REG_MON_CAM1, 0xffffffff);
	TSEC_WRITE(sc, TSEC_REG_MON_CAM2, 0xffffffff);

	/* Step 23: Enable Rx and Tx */
	val = TSEC_READ(sc, TSEC_REG_MACCFG1);
	val |= (TSEC_MACCFG1_RX_EN | TSEC_MACCFG1_TX_EN);
	TSEC_WRITE(sc, TSEC_REG_MACCFG1, val);

	/* Step 24: Reset TSEC counters for Tx and Rx rings */
	TSEC_TX_RX_COUNTERS_INIT(sc);

	/* Step 25: Setup TCP/IP Off-Load engine */
	if (sc->is_etsec)
		tsec_offload_setup(sc);

	/* Step 26: Setup multicast filters */
	tsec_setup_multicast(sc);
	
	/* Step 27: Activate network interface */
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	sc->tsec_if_flags = ifp->if_flags;
	sc->tsec_watchdog = 0;

	/* Schedule watchdog timeout */
	callout_reset(&sc->tsec_callout, hz, tsec_tick, sc);
}

static void
tsec_set_mac_address(struct tsec_softc *sc)
{
	uint32_t macbuf[2] = { 0, 0 };
	char *macbufp, *curmac;
	int i;

	TSEC_GLOBAL_LOCK_ASSERT(sc);

	KASSERT((ETHER_ADDR_LEN <= sizeof(macbuf)),
	    ("tsec_set_mac_address: (%d <= %zd", ETHER_ADDR_LEN,
	    sizeof(macbuf)));

	macbufp = (char *)macbuf;
	curmac = (char *)IF_LLADDR(sc->tsec_ifp);

	/* Correct order of MAC address bytes */
	for (i = 1; i <= ETHER_ADDR_LEN; i++)
		macbufp[ETHER_ADDR_LEN-i] = curmac[i-1];

	/* Initialize MAC station address MACSTNADDR2 and MACSTNADDR1 */
	TSEC_WRITE(sc, TSEC_REG_MACSTNADDR2, macbuf[1]);
	TSEC_WRITE(sc, TSEC_REG_MACSTNADDR1, macbuf[0]);
}

/*
 * DMA control function, if argument state is:
 * 0 - DMA engine will be disabled
 * 1 - DMA engine will be enabled
 */
static void
tsec_dma_ctl(struct tsec_softc *sc, int state)
{
	device_t dev;
	uint32_t dma_flags, timeout;

	dev = sc->dev;

	dma_flags = TSEC_READ(sc, TSEC_REG_DMACTRL);

	switch (state) {
	case 0:
		/* Temporarily clear stop graceful stop bits. */
		tsec_dma_ctl(sc, 1000);

		/* Set it again */
		dma_flags |= (TSEC_DMACTRL_GRS | TSEC_DMACTRL_GTS);
		break;
	case 1000:
	case 1:
		/* Set write with response (WWR), wait (WOP) and snoop bits */
		dma_flags |= (TSEC_DMACTRL_TDSEN | TSEC_DMACTRL_TBDSEN |
		    DMACTRL_WWR | DMACTRL_WOP);

		/* Clear graceful stop bits */
		dma_flags &= ~(TSEC_DMACTRL_GRS | TSEC_DMACTRL_GTS);
		break;
	default:
		device_printf(dev, "tsec_dma_ctl(): unknown state value: %d\n",
		    state);
	}

	TSEC_WRITE(sc, TSEC_REG_DMACTRL, dma_flags);

	switch (state) {
	case 0:
		/* Wait for DMA stop */
		timeout = TSEC_READ_RETRY;
		while (--timeout && (!(TSEC_READ(sc, TSEC_REG_IEVENT) &
		    (TSEC_IEVENT_GRSC | TSEC_IEVENT_GTSC))))
			DELAY(TSEC_READ_DELAY);

		if (timeout == 0)
			device_printf(dev, "tsec_dma_ctl(): timeout!\n");
		break;
	case 1:
		/* Restart transmission function */
		TSEC_WRITE(sc, TSEC_REG_TSTAT, TSEC_TSTAT_THLT);
	}
}

/*
 * Interrupts control function, if argument state is:
 * 0 - all TSEC interrupts will be masked
 * 1 - all TSEC interrupts will be unmasked
 */
static void
tsec_intrs_ctl(struct tsec_softc *sc, int state)
{
	device_t dev;

	dev = sc->dev;

	switch (state) {
	case 0:
		TSEC_WRITE(sc, TSEC_REG_IMASK, 0);
		break;
	case 1:
		TSEC_WRITE(sc, TSEC_REG_IMASK, TSEC_IMASK_BREN |
		    TSEC_IMASK_RXCEN | TSEC_IMASK_BSYEN | TSEC_IMASK_EBERREN |
		    TSEC_IMASK_BTEN | TSEC_IMASK_TXEEN | TSEC_IMASK_TXBEN |
		    TSEC_IMASK_TXFEN | TSEC_IMASK_XFUNEN | TSEC_IMASK_RXFEN);
		break;
	default:
		device_printf(dev, "tsec_intrs_ctl(): unknown state value: %d\n",
		    state);
	}
}

static void
tsec_reset_mac(struct tsec_softc *sc)
{
	uint32_t maccfg1_flags;

	/* Set soft reset bit */
	maccfg1_flags = TSEC_READ(sc, TSEC_REG_MACCFG1);
	maccfg1_flags |= TSEC_MACCFG1_SOFT_RESET;
	TSEC_WRITE(sc, TSEC_REG_MACCFG1, maccfg1_flags);

	/* Clear soft reset bit */
	maccfg1_flags = TSEC_READ(sc, TSEC_REG_MACCFG1);
	maccfg1_flags &= ~TSEC_MACCFG1_SOFT_RESET;
	TSEC_WRITE(sc, TSEC_REG_MACCFG1, maccfg1_flags);
}

static void
tsec_watchdog(struct tsec_softc *sc)
{
	struct ifnet *ifp;

	TSEC_GLOBAL_LOCK_ASSERT(sc);

	if (sc->tsec_watchdog == 0 || --sc->tsec_watchdog > 0)
		return;

	ifp = sc->tsec_ifp;
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	if_printf(ifp, "watchdog timeout\n");

	tsec_stop(sc);
	tsec_init_locked(sc);
}

static void
tsec_start(struct ifnet *ifp)
{
	struct tsec_softc *sc = ifp->if_softc;

	TSEC_TRANSMIT_LOCK(sc);
	tsec_start_locked(ifp);
	TSEC_TRANSMIT_UNLOCK(sc);
}

static void
tsec_start_locked(struct ifnet *ifp)
{
	struct tsec_softc *sc;
	struct mbuf *m0;
	struct tsec_tx_fcb *tx_fcb;
	int csum_flags;
	int start_tx;
	uint16_t fcb_flags;

	sc = ifp->if_softc;
	start_tx = 0;

	TSEC_TRANSMIT_LOCK_ASSERT(sc);

	if (sc->tsec_link == 0)
		return;

	bus_dmamap_sync(sc->tsec_tx_dtag, sc->tsec_tx_dmap,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (;;) {

		if (TSEC_FREE_TX_DESC(sc) < TSEC_TX_MAX_DMA_SEGS) {
			/* No free descriptors */
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		/* Get packet from the queue */
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m0);
		if (m0 == NULL)
			break;

		/* Insert TCP/IP Off-load frame control block */
		fcb_flags = 0;
		csum_flags = m0->m_pkthdr.csum_flags;
		if (csum_flags) {
			M_PREPEND(m0, sizeof(struct tsec_tx_fcb), M_NOWAIT);
			if (m0 == NULL)
				break;

			if (csum_flags & CSUM_IP)
				fcb_flags |= TSEC_TX_FCB_IP4 |
				    TSEC_TX_FCB_CSUM_IP;

			if (csum_flags & CSUM_TCP)
				fcb_flags |= TSEC_TX_FCB_TCP |
				    TSEC_TX_FCB_CSUM_TCP_UDP;

			if (csum_flags & CSUM_UDP)
				fcb_flags |= TSEC_TX_FCB_UDP |
				    TSEC_TX_FCB_CSUM_TCP_UDP;

			tx_fcb = mtod(m0, struct tsec_tx_fcb *);
			tx_fcb->flags = fcb_flags;
			tx_fcb->l3_offset = ETHER_HDR_LEN;
			tx_fcb->l4_offset = sizeof(struct ip);
		}

		tsec_encap(ifp, sc, m0, fcb_flags, &start_tx);
	}
	bus_dmamap_sync(sc->tsec_tx_dtag, sc->tsec_tx_dmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if (start_tx) {
		/* Enable transmitter and watchdog timer */
		TSEC_WRITE(sc, TSEC_REG_TSTAT, TSEC_TSTAT_THLT);
		sc->tsec_watchdog = 5;
	}
}

static void
tsec_encap(struct ifnet *ifp, struct tsec_softc *sc, struct mbuf *m0,
    uint16_t fcb_flags, int *start_tx)
{
	bus_dma_segment_t segs[TSEC_TX_MAX_DMA_SEGS];
	int error, i, nsegs;
	struct tsec_bufmap *tx_bufmap;
	uint32_t tx_idx;
	uint16_t flags;

	TSEC_TRANSMIT_LOCK_ASSERT(sc);

	tx_idx = sc->tx_idx_head;
	tx_bufmap = &sc->tx_bufmap[tx_idx];
 
	/* Create mapping in DMA memory */
	error = bus_dmamap_load_mbuf_sg(sc->tsec_tx_mtag, tx_bufmap->map, m0,
	    segs, &nsegs, BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		/* Too many segments!  Defrag and try again. */
		struct mbuf *m = m_defrag(m0, M_NOWAIT);

		if (m == NULL) {
			m_freem(m0);
			return;
		}
		m0 = m;
		error = bus_dmamap_load_mbuf_sg(sc->tsec_tx_mtag,
		    tx_bufmap->map, m0, segs, &nsegs, BUS_DMA_NOWAIT);
	}
	if (error != 0) {
		/* Give up. */
		m_freem(m0);
		return;
	}

	bus_dmamap_sync(sc->tsec_tx_mtag, tx_bufmap->map,
	    BUS_DMASYNC_PREWRITE);
	tx_bufmap->mbuf = m0;
 
	/*
	 * Fill in the TX descriptors back to front so that READY bit in first
	 * descriptor is set last.
	 */
	tx_idx = (tx_idx + (uint32_t)nsegs) & (TSEC_TX_NUM_DESC - 1);
	sc->tx_idx_head = tx_idx;
	flags = TSEC_TXBD_L | TSEC_TXBD_I | TSEC_TXBD_R | TSEC_TXBD_TC;
	for (i = nsegs - 1; i >= 0; i--) {
		struct tsec_desc *tx_desc;

		tx_idx = (tx_idx - 1) & (TSEC_TX_NUM_DESC - 1);
		tx_desc = &sc->tsec_tx_vaddr[tx_idx];
		tx_desc->length = segs[i].ds_len;
		tx_desc->bufptr = segs[i].ds_addr;

		if (i == 0) {
			wmb();

			if (fcb_flags != 0)
				flags |= TSEC_TXBD_TOE;
		}

		/*
		 * Set flags:
		 *   - wrap
		 *   - checksum
		 *   - ready to send
		 *   - transmit the CRC sequence after the last data byte
		 *   - interrupt after the last buffer
		 */
		tx_desc->flags = (tx_idx == (TSEC_TX_NUM_DESC - 1) ?
		    TSEC_TXBD_W : 0) | flags;

		flags &= ~(TSEC_TXBD_L | TSEC_TXBD_I);
	}

	BPF_MTAP(ifp, m0);
	*start_tx = 1;
}

static void
tsec_setfilter(struct tsec_softc *sc)
{
	struct ifnet *ifp;
	uint32_t flags;

	ifp = sc->tsec_ifp;
	flags = TSEC_READ(sc, TSEC_REG_RCTRL);

	/* Promiscuous mode */
	if (ifp->if_flags & IFF_PROMISC)
		flags |= TSEC_RCTRL_PROM;
	else
		flags &= ~TSEC_RCTRL_PROM;

	TSEC_WRITE(sc, TSEC_REG_RCTRL, flags);
}

#ifdef DEVICE_POLLING
static poll_handler_t tsec_poll;

static int
tsec_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	uint32_t ie;
	struct tsec_softc *sc = ifp->if_softc;
	int rx_npkts;

	rx_npkts = 0;

	TSEC_GLOBAL_LOCK(sc);
	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		TSEC_GLOBAL_UNLOCK(sc);
		return (rx_npkts);
	}

	if (cmd == POLL_AND_CHECK_STATUS) {
		tsec_error_intr_locked(sc, count);

		/* Clear all events reported */
		ie = TSEC_READ(sc, TSEC_REG_IEVENT);
		TSEC_WRITE(sc, TSEC_REG_IEVENT, ie);
	}

	tsec_transmit_intr_locked(sc);

	TSEC_GLOBAL_TO_RECEIVE_LOCK(sc);

	rx_npkts = tsec_receive_intr_locked(sc, count);

	TSEC_RECEIVE_UNLOCK(sc);

	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

static int
tsec_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct tsec_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int mask, error = 0;

	switch (command) {
	case SIOCSIFMTU:
		TSEC_GLOBAL_LOCK(sc);
		if (tsec_set_mtu(sc, ifr->ifr_mtu))
			ifp->if_mtu = ifr->ifr_mtu;
		else
			error = EINVAL;
		TSEC_GLOBAL_UNLOCK(sc);
		break;
	case SIOCSIFFLAGS:
		TSEC_GLOBAL_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				if ((sc->tsec_if_flags ^ ifp->if_flags) &
				    IFF_PROMISC)
					tsec_setfilter(sc);

				if ((sc->tsec_if_flags ^ ifp->if_flags) &
				    IFF_ALLMULTI)
					tsec_setup_multicast(sc);
			} else
				tsec_init_locked(sc);
		} else if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			tsec_stop(sc);

		sc->tsec_if_flags = ifp->if_flags;
		TSEC_GLOBAL_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			TSEC_GLOBAL_LOCK(sc);
			tsec_setup_multicast(sc);
			TSEC_GLOBAL_UNLOCK(sc);
		}
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->tsec_mii->mii_media,
		    command);
		break;
	case SIOCSIFCAP:
		mask = ifp->if_capenable ^ ifr->ifr_reqcap;
		if ((mask & IFCAP_HWCSUM) && sc->is_etsec) {
			TSEC_GLOBAL_LOCK(sc);
			ifp->if_capenable &= ~IFCAP_HWCSUM;
			ifp->if_capenable |= IFCAP_HWCSUM & ifr->ifr_reqcap;
			tsec_offload_setup(sc);
			TSEC_GLOBAL_UNLOCK(sc);
		}
#ifdef DEVICE_POLLING
		if (mask & IFCAP_POLLING) {
			if (ifr->ifr_reqcap & IFCAP_POLLING) {
				error = ether_poll_register(tsec_poll, ifp);
				if (error)
					return (error);

				TSEC_GLOBAL_LOCK(sc);
				/* Disable interrupts */
				tsec_intrs_ctl(sc, 0);
				ifp->if_capenable |= IFCAP_POLLING;
				TSEC_GLOBAL_UNLOCK(sc);
			} else {
				error = ether_poll_deregister(ifp);
				TSEC_GLOBAL_LOCK(sc);
				/* Enable interrupts */
				tsec_intrs_ctl(sc, 1);
				ifp->if_capenable &= ~IFCAP_POLLING;
				TSEC_GLOBAL_UNLOCK(sc);
			}
		}
#endif
		break;

	default:
		error = ether_ioctl(ifp, command, data);
	}

	/* Flush buffers if not empty */
	if (ifp->if_flags & IFF_UP)
		tsec_start(ifp);
	return (error);
}

static int
tsec_ifmedia_upd(struct ifnet *ifp)
{
	struct tsec_softc *sc = ifp->if_softc;
	struct mii_data *mii;

	TSEC_TRANSMIT_LOCK(sc);

	mii = sc->tsec_mii;
	mii_mediachg(mii);

	TSEC_TRANSMIT_UNLOCK(sc);
	return (0);
}

static void
tsec_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct tsec_softc *sc = ifp->if_softc;
	struct mii_data *mii;

	TSEC_TRANSMIT_LOCK(sc);

	mii = sc->tsec_mii;
	mii_pollstat(mii);

	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;

	TSEC_TRANSMIT_UNLOCK(sc);
}

static int
tsec_new_rxbuf(bus_dma_tag_t tag, bus_dmamap_t map, struct mbuf **mbufp,
    uint32_t *paddr)
{
	struct mbuf *new_mbuf;
	bus_dma_segment_t seg[1];
	int error, nsegs;

	KASSERT(mbufp != NULL, ("NULL mbuf pointer!"));

	new_mbuf = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MCLBYTES);
	if (new_mbuf == NULL)
		return (ENOBUFS);
	new_mbuf->m_len = new_mbuf->m_pkthdr.len = new_mbuf->m_ext.ext_size;

	if (*mbufp) {
		bus_dmamap_sync(tag, map, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(tag, map);
	}

	error = bus_dmamap_load_mbuf_sg(tag, map, new_mbuf, seg, &nsegs,
	    BUS_DMA_NOWAIT);
	KASSERT(nsegs == 1, ("Too many segments returned!"));
	if (nsegs != 1 || error)
		panic("tsec_new_rxbuf(): nsegs(%d), error(%d)", nsegs, error);

#if 0
	if (error) {
		printf("tsec: bus_dmamap_load_mbuf_sg() returned: %d!\n",
			error);
		m_freem(new_mbuf);
		return (ENOBUFS);
	}
#endif

#if 0
	KASSERT(((seg->ds_addr) & (TSEC_RXBUFFER_ALIGNMENT-1)) == 0,
		("Wrong alignment of RX buffer!"));
#endif
	bus_dmamap_sync(tag, map, BUS_DMASYNC_PREREAD);

	(*mbufp) = new_mbuf;
	(*paddr) = seg->ds_addr;
	return (0);
}

static void
tsec_map_dma_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	u_int32_t *paddr;

	KASSERT(nseg == 1, ("wrong number of segments, should be 1"));
	paddr = arg;
	*paddr = segs->ds_addr;
}

static int
tsec_alloc_dma_desc(device_t dev, bus_dma_tag_t *dtag, bus_dmamap_t *dmap,
    bus_size_t dsize, void **vaddr, void *raddr, const char *dname)
{
	int error;

	/* Allocate a busdma tag and DMA safe memory for TX/RX descriptors. */
	error = bus_dma_tag_create(NULL,	/* parent */
	    PAGE_SIZE, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    dsize, 1,				/* maxsize, nsegments */
	    dsize, 0,				/* maxsegsz, flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    dtag);				/* dmat */

	if (error) {
		device_printf(dev, "failed to allocate busdma %s tag\n",
		    dname);
		(*vaddr) = NULL;
		return (ENXIO);
	}

	error = bus_dmamem_alloc(*dtag, vaddr, BUS_DMA_NOWAIT | BUS_DMA_ZERO,
	    dmap);
	if (error) {
		device_printf(dev, "failed to allocate %s DMA safe memory\n",
		    dname);
		bus_dma_tag_destroy(*dtag);
		(*vaddr) = NULL;
		return (ENXIO);
	}

	error = bus_dmamap_load(*dtag, *dmap, *vaddr, dsize,
	    tsec_map_dma_addr, raddr, BUS_DMA_NOWAIT);
	if (error) {
		device_printf(dev, "cannot get address of the %s "
		    "descriptors\n", dname);
		bus_dmamem_free(*dtag, *vaddr, *dmap);
		bus_dma_tag_destroy(*dtag);
		(*vaddr) = NULL;
		return (ENXIO);
	}

	return (0);
}

static void
tsec_free_dma_desc(bus_dma_tag_t dtag, bus_dmamap_t dmap, void *vaddr)
{

	if (vaddr == NULL)
		return;

	/* Unmap descriptors from DMA memory */
	bus_dmamap_sync(dtag, dmap, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(dtag, dmap);

	/* Free descriptors memory */
	bus_dmamem_free(dtag, vaddr, dmap);

	/* Destroy descriptors tag */
	bus_dma_tag_destroy(dtag);
}

static void
tsec_free_dma(struct tsec_softc *sc)
{
	int i;

	/* Free TX maps */
	for (i = 0; i < TSEC_TX_NUM_DESC; i++)
		if (sc->tx_bufmap[i].map_initialized)
			bus_dmamap_destroy(sc->tsec_tx_mtag,
			    sc->tx_bufmap[i].map);
	/* Destroy tag for TX mbufs */
	bus_dma_tag_destroy(sc->tsec_tx_mtag);

	/* Free RX mbufs and maps */
	for (i = 0; i < TSEC_RX_NUM_DESC; i++) {
		if (sc->rx_data[i].mbuf) {
			/* Unload buffer from DMA */
			bus_dmamap_sync(sc->tsec_rx_mtag, sc->rx_data[i].map,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->tsec_rx_mtag,
			    sc->rx_data[i].map);

			/* Free buffer */
			m_freem(sc->rx_data[i].mbuf);
		}
		/* Destroy map for this buffer */
		if (sc->rx_data[i].map != NULL)
			bus_dmamap_destroy(sc->tsec_rx_mtag,
			    sc->rx_data[i].map);
	}
	/* Destroy tag for RX mbufs */
	bus_dma_tag_destroy(sc->tsec_rx_mtag);

	/* Unload TX/RX descriptors */
	tsec_free_dma_desc(sc->tsec_tx_dtag, sc->tsec_tx_dmap,
	    sc->tsec_tx_vaddr);
	tsec_free_dma_desc(sc->tsec_rx_dtag, sc->tsec_rx_dmap,
	    sc->tsec_rx_vaddr);
}

static void
tsec_stop(struct tsec_softc *sc)
{
	struct ifnet *ifp;
	uint32_t tmpval;

	TSEC_GLOBAL_LOCK_ASSERT(sc);

	ifp = sc->tsec_ifp;

	/* Disable interface and watchdog timer */
	callout_stop(&sc->tsec_callout);
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->tsec_watchdog = 0;

	/* Disable all interrupts and stop DMA */
	tsec_intrs_ctl(sc, 0);
	tsec_dma_ctl(sc, 0);

	/* Remove pending data from TX queue */
	while (sc->tx_idx_tail != sc->tx_idx_head) {
		bus_dmamap_sync(sc->tsec_tx_mtag,
		    sc->tx_bufmap[sc->tx_idx_tail].map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->tsec_tx_mtag,
		    sc->tx_bufmap[sc->tx_idx_tail].map);
		m_freem(sc->tx_bufmap[sc->tx_idx_tail].mbuf);
		sc->tx_idx_tail = (sc->tx_idx_tail + 1)
		    & (TSEC_TX_NUM_DESC - 1);
	}

	/* Disable RX and TX */
	tmpval = TSEC_READ(sc, TSEC_REG_MACCFG1);
	tmpval &= ~(TSEC_MACCFG1_RX_EN | TSEC_MACCFG1_TX_EN);
	TSEC_WRITE(sc, TSEC_REG_MACCFG1, tmpval);
	DELAY(10);
}

static void
tsec_tick(void *arg)
{
	struct tsec_softc *sc = arg;
	struct ifnet *ifp;
	int link;

	TSEC_GLOBAL_LOCK(sc);

	tsec_watchdog(sc);

	ifp = sc->tsec_ifp;
	link = sc->tsec_link;

	mii_tick(sc->tsec_mii);

	if (link == 0 && sc->tsec_link == 1 &&
	    (!IFQ_DRV_IS_EMPTY(&ifp->if_snd)))
		tsec_start_locked(ifp);

	/* Schedule another timeout one second from now. */
	callout_reset(&sc->tsec_callout, hz, tsec_tick, sc);

	TSEC_GLOBAL_UNLOCK(sc);
}

/*
 *  This is the core RX routine. It replenishes mbufs in the descriptor and
 *  sends data which have been dma'ed into host memory to upper layer.
 *
 *  Loops at most count times if count is > 0, or until done if count < 0.
 */
static int
tsec_receive_intr_locked(struct tsec_softc *sc, int count)
{
	struct tsec_desc *rx_desc;
	struct ifnet *ifp;
	struct rx_data_type *rx_data;
	struct mbuf *m;
	uint32_t i;
	int c, rx_npkts;
	uint16_t flags;

	TSEC_RECEIVE_LOCK_ASSERT(sc);

	ifp = sc->tsec_ifp;
	rx_data = sc->rx_data;
	rx_npkts = 0;

	bus_dmamap_sync(sc->tsec_rx_dtag, sc->tsec_rx_dmap,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (c = 0; ; c++) {
		if (count >= 0 && count-- == 0)
			break;

		rx_desc = TSEC_GET_CUR_RX_DESC(sc);
		flags = rx_desc->flags;

		/* Check if there is anything to receive */
		if ((flags & TSEC_RXBD_E) || (c >= TSEC_RX_NUM_DESC)) {
			/*
			 * Avoid generating another interrupt
			 */
			if (flags & TSEC_RXBD_E)
				TSEC_WRITE(sc, TSEC_REG_IEVENT,
				    TSEC_IEVENT_RXB | TSEC_IEVENT_RXF);
			/*
			 * We didn't consume current descriptor and have to
			 * return it to the queue
			 */
			TSEC_BACK_CUR_RX_DESC(sc);
			break;
		}

		if (flags & (TSEC_RXBD_LG | TSEC_RXBD_SH | TSEC_RXBD_NO |
		    TSEC_RXBD_CR | TSEC_RXBD_OV | TSEC_RXBD_TR)) {

			rx_desc->length = 0;
			rx_desc->flags = (rx_desc->flags &
			    ~TSEC_RXBD_ZEROONINIT) | TSEC_RXBD_E | TSEC_RXBD_I;

			if (sc->frame != NULL) {
				m_free(sc->frame);
				sc->frame = NULL;
			}

			continue;
		}

		/* Ok... process frame */
		i = TSEC_GET_CUR_RX_DESC_CNT(sc);
		m = rx_data[i].mbuf;
		m->m_len = rx_desc->length;

		if (sc->frame != NULL) {
			if ((flags & TSEC_RXBD_L) != 0)
				m->m_len -= m_length(sc->frame, NULL);

			m->m_flags &= ~M_PKTHDR;
			m_cat(sc->frame, m);
		} else {
			sc->frame = m;
		}

		m = NULL;

		if ((flags & TSEC_RXBD_L) != 0) {
			m = sc->frame;
			sc->frame = NULL;
		}

		if (tsec_new_rxbuf(sc->tsec_rx_mtag, rx_data[i].map,
		    &rx_data[i].mbuf, &rx_data[i].paddr)) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			/*
			 * We ran out of mbufs; didn't consume current
			 * descriptor and have to return it to the queue.
			 */
			TSEC_BACK_CUR_RX_DESC(sc);
			break;
		}

		/* Attach new buffer to descriptor and clear flags */
		rx_desc->bufptr = rx_data[i].paddr;
		rx_desc->length = 0;
		rx_desc->flags = (rx_desc->flags & ~TSEC_RXBD_ZEROONINIT) |
		    TSEC_RXBD_E | TSEC_RXBD_I;

		if (m != NULL) {
			m->m_pkthdr.rcvif = ifp;

			m_fixhdr(m);
			m_adj(m, -ETHER_CRC_LEN);

			if (sc->is_etsec)
				tsec_offload_process_frame(sc, m);

			TSEC_RECEIVE_UNLOCK(sc);
			(*ifp->if_input)(ifp, m);
			TSEC_RECEIVE_LOCK(sc);
			rx_npkts++;
		}
	}

	bus_dmamap_sync(sc->tsec_rx_dtag, sc->tsec_rx_dmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/*
	 * Make sure TSEC receiver is not halted.
	 *
	 * Various conditions can stop the TSEC receiver, but not all are
	 * signaled and handled by error interrupt, so make sure the receiver
	 * is running. Writing to TSEC_REG_RSTAT restarts the receiver when
	 * halted, and is harmless if already running.
	 */
	TSEC_WRITE(sc, TSEC_REG_RSTAT, TSEC_RSTAT_QHLT);
	return (rx_npkts);
}

void
tsec_receive_intr(void *arg)
{
	struct tsec_softc *sc = arg;

	TSEC_RECEIVE_LOCK(sc);

#ifdef DEVICE_POLLING
	if (sc->tsec_ifp->if_capenable & IFCAP_POLLING) {
		TSEC_RECEIVE_UNLOCK(sc);
		return;
	}
#endif

	/* Confirm the interrupt was received by driver */
	TSEC_WRITE(sc, TSEC_REG_IEVENT, TSEC_IEVENT_RXB | TSEC_IEVENT_RXF);
	tsec_receive_intr_locked(sc, -1);

	TSEC_RECEIVE_UNLOCK(sc);
}

static void
tsec_transmit_intr_locked(struct tsec_softc *sc)
{
	struct ifnet *ifp;
	uint32_t tx_idx;

	TSEC_TRANSMIT_LOCK_ASSERT(sc);

	ifp = sc->tsec_ifp;

	/* Update collision statistics */
	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, TSEC_READ(sc, TSEC_REG_MON_TNCL));

	/* Reset collision counters in hardware */
	TSEC_WRITE(sc, TSEC_REG_MON_TSCL, 0);
	TSEC_WRITE(sc, TSEC_REG_MON_TMCL, 0);
	TSEC_WRITE(sc, TSEC_REG_MON_TLCL, 0);
	TSEC_WRITE(sc, TSEC_REG_MON_TXCL, 0);
	TSEC_WRITE(sc, TSEC_REG_MON_TNCL, 0);

	bus_dmamap_sync(sc->tsec_tx_dtag, sc->tsec_tx_dmap,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	tx_idx = sc->tx_idx_tail;
	while (tx_idx != sc->tx_idx_head) {
		struct tsec_desc *tx_desc;
		struct tsec_bufmap *tx_bufmap;

		tx_desc = &sc->tsec_tx_vaddr[tx_idx];
		if (tx_desc->flags & TSEC_TXBD_R) {
			break;
		}

		tx_bufmap = &sc->tx_bufmap[tx_idx];
		tx_idx = (tx_idx + 1) & (TSEC_TX_NUM_DESC - 1);
		if (tx_bufmap->mbuf == NULL)
			continue;

		/*
		 * This is the last buf in this packet, so unmap and free it.
		 */
		bus_dmamap_sync(sc->tsec_tx_mtag, tx_bufmap->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->tsec_tx_mtag, tx_bufmap->map);
		m_freem(tx_bufmap->mbuf);
		tx_bufmap->mbuf = NULL;

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	}
	sc->tx_idx_tail = tx_idx;
	bus_dmamap_sync(sc->tsec_tx_dtag, sc->tsec_tx_dmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	tsec_start_locked(ifp);

	if (sc->tx_idx_tail == sc->tx_idx_head)
		sc->tsec_watchdog = 0;
}

void
tsec_transmit_intr(void *arg)
{
	struct tsec_softc *sc = arg;

	TSEC_TRANSMIT_LOCK(sc);

#ifdef DEVICE_POLLING
	if (sc->tsec_ifp->if_capenable & IFCAP_POLLING) {
		TSEC_TRANSMIT_UNLOCK(sc);
		return;
	}
#endif
	/* Confirm the interrupt was received by driver */
	TSEC_WRITE(sc, TSEC_REG_IEVENT, TSEC_IEVENT_TXB | TSEC_IEVENT_TXF);
	tsec_transmit_intr_locked(sc);

	TSEC_TRANSMIT_UNLOCK(sc);
}

static void
tsec_error_intr_locked(struct tsec_softc *sc, int count)
{
	struct ifnet *ifp;
	uint32_t eflags;

	TSEC_GLOBAL_LOCK_ASSERT(sc);

	ifp = sc->tsec_ifp;

	eflags = TSEC_READ(sc, TSEC_REG_IEVENT);

	/* Clear events bits in hardware */
	TSEC_WRITE(sc, TSEC_REG_IEVENT, TSEC_IEVENT_RXC | TSEC_IEVENT_BSY |
	    TSEC_IEVENT_EBERR | TSEC_IEVENT_MSRO | TSEC_IEVENT_BABT |
	    TSEC_IEVENT_TXC | TSEC_IEVENT_TXE | TSEC_IEVENT_LC |
	    TSEC_IEVENT_CRL | TSEC_IEVENT_XFUN);

	/* Check transmitter errors */
	if (eflags & TSEC_IEVENT_TXE) {
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

		if (eflags & TSEC_IEVENT_LC)
			if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);

		TSEC_WRITE(sc, TSEC_REG_TSTAT, TSEC_TSTAT_THLT);
	}

	/* Check for discarded frame due to a lack of buffers */
	if (eflags & TSEC_IEVENT_BSY) {
		if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
	}

	if (ifp->if_flags & IFF_DEBUG)
		if_printf(ifp, "tsec_error_intr(): event flags: 0x%x\n",
		    eflags);

	if (eflags & TSEC_IEVENT_EBERR) {
		if_printf(ifp, "System bus error occurred during"
		    "DMA transaction (flags: 0x%x)\n", eflags);
		tsec_init_locked(sc);
	}

	if (eflags & TSEC_IEVENT_BABT)
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	if (eflags & TSEC_IEVENT_BABR)
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
}

void
tsec_error_intr(void *arg)
{
	struct tsec_softc *sc = arg;

	TSEC_GLOBAL_LOCK(sc);
	tsec_error_intr_locked(sc, -1);
	TSEC_GLOBAL_UNLOCK(sc);
}

int
tsec_miibus_readreg(device_t dev, int phy, int reg)
{
	struct tsec_softc *sc;
	int timeout;
	int rv;

	sc = device_get_softc(dev);

	TSEC_PHY_LOCK();
	TSEC_PHY_WRITE(sc, TSEC_REG_MIIMADD, (phy << 8) | reg);
	TSEC_PHY_WRITE(sc, TSEC_REG_MIIMCOM, 0);
	TSEC_PHY_WRITE(sc, TSEC_REG_MIIMCOM, TSEC_MIIMCOM_READCYCLE);

	timeout = tsec_mii_wait(sc, TSEC_MIIMIND_NOTVALID | TSEC_MIIMIND_BUSY);
	rv = TSEC_PHY_READ(sc, TSEC_REG_MIIMSTAT);
	TSEC_PHY_UNLOCK();

	if (timeout)
		device_printf(dev, "Timeout while reading from PHY!\n");

	return (rv);
}

int
tsec_miibus_writereg(device_t dev, int phy, int reg, int value)
{
	struct tsec_softc *sc;
	int timeout;

	sc = device_get_softc(dev);

	TSEC_PHY_LOCK();
	TSEC_PHY_WRITE(sc, TSEC_REG_MIIMADD, (phy << 8) | reg);
	TSEC_PHY_WRITE(sc, TSEC_REG_MIIMCON, value);
	timeout = tsec_mii_wait(sc, TSEC_MIIMIND_BUSY);
	TSEC_PHY_UNLOCK();

	if (timeout)
		device_printf(dev, "Timeout while writing to PHY!\n");

	return (0);
}

void
tsec_miibus_statchg(device_t dev)
{
	struct tsec_softc *sc;
	struct mii_data *mii;
	uint32_t ecntrl, id, tmp;
	int link;

	sc = device_get_softc(dev);
	mii = sc->tsec_mii;
	link = ((mii->mii_media_status & IFM_ACTIVE) ? 1 : 0);

	tmp = TSEC_READ(sc, TSEC_REG_MACCFG2) & ~TSEC_MACCFG2_IF;

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX)
		tmp |= TSEC_MACCFG2_FULLDUPLEX;
	else
		tmp &= ~TSEC_MACCFG2_FULLDUPLEX;

	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_1000_T:
	case IFM_1000_SX:
		tmp |= TSEC_MACCFG2_GMII;
		sc->tsec_link = link;
		break;
	case IFM_100_TX:
	case IFM_10_T:
		tmp |= TSEC_MACCFG2_MII;
		sc->tsec_link = link;
		break;
	case IFM_NONE:
		if (link)
			device_printf(dev, "No speed selected but link "
			    "active!\n");
		sc->tsec_link = 0;
		return;
	default:
		sc->tsec_link = 0;
		device_printf(dev, "Unknown speed (%d), link %s!\n",
		    IFM_SUBTYPE(mii->mii_media_active),
		        ((link) ? "up" : "down"));
		return;
	}
	TSEC_WRITE(sc, TSEC_REG_MACCFG2, tmp);

	/* XXX kludge - use circumstantial evidence for reduced mode. */
	id = TSEC_READ(sc, TSEC_REG_ID2);
	if (id & 0xffff) {
		ecntrl = TSEC_READ(sc, TSEC_REG_ECNTRL) & ~TSEC_ECNTRL_R100M;
		ecntrl |= (tmp & TSEC_MACCFG2_MII) ? TSEC_ECNTRL_R100M : 0;
		TSEC_WRITE(sc, TSEC_REG_ECNTRL, ecntrl);
	}
}

static void
tsec_add_sysctls(struct tsec_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *children;
	struct sysctl_oid *tree;

	ctx = device_get_sysctl_ctx(sc->dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));
	tree = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "int_coal",
	    CTLFLAG_RD, 0, "TSEC Interrupts coalescing");
	children = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_time",
	    CTLTYPE_UINT | CTLFLAG_RW, sc, TSEC_IC_RX, tsec_sysctl_ic_time,
	    "I", "IC RX time threshold (0-65535)");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_count",
	    CTLTYPE_UINT | CTLFLAG_RW, sc, TSEC_IC_RX, tsec_sysctl_ic_count,
	    "I", "IC RX frame count threshold (0-255)");

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tx_time",
	    CTLTYPE_UINT | CTLFLAG_RW, sc, TSEC_IC_TX, tsec_sysctl_ic_time,
	    "I", "IC TX time threshold (0-65535)");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tx_count",
	    CTLTYPE_UINT | CTLFLAG_RW, sc, TSEC_IC_TX, tsec_sysctl_ic_count,
	    "I", "IC TX frame count threshold (0-255)");
}

/*
 * With Interrupt Coalescing (IC) active, a transmit/receive frame
 * interrupt is raised either upon:
 *
 * - threshold-defined period of time elapsed, or
 * - threshold-defined number of frames is received/transmitted,
 *   whichever occurs first.
 *
 * The following sysctls regulate IC behaviour (for TX/RX separately):
 *
 * dev.tsec.<unit>.int_coal.rx_time
 * dev.tsec.<unit>.int_coal.rx_count
 * dev.tsec.<unit>.int_coal.tx_time
 * dev.tsec.<unit>.int_coal.tx_count
 *
 * Values:
 *
 * - 0 for either time or count disables IC on the given TX/RX path
 *
 * - count: 1-255 (expresses frame count number; note that value of 1 is
 *   effectively IC off)
 *
 * - time: 1-65535 (value corresponds to a real time period and is
 *   expressed in units equivalent to 64 TSEC interface clocks, i.e. one timer
 *   threshold unit is 26.5 us, 2.56 us, or 512 ns, corresponding to 10 Mbps,
 *   100 Mbps, or 1Gbps, respectively. For detailed discussion consult the
 *   TSEC reference manual.
 */
static int
tsec_sysctl_ic_time(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t time;
	struct tsec_softc *sc = (struct tsec_softc *)arg1;

	time = (arg2 == TSEC_IC_RX) ? sc->rx_ic_time : sc->tx_ic_time;

	error = sysctl_handle_int(oidp, &time, 0, req);
	if (error != 0)
		return (error);

	if (time > 65535)
		return (EINVAL);

	TSEC_IC_LOCK(sc);
	if (arg2 == TSEC_IC_RX) {
		sc->rx_ic_time = time;
		tsec_set_rxic(sc);
	} else {
		sc->tx_ic_time = time;
		tsec_set_txic(sc);
	}
	TSEC_IC_UNLOCK(sc);

	return (0);
}

static int
tsec_sysctl_ic_count(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t count;
	struct tsec_softc *sc = (struct tsec_softc *)arg1;

	count = (arg2 == TSEC_IC_RX) ? sc->rx_ic_count : sc->tx_ic_count;

	error = sysctl_handle_int(oidp, &count, 0, req);
	if (error != 0)
		return (error);

	if (count > 255)
		return (EINVAL);

	TSEC_IC_LOCK(sc);
	if (arg2 == TSEC_IC_RX) {
		sc->rx_ic_count = count;
		tsec_set_rxic(sc);
	} else {
		sc->tx_ic_count = count;
		tsec_set_txic(sc);
	}
	TSEC_IC_UNLOCK(sc);

	return (0);
}

static void
tsec_set_rxic(struct tsec_softc *sc)
{
	uint32_t rxic_val;

	if (sc->rx_ic_count == 0 || sc->rx_ic_time == 0)
		/* Disable RX IC */
		rxic_val = 0;
	else {
		rxic_val = 0x80000000;
		rxic_val |= (sc->rx_ic_count << 21);
		rxic_val |= sc->rx_ic_time;
	}

	TSEC_WRITE(sc, TSEC_REG_RXIC, rxic_val);
}

static void
tsec_set_txic(struct tsec_softc *sc)
{
	uint32_t txic_val;

	if (sc->tx_ic_count == 0 || sc->tx_ic_time == 0)
		/* Disable TX IC */
		txic_val = 0;
	else {
		txic_val = 0x80000000;
		txic_val |= (sc->tx_ic_count << 21);
		txic_val |= sc->tx_ic_time;
	}

	TSEC_WRITE(sc, TSEC_REG_TXIC, txic_val);
}

static void
tsec_offload_setup(struct tsec_softc *sc)
{
	struct ifnet *ifp = sc->tsec_ifp;
	uint32_t reg;

	TSEC_GLOBAL_LOCK_ASSERT(sc);

	reg = TSEC_READ(sc, TSEC_REG_TCTRL);
	reg |= TSEC_TCTRL_IPCSEN | TSEC_TCTRL_TUCSEN;

	if (ifp->if_capenable & IFCAP_TXCSUM)
		ifp->if_hwassist = TSEC_CHECKSUM_FEATURES;
	else
		ifp->if_hwassist = 0;

	TSEC_WRITE(sc, TSEC_REG_TCTRL, reg);

	reg = TSEC_READ(sc, TSEC_REG_RCTRL);
	reg &= ~(TSEC_RCTRL_IPCSEN | TSEC_RCTRL_TUCSEN | TSEC_RCTRL_PRSDEP);
	reg |= TSEC_RCTRL_PRSDEP_PARSE_L2 | TSEC_RCTRL_VLEX;

	if (ifp->if_capenable & IFCAP_RXCSUM)
		reg |= TSEC_RCTRL_IPCSEN | TSEC_RCTRL_TUCSEN |
		    TSEC_RCTRL_PRSDEP_PARSE_L234;

	TSEC_WRITE(sc, TSEC_REG_RCTRL, reg);
}


static void
tsec_offload_process_frame(struct tsec_softc *sc, struct mbuf *m)
{
	struct tsec_rx_fcb rx_fcb;
	int csum_flags = 0;
	int protocol, flags;

	TSEC_RECEIVE_LOCK_ASSERT(sc);

	m_copydata(m, 0, sizeof(struct tsec_rx_fcb), (caddr_t)(&rx_fcb));
	flags = rx_fcb.flags;
	protocol = rx_fcb.protocol;

	if (TSEC_RX_FCB_IP_CSUM_CHECKED(flags)) {
		csum_flags |= CSUM_IP_CHECKED;

		if ((flags & TSEC_RX_FCB_IP_CSUM_ERROR) == 0)
			csum_flags |= CSUM_IP_VALID;
	}

	if ((protocol == IPPROTO_TCP || protocol == IPPROTO_UDP) &&
	    TSEC_RX_FCB_TCP_UDP_CSUM_CHECKED(flags) &&
	    (flags & TSEC_RX_FCB_TCP_UDP_CSUM_ERROR) == 0) {

		csum_flags |= CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
		m->m_pkthdr.csum_data = 0xFFFF;
	}

	m->m_pkthdr.csum_flags = csum_flags;

	if (flags & TSEC_RX_FCB_VLAN) {
		m->m_pkthdr.ether_vtag = rx_fcb.vlan;
		m->m_flags |= M_VLANTAG;
	}

	m_adj(m, sizeof(struct tsec_rx_fcb));
}

static void
tsec_setup_multicast(struct tsec_softc *sc)
{
	uint32_t hashtable[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	struct ifnet *ifp = sc->tsec_ifp;
	struct ifmultiaddr *ifma;
	uint32_t h;
	int i;

	TSEC_GLOBAL_LOCK_ASSERT(sc);

	if (ifp->if_flags & IFF_ALLMULTI) {
		for (i = 0; i < 8; i++)
			TSEC_WRITE(sc, TSEC_REG_GADDR(i), 0xFFFFFFFF);

		return;
	}

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {

		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		h = (ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 24) & 0xFF;

		hashtable[(h >> 5)] |= 1 << (0x1F - (h & 0x1F));
	}
	if_maddr_runlock(ifp);

	for (i = 0; i < 8; i++)
		TSEC_WRITE(sc, TSEC_REG_GADDR(i), hashtable[i]);
}

static int
tsec_set_mtu(struct tsec_softc *sc, unsigned int mtu)
{

	mtu += ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN + ETHER_CRC_LEN;

	TSEC_GLOBAL_LOCK_ASSERT(sc);

	if (mtu >= TSEC_MIN_FRAME_SIZE && mtu <= TSEC_MAX_FRAME_SIZE) {
		TSEC_WRITE(sc, TSEC_REG_MAXFRM, mtu);
		return (mtu);
	}

	return (0);
}

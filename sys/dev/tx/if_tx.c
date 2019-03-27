/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Semen Ustimenko (semenu@FreeBSD.org)
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * EtherPower II 10/100 Fast Ethernet (SMC 9432 serie)
 *
 * These cards are based on SMC83c17x (EPIC) chip and one of the various
 * PHYs (QS6612, AC101 and LXT970 were seen). The media support depends on
 * card model. All cards support 10baseT/UTP and 100baseTX half- and full-
 * duplex (SMB9432TX). SMC9432BTX also supports 10baseT/BNC. SMC9432FTX also
 * supports fibre optics.
 *
 * Thanks are going to Steve Bauer and Jason Wright.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net/bpf.h>

#include <net/if_vlan_var.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"

#include <dev/mii/lxtphyreg.h>

#include "miibus_if.h"

#include <dev/tx/if_txreg.h>
#include <dev/tx/if_txvar.h>

MODULE_DEPEND(tx, pci, 1, 1, 1);
MODULE_DEPEND(tx, ether, 1, 1, 1);
MODULE_DEPEND(tx, miibus, 1, 1, 1);

static int epic_ifioctl(struct ifnet *, u_long, caddr_t);
static void epic_intr(void *);
static void epic_tx_underrun(epic_softc_t *);
static void epic_ifstart(struct ifnet *);
static void epic_ifstart_locked(struct ifnet *);
static void epic_timer(void *);
static void epic_init(void *);
static void epic_init_locked(epic_softc_t *);
static void epic_stop(epic_softc_t *);
static void epic_rx_done(epic_softc_t *);
static void epic_tx_done(epic_softc_t *);
static int epic_init_rings(epic_softc_t *);
static void epic_free_rings(epic_softc_t *);
static void epic_stop_activity(epic_softc_t *);
static int epic_queue_last_packet(epic_softc_t *);
static void epic_start_activity(epic_softc_t *);
static void epic_set_rx_mode(epic_softc_t *);
static void epic_set_tx_mode(epic_softc_t *);
static void epic_set_mc_table(epic_softc_t *);
static int epic_read_eeprom(epic_softc_t *,u_int16_t);
static void epic_output_eepromw(epic_softc_t *, u_int16_t);
static u_int16_t epic_input_eepromw(epic_softc_t *);
static u_int8_t epic_eeprom_clock(epic_softc_t *,u_int8_t);
static void epic_write_eepromreg(epic_softc_t *,u_int8_t);
static u_int8_t epic_read_eepromreg(epic_softc_t *);

static int epic_read_phy_reg(epic_softc_t *, int, int);
static void epic_write_phy_reg(epic_softc_t *, int, int, int);

static int epic_miibus_readreg(device_t, int, int);
static int epic_miibus_writereg(device_t, int, int, int);
static void epic_miibus_statchg(device_t);
static void epic_miibus_mediainit(device_t);

static int epic_ifmedia_upd(struct ifnet *);
static int epic_ifmedia_upd_locked(struct ifnet *);
static void epic_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int epic_probe(device_t);
static int epic_attach(device_t);
static int epic_shutdown(device_t);
static int epic_detach(device_t);
static void epic_release(epic_softc_t *);
static struct epic_type *epic_devtype(device_t);

static device_method_t epic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		epic_probe),
	DEVMETHOD(device_attach,	epic_attach),
	DEVMETHOD(device_detach,	epic_detach),
	DEVMETHOD(device_shutdown,	epic_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	epic_miibus_readreg),
	DEVMETHOD(miibus_writereg,	epic_miibus_writereg),
	DEVMETHOD(miibus_statchg,	epic_miibus_statchg),
	DEVMETHOD(miibus_mediainit,	epic_miibus_mediainit),

	{ 0, 0 }
};

static driver_t epic_driver = {
	"tx",
	epic_methods,
	sizeof(epic_softc_t)
};

static devclass_t epic_devclass;

DRIVER_MODULE(tx, pci, epic_driver, epic_devclass, 0, 0);
DRIVER_MODULE(miibus, tx, miibus_driver, miibus_devclass, 0, 0);

static struct epic_type epic_devs[] = {
	{ SMC_VENDORID, SMC_DEVICEID_83C170, "SMC EtherPower II 10/100" },
	{ 0, 0, NULL }
};

static int
epic_probe(device_t dev)
{
	struct epic_type *t;

	t = epic_devtype(dev);

	if (t != NULL) {
		device_set_desc(dev, t->name);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static struct epic_type *
epic_devtype(device_t dev)
{
	struct epic_type *t;

	t = epic_devs;

	while (t->name != NULL) {
		if ((pci_get_vendor(dev) == t->ven_id) &&
		    (pci_get_device(dev) == t->dev_id)) {
			return (t);
		}
		t++;
	}
	return (NULL);
}

#ifdef EPIC_USEIOSPACE
#define	EPIC_RES	SYS_RES_IOPORT
#define	EPIC_RID	PCIR_BASEIO
#else
#define	EPIC_RES	SYS_RES_MEMORY
#define	EPIC_RID	PCIR_BASEMEM
#endif

static void
epic_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	u_int32_t *addr;

	if (error)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
	addr = arg;
	*addr = segs->ds_addr;
}

/*
 * Attach routine: map registers, allocate softc, rings and descriptors.
 * Reset to known state.
 */
static int
epic_attach(device_t dev)
{
	struct ifnet *ifp;
	epic_softc_t *sc;
	int error;
	int i, rid, tmp;
	u_char eaddr[6];

	sc = device_get_softc(dev);

	/* Preinitialize softc structure. */
	sc->dev = dev;
	mtx_init(&sc->lock, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);

	/* Fill ifnet structure. */
	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST|IFF_SIMPLEX|IFF_MULTICAST;
	ifp->if_ioctl = epic_ifioctl;
	ifp->if_start = epic_ifstart;
	ifp->if_init = epic_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, TX_RING_SIZE - 1);

	/* Enable busmastering. */
	pci_enable_busmaster(dev);

	rid = EPIC_RID;
	sc->res = bus_alloc_resource_any(dev, EPIC_RES, &rid, RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		error = ENXIO;
		goto fail;
	}

	/* Allocate interrupt. */
	rid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Allocate DMA tags. */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 4, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    MCLBYTES * EPIC_MAX_FRAGS, EPIC_MAX_FRAGS, MCLBYTES, 0, NULL, NULL,
	    &sc->mtag);
	if (error) {
		device_printf(dev, "couldn't allocate dma tag\n");
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(dev), 4, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    sizeof(struct epic_rx_desc) * RX_RING_SIZE,
	    1, sizeof(struct epic_rx_desc) * RX_RING_SIZE, 0, NULL,
	    NULL, &sc->rtag);
	if (error) {
		device_printf(dev, "couldn't allocate dma tag\n");
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(dev), 4, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    sizeof(struct epic_tx_desc) * TX_RING_SIZE,
	    1, sizeof(struct epic_tx_desc) * TX_RING_SIZE, 0,
	    NULL, NULL, &sc->ttag);
	if (error) {
		device_printf(dev, "couldn't allocate dma tag\n");
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(dev), 4, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    sizeof(struct epic_frag_list) * TX_RING_SIZE,
	    1, sizeof(struct epic_frag_list) * TX_RING_SIZE, 0,
	    NULL, NULL, &sc->ftag);
	if (error) {
		device_printf(dev, "couldn't allocate dma tag\n");
		goto fail;
	}

	/* Allocate DMA safe memory and get the DMA addresses. */
	error = bus_dmamem_alloc(sc->ftag, (void **)&sc->tx_flist,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &sc->fmap);
	if (error) {
		device_printf(dev, "couldn't allocate dma memory\n");
		goto fail;
	}
	error = bus_dmamap_load(sc->ftag, sc->fmap, sc->tx_flist,
	    sizeof(struct epic_frag_list) * TX_RING_SIZE, epic_dma_map_addr,
	    &sc->frag_addr, 0);
	if (error) {
		device_printf(dev, "couldn't map dma memory\n");
		goto fail;
	}
	error = bus_dmamem_alloc(sc->ttag, (void **)&sc->tx_desc,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &sc->tmap);
	if (error) {
		device_printf(dev, "couldn't allocate dma memory\n");
		goto fail;
	}
	error = bus_dmamap_load(sc->ttag, sc->tmap, sc->tx_desc,
	    sizeof(struct epic_tx_desc) * TX_RING_SIZE, epic_dma_map_addr,
	    &sc->tx_addr, 0);
	if (error) {
		device_printf(dev, "couldn't map dma memory\n");
		goto fail;
	}
	error = bus_dmamem_alloc(sc->rtag, (void **)&sc->rx_desc,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &sc->rmap);
	if (error) {
		device_printf(dev, "couldn't allocate dma memory\n");
		goto fail;
	}
	error = bus_dmamap_load(sc->rtag, sc->rmap, sc->rx_desc,
	    sizeof(struct epic_rx_desc) * RX_RING_SIZE, epic_dma_map_addr,
	    &sc->rx_addr, 0);
	if (error) {
		device_printf(dev, "couldn't map dma memory\n");
		goto fail;
	}

	/* Bring the chip out of low-power mode. */
	CSR_WRITE_4(sc, GENCTL, GENCTL_SOFT_RESET);
	DELAY(500);

	/* Workaround for Application Note 7-15. */
	for (i = 0; i < 16; i++)
		CSR_WRITE_4(sc, TEST1, TEST1_CLOCK_TEST);

	/* Read MAC address from EEPROM. */
	for (i = 0; i < ETHER_ADDR_LEN / sizeof(u_int16_t); i++)
		((u_int16_t *)eaddr)[i] = epic_read_eeprom(sc,i);

	/* Set Non-Volatile Control Register from EEPROM. */
	CSR_WRITE_4(sc, NVCTL, epic_read_eeprom(sc, EEPROM_NVCTL) & 0x1F);

	/* Set defaults. */
	sc->tx_threshold = TRANSMIT_THRESHOLD;
	sc->txcon = TXCON_DEFAULT;
	sc->miicfg = MIICFG_SMI_ENABLE;
	sc->phyid = EPIC_UNKN_PHY;
	sc->serinst = -1;

	/* Fetch card id. */
	sc->cardvend = pci_read_config(dev, PCIR_SUBVEND_0, 2);
	sc->cardid = pci_read_config(dev, PCIR_SUBDEV_0, 2);

	if (sc->cardvend != SMC_VENDORID)
		device_printf(dev, "unknown card vendor %04xh\n", sc->cardvend);

	/* Do ifmedia setup. */
	error = mii_attach(dev, &sc->miibus, ifp, epic_ifmedia_upd,
	    epic_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	/* board type and ... */
	printf(" type ");
	for(i = 0x2c; i < 0x32; i++) {
		tmp = epic_read_eeprom(sc, i);
		if (' ' == (u_int8_t)tmp)
			break;
		printf("%c", (u_int8_t)tmp);
		tmp >>= 8;
		if (' ' == (u_int8_t)tmp)
			break;
		printf("%c", (u_int8_t)tmp);
	}
	printf("\n");

	/* Initialize rings. */
	if (epic_init_rings(sc)) {
		device_printf(dev, "failed to init rings\n");
		error = ENXIO;
		goto fail;
	}

	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable |= IFCAP_VLAN_MTU;
	callout_init_mtx(&sc->timer, &sc->lock, 0);

	/* Attach to OS's managers. */
	ether_ifattach(ifp, eaddr);

	/* Activate our interrupt handler. */
	error = bus_setup_intr(dev, sc->irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, epic_intr, sc, &sc->sc_ih);
	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

	gone_by_fcp101_dev(dev);

	return (0);
fail:
	epic_release(sc);
	return (error);
}

/*
 * Free any resources allocated by the driver.
 */
static void
epic_release(epic_softc_t *sc)
{
	if (sc->ifp != NULL)
		if_free(sc->ifp);
	if (sc->irq)
		bus_release_resource(sc->dev, SYS_RES_IRQ, 0, sc->irq);
	if (sc->res)
		bus_release_resource(sc->dev, EPIC_RES, EPIC_RID, sc->res);
	epic_free_rings(sc);
	if (sc->tx_flist) {
		bus_dmamap_unload(sc->ftag, sc->fmap);
		bus_dmamem_free(sc->ftag, sc->tx_flist, sc->fmap);
	}
	if (sc->tx_desc) {
		bus_dmamap_unload(sc->ttag, sc->tmap);
		bus_dmamem_free(sc->ttag, sc->tx_desc, sc->tmap);
	}
	if (sc->rx_desc) {
		bus_dmamap_unload(sc->rtag, sc->rmap);
		bus_dmamem_free(sc->rtag, sc->rx_desc, sc->rmap);
	}
	if (sc->mtag)
		bus_dma_tag_destroy(sc->mtag);
	if (sc->ftag)
		bus_dma_tag_destroy(sc->ftag);
	if (sc->ttag)
		bus_dma_tag_destroy(sc->ttag);
	if (sc->rtag)
		bus_dma_tag_destroy(sc->rtag);
	mtx_destroy(&sc->lock);
}

/*
 * Detach driver and free resources.
 */
static int
epic_detach(device_t dev)
{
	struct ifnet *ifp;
	epic_softc_t *sc;

	sc = device_get_softc(dev);
	ifp = sc->ifp;

	EPIC_LOCK(sc);
	epic_stop(sc);
	EPIC_UNLOCK(sc);
	callout_drain(&sc->timer);
	ether_ifdetach(ifp);
	bus_teardown_intr(dev, sc->irq, sc->sc_ih);

	bus_generic_detach(dev);
	device_delete_child(dev, sc->miibus);

	epic_release(sc);
	return (0);
}

#undef	EPIC_RES
#undef	EPIC_RID

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
epic_shutdown(device_t dev)
{
	epic_softc_t *sc;

	sc = device_get_softc(dev);

	EPIC_LOCK(sc);
	epic_stop(sc);
	EPIC_UNLOCK(sc);
	return (0);
}

/*
 * This is if_ioctl handler.
 */
static int
epic_ifioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	epic_softc_t *sc = ifp->if_softc;
	struct mii_data	*mii;
	struct ifreq *ifr = (struct ifreq *) data;
	int error = 0;

	switch (command) {
	case SIOCSIFMTU:
		if (ifp->if_mtu == ifr->ifr_mtu)
			break;

		/* XXX Though the datasheet doesn't imply any
		 * limitations on RX and TX sizes beside max 64Kb
		 * DMA transfer, seems we can't send more then 1600
		 * data bytes per ethernet packet (transmitter hangs
		 * up if more data is sent).
		 */
		EPIC_LOCK(sc);
		if (ifr->ifr_mtu + ifp->if_hdrlen <= EPIC_MAX_MTU) {
			ifp->if_mtu = ifr->ifr_mtu;
			epic_stop(sc);
			epic_init_locked(sc);
		} else
			error = EINVAL;
		EPIC_UNLOCK(sc);
		break;

	case SIOCSIFFLAGS:
		/*
		 * If the interface is marked up and stopped, then start it.
		 * If it is marked down and running, then stop it.
		 */
		EPIC_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
				epic_init_locked(sc);
				EPIC_UNLOCK(sc);
				break;
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				epic_stop(sc);
				EPIC_UNLOCK(sc);
				break;
			}
		}

		/* Handle IFF_PROMISC and IFF_ALLMULTI flags. */
		epic_stop_activity(sc);
		epic_set_mc_table(sc);
		epic_set_rx_mode(sc);
		epic_start_activity(sc);
		EPIC_UNLOCK(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		EPIC_LOCK(sc);
		epic_set_mc_table(sc);
		EPIC_UNLOCK(sc);
		error = 0;
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return (error);
}

static void
epic_dma_map_txbuf(void *arg, bus_dma_segment_t *segs, int nseg,
    bus_size_t mapsize, int error)
{
	struct epic_frag_list *flist;
	int i;

	if (error)
		return;

	KASSERT(nseg <= EPIC_MAX_FRAGS, ("too many DMA segments"));
	flist = arg;
	/* Fill fragments list. */
	for (i = 0; i < nseg; i++) {
		KASSERT(segs[i].ds_len <= MCLBYTES, ("segment size too large"));
		flist->frag[i].fraglen = segs[i].ds_len;
		flist->frag[i].fragaddr = segs[i].ds_addr;
	}
	flist->numfrags = nseg;
}

static void
epic_dma_map_rxbuf(void *arg, bus_dma_segment_t *segs, int nseg,
    bus_size_t mapsize, int error)
{
	struct epic_rx_desc *desc;

	if (error)
		return;

	KASSERT(nseg == 1, ("too many DMA segments"));
	desc = arg;
	desc->bufaddr = segs->ds_addr;
}

/*
 * This is if_start handler. It takes mbufs from if_snd queue
 * and queue them for transmit, one by one, until TX ring become full
 * or queue become empty.
 */
static void
epic_ifstart(struct ifnet * ifp)
{
	epic_softc_t *sc = ifp->if_softc;

	EPIC_LOCK(sc);
	epic_ifstart_locked(ifp);
	EPIC_UNLOCK(sc);
}

static void
epic_ifstart_locked(struct ifnet * ifp)
{
	epic_softc_t *sc = ifp->if_softc;
	struct epic_tx_buffer *buf;
	struct epic_tx_desc *desc;
	struct epic_frag_list *flist;
	struct mbuf *m0, *m;
	int error;

	while (sc->pending_txs < TX_RING_SIZE) {
		buf = sc->tx_buffer + sc->cur_tx;
		desc = sc->tx_desc + sc->cur_tx;
		flist = sc->tx_flist + sc->cur_tx;

		/* Get next packet to send. */
		IF_DEQUEUE(&ifp->if_snd, m0);

		/* If nothing to send, return. */
		if (m0 == NULL)
			return;

		error = bus_dmamap_load_mbuf(sc->mtag, buf->map, m0,
		    epic_dma_map_txbuf, flist, 0);

		if (error && error != EFBIG) {
			m_freem(m0);
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			continue;
		}

		/*
		 * If packet was more than EPIC_MAX_FRAGS parts,
		 * recopy packet to a newly allocated mbuf cluster.
		 */
		if (error) {
			m = m_defrag(m0, M_NOWAIT);
			if (m == NULL) {
				m_freem(m0);
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
				continue;
			}
			m_freem(m0);
			m0 = m;

			error = bus_dmamap_load_mbuf(sc->mtag, buf->map, m,
			    epic_dma_map_txbuf, flist, 0);
			if (error) {
				m_freem(m);
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
				continue;
			}
		}
		bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_PREWRITE);

		buf->mbuf = m0;
		sc->pending_txs++;
		sc->cur_tx = (sc->cur_tx + 1) & TX_RING_MASK;
		desc->control = 0x01;
		desc->txlength =
		    max(m0->m_pkthdr.len, ETHER_MIN_LEN - ETHER_CRC_LEN);
		desc->status = 0x8000;
		bus_dmamap_sync(sc->ttag, sc->tmap,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		bus_dmamap_sync(sc->ftag, sc->fmap, BUS_DMASYNC_PREWRITE);
		CSR_WRITE_4(sc, COMMAND, COMMAND_TXQUEUED);

		/* Set watchdog timer. */
		sc->tx_timeout = 8;

		BPF_MTAP(ifp, m0);
	}

	ifp->if_drv_flags |= IFF_DRV_OACTIVE;
}

/*
 * Synopsis: Finish all received frames.
 */
static void
epic_rx_done(epic_softc_t *sc)
{
	struct ifnet *ifp = sc->ifp;
	u_int16_t len;
	struct epic_rx_buffer *buf;
	struct epic_rx_desc *desc;
	struct mbuf *m;
	bus_dmamap_t map;
	int error;

	bus_dmamap_sync(sc->rtag, sc->rmap, BUS_DMASYNC_POSTREAD);
	while ((sc->rx_desc[sc->cur_rx].status & 0x8000) == 0) {
		buf = sc->rx_buffer + sc->cur_rx;
		desc = sc->rx_desc + sc->cur_rx;

		/* Switch to next descriptor. */
		sc->cur_rx = (sc->cur_rx + 1) & RX_RING_MASK;

		/*
		 * Check for RX errors. This should only happen if
		 * SAVE_ERRORED_PACKETS is set. RX errors generate
		 * RXE interrupt usually.
		 */
		if ((desc->status & 1) == 0) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			desc->status = 0x8000;
			continue;
		}

		/* Save packet length and mbuf contained packet. */
		bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_POSTREAD);
		len = desc->rxlength - ETHER_CRC_LEN;
		m = buf->mbuf;

		/* Try to get an mbuf cluster. */
		buf->mbuf = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (buf->mbuf == NULL) {
			buf->mbuf = m;
			desc->status = 0x8000;
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			continue;
		}
		buf->mbuf->m_len = buf->mbuf->m_pkthdr.len = MCLBYTES;
		m_adj(buf->mbuf, ETHER_ALIGN);

		/* Point to new mbuf, and give descriptor to chip. */
		error = bus_dmamap_load_mbuf(sc->mtag, sc->sparemap, buf->mbuf,
		    epic_dma_map_rxbuf, desc, 0);
		if (error) {
			buf->mbuf = m;
			desc->status = 0x8000;
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			continue;
		}

		desc->status = 0x8000;
		bus_dmamap_unload(sc->mtag, buf->map);
		map = buf->map;
		buf->map = sc->sparemap;
		sc->sparemap = map;
		bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_PREREAD);

		/* First mbuf in packet holds the ethernet and packet headers */
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = len;

		/* Give mbuf to OS. */
		EPIC_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		EPIC_LOCK(sc);

		/* Successfully received frame */
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
        }
	bus_dmamap_sync(sc->rtag, sc->rmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

/*
 * Synopsis: Do last phase of transmission. I.e. if desc is
 * transmitted, decrease pending_txs counter, free mbuf contained
 * packet, switch to next descriptor and repeat until no packets
 * are pending or descriptor is not transmitted yet.
 */
static void
epic_tx_done(epic_softc_t *sc)
{
	struct epic_tx_buffer *buf;
	struct epic_tx_desc *desc;
	u_int16_t status;

	bus_dmamap_sync(sc->ttag, sc->tmap, BUS_DMASYNC_POSTREAD);
	while (sc->pending_txs > 0) {
		buf = sc->tx_buffer + sc->dirty_tx;
		desc = sc->tx_desc + sc->dirty_tx;
		status = desc->status;

		/*
		 * If packet is not transmitted, thou followed
		 * packets are not transmitted too.
		 */
		if (status & 0x8000)
			break;

		/* Packet is transmitted. Switch to next and free mbuf. */
		sc->pending_txs--;
		sc->dirty_tx = (sc->dirty_tx + 1) & TX_RING_MASK;
		bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->mtag, buf->map);
		m_freem(buf->mbuf);
		buf->mbuf = NULL;

		/* Check for errors and collisions. */
		if (status & 0x0001)
			if_inc_counter(sc->ifp, IFCOUNTER_OPACKETS, 1);
		else
			if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, 1);
		if_inc_counter(sc->ifp, IFCOUNTER_COLLISIONS, (status >> 8) & 0x1F);
#ifdef EPIC_DIAG
		if ((status & 0x1001) == 0x1001)
			device_printf(sc->dev,
			    "Tx ERROR: excessive coll. number\n");
#endif
	}

	if (sc->pending_txs < TX_RING_SIZE)
		sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	bus_dmamap_sync(sc->ttag, sc->tmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

/*
 * Interrupt function
 */
static void
epic_intr(void *arg)
{
    epic_softc_t *sc;
    int status, i;

    sc = arg;
    i = 4;
    EPIC_LOCK(sc);
    while (i-- && ((status = CSR_READ_4(sc, INTSTAT)) & INTSTAT_INT_ACTV)) {
	CSR_WRITE_4(sc, INTSTAT, status);

	if (status & (INTSTAT_RQE|INTSTAT_RCC|INTSTAT_OVW)) {
	    epic_rx_done(sc);
	    if (status & (INTSTAT_RQE|INTSTAT_OVW)) {
#ifdef EPIC_DIAG
		if (status & INTSTAT_OVW)
		    device_printf(sc->dev, "RX buffer overflow\n");
		if (status & INTSTAT_RQE)
		    device_printf(sc->dev, "RX FIFO overflow\n");
#endif
		if ((CSR_READ_4(sc, COMMAND) & COMMAND_RXQUEUED) == 0)
		    CSR_WRITE_4(sc, COMMAND, COMMAND_RXQUEUED);
		if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
	    }
	}

	if (status & (INTSTAT_TXC|INTSTAT_TCC|INTSTAT_TQE)) {
	    epic_tx_done(sc);
	    if (sc->ifp->if_snd.ifq_head != NULL)
		    epic_ifstart_locked(sc->ifp);
	}

	/* Check for rare errors */
	if (status & (INTSTAT_FATAL|INTSTAT_PMA|INTSTAT_PTA|
		      INTSTAT_APE|INTSTAT_DPE|INTSTAT_TXU|INTSTAT_RXE)) {
    	    if (status & (INTSTAT_FATAL|INTSTAT_PMA|INTSTAT_PTA|
			  INTSTAT_APE|INTSTAT_DPE)) {
		device_printf(sc->dev, "PCI fatal errors occurred: %s%s%s%s\n",
		    (status & INTSTAT_PMA) ? "PMA " : "",
		    (status & INTSTAT_PTA) ? "PTA " : "",
		    (status & INTSTAT_APE) ? "APE " : "",
		    (status & INTSTAT_DPE) ? "DPE" : "");

		epic_stop(sc);
		epic_init_locked(sc);
	    	break;
	    }

	    if (status & INTSTAT_RXE) {
#ifdef EPIC_DIAG
		device_printf(sc->dev, "CRC/Alignment error\n");
#endif
		if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
	    }

	    if (status & INTSTAT_TXU) {
		epic_tx_underrun(sc);
		if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, 1);
	    }
	}
    }

    /* If no packets are pending, then no timeouts. */
    if (sc->pending_txs == 0)
	    sc->tx_timeout = 0;
    EPIC_UNLOCK(sc);
}

/*
 * Handle the TX underrun error: increase the TX threshold
 * and restart the transmitter.
 */
static void
epic_tx_underrun(epic_softc_t *sc)
{
	if (sc->tx_threshold > TRANSMIT_THRESHOLD_MAX) {
		sc->txcon &= ~TXCON_EARLY_TRANSMIT_ENABLE;
#ifdef EPIC_DIAG
		device_printf(sc->dev, "Tx UNDERRUN: early TX disabled\n");
#endif
	} else {
		sc->tx_threshold += 0x40;
#ifdef EPIC_DIAG
		device_printf(sc->dev,
		    "Tx UNDERRUN: TX threshold increased to %d\n",
		    sc->tx_threshold);
#endif
	}

	/* We must set TXUGO to reset the stuck transmitter. */
	CSR_WRITE_4(sc, COMMAND, COMMAND_TXUGO);

	/* Update the TX threshold */
	epic_stop_activity(sc);
	epic_set_tx_mode(sc);
	epic_start_activity(sc);
}

/*
 * This function is called once a second when the interface is running
 * and performs two functions.  First, it provides a timer for the mii
 * to help with autonegotiation.  Second, it checks for transmit
 * timeouts.
 */
static void
epic_timer(void *arg)
{
	epic_softc_t *sc = arg;
	struct mii_data *mii;
	struct ifnet *ifp;

	ifp = sc->ifp;
	EPIC_ASSERT_LOCKED(sc);
	if (sc->tx_timeout && --sc->tx_timeout == 0) {
		device_printf(sc->dev, "device timeout %d packets\n",
		    sc->pending_txs);

		/* Try to finish queued packets. */
		epic_tx_done(sc);

		/* If not successful. */
		if (sc->pending_txs > 0) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, sc->pending_txs);

			/* Reinitialize board. */
			device_printf(sc->dev, "reinitialization\n");
			epic_stop(sc);
			epic_init_locked(sc);
		} else
			device_printf(sc->dev,
			    "seems we can continue normaly\n");

		/* Start output. */
		if (ifp->if_snd.ifq_head)
			epic_ifstart_locked(ifp);
	}

	mii = device_get_softc(sc->miibus);
	mii_tick(mii);

	callout_reset(&sc->timer, hz, epic_timer, sc);
}

/*
 * Set media options.
 */
static int
epic_ifmedia_upd(struct ifnet *ifp)
{
	epic_softc_t *sc;
	int error;

	sc = ifp->if_softc;
	EPIC_LOCK(sc);
	error = epic_ifmedia_upd_locked(ifp);
	EPIC_UNLOCK(sc);
	return (error);
}
	
static int
epic_ifmedia_upd_locked(struct ifnet *ifp)
{
	epic_softc_t *sc;
	struct mii_data *mii;
	struct ifmedia *ifm;
	struct mii_softc *miisc;
	int cfg, media;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->miibus);
	ifm = &mii->mii_media;
	media = ifm->ifm_cur->ifm_media;

	/* Do not do anything if interface is not up. */
	if ((ifp->if_flags & IFF_UP) == 0)
		return (0);

	/*
	 * Lookup current selected PHY.
	 */
	if (IFM_INST(media) == sc->serinst) {
		sc->phyid = EPIC_SERIAL;
		sc->physc = NULL;
	} else {
		/* If we're not selecting serial interface, select MII mode. */
		sc->miicfg &= ~MIICFG_SERIAL_ENABLE;
		CSR_WRITE_4(sc, MIICFG, sc->miicfg);

		/* Default to unknown PHY. */
		sc->phyid = EPIC_UNKN_PHY;

		/* Lookup selected PHY. */
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list) {
			if (IFM_INST(media) == miisc->mii_inst) {
				sc->physc = miisc;
				break;
			}
		}

		/* Identify selected PHY. */
		if (sc->physc) {
			int id1, id2, model, oui;

			id1 = PHY_READ(sc->physc, MII_PHYIDR1);
			id2 = PHY_READ(sc->physc, MII_PHYIDR2);

			oui = MII_OUI(id1, id2);
			model = MII_MODEL(id2);
			switch (oui) {
			case MII_OUI_xxQUALSEMI:
				if (model == MII_MODEL_xxQUALSEMI_QS6612)
					sc->phyid = EPIC_QS6612_PHY;
				break;
			case MII_OUI_ALTIMA:
				if (model == MII_MODEL_ALTIMA_AC101)
					sc->phyid = EPIC_AC101_PHY;
				break;
			case MII_OUI_xxLEVEL1:
				if (model == MII_MODEL_xxLEVEL1_LXT970)
					sc->phyid = EPIC_LXT970_PHY;
				break;
			}
		}
	}

	/*
	 * Do PHY specific card setup.
	 */

	/*
	 * Call this, to isolate all not selected PHYs and
	 * set up selected.
	 */
	mii_mediachg(mii);

	/* Do our own setup. */
	switch (sc->phyid) {
	case EPIC_QS6612_PHY:
		break;
	case EPIC_AC101_PHY:
		/* We have to powerup fiber tranceivers. */
		if (IFM_SUBTYPE(media) == IFM_100_FX)
			sc->miicfg |= MIICFG_694_ENABLE;
		else
			sc->miicfg &= ~MIICFG_694_ENABLE;
		CSR_WRITE_4(sc, MIICFG, sc->miicfg);

		break;
	case EPIC_LXT970_PHY:
		/* We have to powerup fiber tranceivers. */
		cfg = PHY_READ(sc->physc, MII_LXTPHY_CONFIG);
		if (IFM_SUBTYPE(media) == IFM_100_FX)
			cfg |= CONFIG_LEDC1 | CONFIG_LEDC0;
		else
			cfg &= ~(CONFIG_LEDC1 | CONFIG_LEDC0);
		PHY_WRITE(sc->physc, MII_LXTPHY_CONFIG, cfg);

		break;
	case EPIC_SERIAL:
		/* Select serial PHY (10base2/BNC usually). */
		sc->miicfg |= MIICFG_694_ENABLE | MIICFG_SERIAL_ENABLE;
		CSR_WRITE_4(sc, MIICFG, sc->miicfg);

		/* There is no driver to fill this. */
		mii->mii_media_active = media;
		mii->mii_media_status = 0;

		/*
		 * We need to call this manually as it wasn't called
		 * in mii_mediachg().
		 */
		epic_miibus_statchg(sc->dev);
		break;
	default:
		device_printf(sc->dev, "ERROR! Unknown PHY selected\n");
		return (EINVAL);
	}

	return (0);
}

/*
 * Report current media status.
 */
static void
epic_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	epic_softc_t *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->miibus);
	EPIC_LOCK(sc);

	/* Nothing should be selected if interface is down. */
	if ((ifp->if_flags & IFF_UP) == 0) {
		ifmr->ifm_active = IFM_NONE;
		ifmr->ifm_status = 0;
		EPIC_UNLOCK(sc);
		return;
	}

	/* Call underlying pollstat, if not serial PHY. */
	if (sc->phyid != EPIC_SERIAL)
		mii_pollstat(mii);

	/* Simply copy media info. */
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	EPIC_UNLOCK(sc);
}

/*
 * Callback routine, called on media change.
 */
static void
epic_miibus_statchg(device_t dev)
{
	epic_softc_t *sc;
	struct mii_data *mii;
	int media;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->miibus);
	media = mii->mii_media_active;

	sc->txcon &= ~(TXCON_LOOPBACK_MODE | TXCON_FULL_DUPLEX);

	/*
	 * If we are in full-duplex mode or loopback operation,
	 * we need to decouple receiver and transmitter.
	 */
	if (IFM_OPTIONS(media) & (IFM_FDX | IFM_LOOP))
 		sc->txcon |= TXCON_FULL_DUPLEX;

	/* On some cards we need manualy set fullduplex led. */
	if (sc->cardid == SMC9432FTX ||
	    sc->cardid == SMC9432FTX_SC) {
		if (IFM_OPTIONS(media) & IFM_FDX)
			sc->miicfg |= MIICFG_694_ENABLE;
		else
			sc->miicfg &= ~MIICFG_694_ENABLE;

		CSR_WRITE_4(sc, MIICFG, sc->miicfg);
	}

	epic_stop_activity(sc);
	epic_set_tx_mode(sc);
	epic_start_activity(sc);
}

static void
epic_miibus_mediainit(device_t dev)
{
	epic_softc_t *sc;
	struct mii_data *mii;
	struct ifmedia *ifm;
	int media;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->miibus);
	ifm = &mii->mii_media;

	/*
	 * Add Serial Media Interface if present, this applies to
	 * SMC9432BTX serie.
	 */
	if (CSR_READ_4(sc, MIICFG) & MIICFG_PHY_PRESENT) {
		/* Store its instance. */
		sc->serinst = mii->mii_instance++;

		/* Add as 10base2/BNC media. */
		media = IFM_MAKEWORD(IFM_ETHER, IFM_10_2, 0, sc->serinst);
		ifmedia_add(ifm, media, 0, NULL);

		/* Report to user. */
		device_printf(sc->dev, "serial PHY detected (10Base2/BNC)\n");
	}
}

/*
 * Reset chip and update media.
 */
static void
epic_init(void *xsc)
{
	epic_softc_t *sc = xsc;

	EPIC_LOCK(sc);
	epic_init_locked(sc);
	EPIC_UNLOCK(sc);
}

static void
epic_init_locked(epic_softc_t *sc)
{
	struct ifnet *ifp = sc->ifp;
	int i;

	/* If interface is already running, then we need not do anything. */
	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		return;
	}

	/* Soft reset the chip (we have to power up card before). */
	CSR_WRITE_4(sc, GENCTL, 0);
	CSR_WRITE_4(sc, GENCTL, GENCTL_SOFT_RESET);

	/*
	 * Reset takes 15 pci ticks which depends on PCI bus speed.
	 * Assuming it >= 33000000 hz, we have wait at least 495e-6 sec.
	 */
	DELAY(500);

	/* Wake up */
	CSR_WRITE_4(sc, GENCTL, 0);

	/* Workaround for Application Note 7-15 */
	for (i = 0; i < 16; i++)
		CSR_WRITE_4(sc, TEST1, TEST1_CLOCK_TEST);

	/* Give rings to EPIC */
	CSR_WRITE_4(sc, PRCDAR, sc->rx_addr);
	CSR_WRITE_4(sc, PTCDAR, sc->tx_addr);

	/* Put node address to EPIC. */
	CSR_WRITE_4(sc, LAN0, ((u_int16_t *)IF_LLADDR(sc->ifp))[0]);
	CSR_WRITE_4(sc, LAN1, ((u_int16_t *)IF_LLADDR(sc->ifp))[1]);
	CSR_WRITE_4(sc, LAN2, ((u_int16_t *)IF_LLADDR(sc->ifp))[2]);

	/* Set tx mode, includeing transmit threshold. */
	epic_set_tx_mode(sc);

	/* Compute and set RXCON. */
	epic_set_rx_mode(sc);

	/* Set multicast table. */
	epic_set_mc_table(sc);

	/* Enable interrupts by setting the interrupt mask. */
	CSR_WRITE_4(sc, INTMASK,
		INTSTAT_RCC  | /* INTSTAT_RQE | INTSTAT_OVW | INTSTAT_RXE | */
		/* INTSTAT_TXC | */ INTSTAT_TCC | INTSTAT_TQE | INTSTAT_TXU |
		INTSTAT_FATAL);

	/* Acknowledge all pending interrupts. */
	CSR_WRITE_4(sc, INTSTAT, CSR_READ_4(sc, INTSTAT));

	/* Enable interrupts,  set for PCI read multiple and etc */
	CSR_WRITE_4(sc, GENCTL,
		GENCTL_ENABLE_INTERRUPT | GENCTL_MEMORY_READ_MULTIPLE |
		GENCTL_ONECOPY | GENCTL_RECEIVE_FIFO_THRESHOLD64);

	/* Mark interface running ... */
	if (ifp->if_flags & IFF_UP)
		ifp->if_drv_flags |= IFF_DRV_RUNNING;
	else
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	/* ... and free */
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	/* Start Rx process */
	epic_start_activity(sc);

	/* Set appropriate media */
	epic_ifmedia_upd_locked(ifp);

	callout_reset(&sc->timer, hz, epic_timer, sc);
}

/*
 * Synopsis: calculate and set Rx mode. Chip must be in idle state to
 * access RXCON.
 */
static void
epic_set_rx_mode(epic_softc_t *sc)
{
	u_int32_t flags;
	u_int32_t rxcon;

	flags = sc->ifp->if_flags;
	rxcon = RXCON_DEFAULT;

#ifdef EPIC_EARLY_RX
	rxcon |= RXCON_EARLY_RX;
#endif

	rxcon |= (flags & IFF_PROMISC) ? RXCON_PROMISCUOUS_MODE : 0;

	CSR_WRITE_4(sc, RXCON, rxcon);
}

/*
 * Synopsis: Set transmit control register. Chip must be in idle state to
 * access TXCON.
 */
static void
epic_set_tx_mode(epic_softc_t *sc)
{

	if (sc->txcon & TXCON_EARLY_TRANSMIT_ENABLE)
		CSR_WRITE_4(sc, ETXTHR, sc->tx_threshold);

	CSR_WRITE_4(sc, TXCON, sc->txcon);
}

/*
 * Synopsis: Program multicast filter honoring IFF_ALLMULTI and IFF_PROMISC
 * flags (note that setting PROMISC bit in EPIC's RXCON will only touch
 * individual frames, multicast filter must be manually programmed).
 *
 * Note: EPIC must be in idle state.
 */
static void
epic_set_mc_table(epic_softc_t *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	u_int16_t filter[4];
	u_int8_t h;

	ifp = sc->ifp;
	if (ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC)) {
		CSR_WRITE_4(sc, MC0, 0xFFFF);
		CSR_WRITE_4(sc, MC1, 0xFFFF);
		CSR_WRITE_4(sc, MC2, 0xFFFF);
		CSR_WRITE_4(sc, MC3, 0xFFFF);
		return;
	}

	filter[0] = 0;
	filter[1] = 0;
	filter[2] = 0;
	filter[3] = 0;

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		filter[h >> 4] |= 1 << (h & 0xF);
	}
	if_maddr_runlock(ifp);

	CSR_WRITE_4(sc, MC0, filter[0]);
	CSR_WRITE_4(sc, MC1, filter[1]);
	CSR_WRITE_4(sc, MC2, filter[2]);
	CSR_WRITE_4(sc, MC3, filter[3]);
}


/*
 * Synopsis: Start receive process and transmit one, if they need.
 */
static void
epic_start_activity(epic_softc_t *sc)
{

	/* Start rx process. */
	CSR_WRITE_4(sc, COMMAND, COMMAND_RXQUEUED | COMMAND_START_RX |
	    (sc->pending_txs ? COMMAND_TXQUEUED : 0));
}

/*
 * Synopsis: Completely stop Rx and Tx processes. If TQE is set additional
 * packet needs to be queued to stop Tx DMA.
 */
static void
epic_stop_activity(epic_softc_t *sc)
{
	int status, i;

	/* Stop Tx and Rx DMA. */
	CSR_WRITE_4(sc, COMMAND,
	    COMMAND_STOP_RX | COMMAND_STOP_RDMA | COMMAND_STOP_TDMA);

	/* Wait Rx and Tx DMA to stop (why 1 ms ??? XXX). */
	for (i = 0; i < 0x1000; i++) {
		status = CSR_READ_4(sc, INTSTAT) &
		    (INTSTAT_TXIDLE | INTSTAT_RXIDLE);
		if (status == (INTSTAT_TXIDLE | INTSTAT_RXIDLE))
			break;
		DELAY(1);
	}

	/* Catch all finished packets. */
	epic_rx_done(sc);
	epic_tx_done(sc);

	status = CSR_READ_4(sc, INTSTAT);

	if ((status & INTSTAT_RXIDLE) == 0)
		device_printf(sc->dev, "ERROR! Can't stop Rx DMA\n");

	if ((status & INTSTAT_TXIDLE) == 0)
		device_printf(sc->dev, "ERROR! Can't stop Tx DMA\n");

	/*
	 * May need to queue one more packet if TQE, this is rare
	 * but existing case.
	 */
	if ((status & INTSTAT_TQE) && !(status & INTSTAT_TXIDLE))
		(void)epic_queue_last_packet(sc);
}

/*
 * The EPIC transmitter may stuck in TQE state. It will not go IDLE until
 * a packet from current descriptor will be copied to internal RAM. We
 * compose a dummy packet here and queue it for transmission.
 *
 * XXX the packet will then be actually sent over network...
 */
static int
epic_queue_last_packet(epic_softc_t *sc)
{
	struct epic_tx_desc *desc;
	struct epic_frag_list *flist;
	struct epic_tx_buffer *buf;
	struct mbuf *m0;
	int error, i;

	device_printf(sc->dev, "queue last packet\n");

	desc = sc->tx_desc + sc->cur_tx;
	flist = sc->tx_flist + sc->cur_tx;
	buf = sc->tx_buffer + sc->cur_tx;

	if ((desc->status & 0x8000) || (buf->mbuf != NULL))
		return (EBUSY);

	MGETHDR(m0, M_NOWAIT, MT_DATA);
	if (m0 == NULL)
		return (ENOBUFS);

	/* Prepare mbuf. */
	m0->m_len = min(MHLEN, ETHER_MIN_LEN - ETHER_CRC_LEN);
	m0->m_pkthdr.len = m0->m_len;
	m0->m_pkthdr.rcvif = sc->ifp;
	bzero(mtod(m0, caddr_t), m0->m_len);

	/* Fill fragments list. */
	error = bus_dmamap_load_mbuf(sc->mtag, buf->map, m0,
	    epic_dma_map_txbuf, flist, 0);
	if (error) {
		m_freem(m0);
		return (error);
	}
	bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_PREWRITE);

	/* Fill in descriptor. */
	buf->mbuf = m0;
	sc->pending_txs++;
	sc->cur_tx = (sc->cur_tx + 1) & TX_RING_MASK;
	desc->control = 0x01;
	desc->txlength = max(m0->m_pkthdr.len, ETHER_MIN_LEN - ETHER_CRC_LEN);
	desc->status = 0x8000;
	bus_dmamap_sync(sc->ttag, sc->tmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->ftag, sc->fmap, BUS_DMASYNC_PREWRITE);

	/* Launch transmission. */
	CSR_WRITE_4(sc, COMMAND, COMMAND_STOP_TDMA | COMMAND_TXQUEUED);

	/* Wait Tx DMA to stop (for how long??? XXX) */
	for (i = 0; i < 1000; i++) {
		if (CSR_READ_4(sc, INTSTAT) & INTSTAT_TXIDLE)
			break;
		DELAY(1);
	}

	if ((CSR_READ_4(sc, INTSTAT) & INTSTAT_TXIDLE) == 0)
		device_printf(sc->dev, "ERROR! can't stop Tx DMA (2)\n");
	else
		epic_tx_done(sc);

	return (0);
}

/*
 *  Synopsis: Shut down board and deallocates rings.
 */
static void
epic_stop(epic_softc_t *sc)
{

	EPIC_ASSERT_LOCKED(sc);

	sc->tx_timeout = 0;
	callout_stop(&sc->timer);

	/* Disable interrupts */
	CSR_WRITE_4(sc, INTMASK, 0);
	CSR_WRITE_4(sc, GENCTL, 0);

	/* Try to stop Rx and TX processes */
	epic_stop_activity(sc);

	/* Reset chip */
	CSR_WRITE_4(sc, GENCTL, GENCTL_SOFT_RESET);
	DELAY(1000);

	/* Make chip go to bed */
	CSR_WRITE_4(sc, GENCTL, GENCTL_POWER_DOWN);

	/* Mark as stopped */
	sc->ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
}

/*
 * Synopsis: This function should free all memory allocated for rings.
 */
static void
epic_free_rings(epic_softc_t *sc)
{
	int i;

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct epic_rx_buffer *buf = sc->rx_buffer + i;
		struct epic_rx_desc *desc = sc->rx_desc + i;

		desc->status = 0;
		desc->buflength = 0;
		desc->bufaddr = 0;

		if (buf->mbuf) {
			bus_dmamap_unload(sc->mtag, buf->map);
			bus_dmamap_destroy(sc->mtag, buf->map);
			m_freem(buf->mbuf);
		}
		buf->mbuf = NULL;
	}

	if (sc->sparemap != NULL)
		bus_dmamap_destroy(sc->mtag, sc->sparemap);

	for (i = 0; i < TX_RING_SIZE; i++) {
		struct epic_tx_buffer *buf = sc->tx_buffer + i;
		struct epic_tx_desc *desc = sc->tx_desc + i;

		desc->status = 0;
		desc->buflength = 0;
		desc->bufaddr = 0;

		if (buf->mbuf) {
			bus_dmamap_unload(sc->mtag, buf->map);
			bus_dmamap_destroy(sc->mtag, buf->map);
			m_freem(buf->mbuf);
		}
		buf->mbuf = NULL;
	}
}

/*
 * Synopsis:  Allocates mbufs for Rx ring and point Rx descs to them.
 * Point Tx descs to fragment lists. Check that all descs and fraglists
 * are bounded and aligned properly.
 */
static int
epic_init_rings(epic_softc_t *sc)
{
	int error, i;

	sc->cur_rx = sc->cur_tx = sc->dirty_tx = sc->pending_txs = 0;

	/* Initialize the RX descriptor ring. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct epic_rx_buffer *buf = sc->rx_buffer + i;
		struct epic_rx_desc *desc = sc->rx_desc + i;

		desc->status = 0;		/* Owned by driver */
		desc->next = sc->rx_addr +
		    ((i + 1) & RX_RING_MASK) * sizeof(struct epic_rx_desc);

		if ((desc->next & 3) ||
		    ((desc->next & PAGE_MASK) + sizeof *desc) > PAGE_SIZE) {
			epic_free_rings(sc);
			return (EFAULT);
		}

		buf->mbuf = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (buf->mbuf == NULL) {
			epic_free_rings(sc);
			return (ENOBUFS);
		}
		buf->mbuf->m_len = buf->mbuf->m_pkthdr.len = MCLBYTES;
		m_adj(buf->mbuf, ETHER_ALIGN);

		error = bus_dmamap_create(sc->mtag, 0, &buf->map);
		if (error) {
			epic_free_rings(sc);
			return (error);
		}
		error = bus_dmamap_load_mbuf(sc->mtag, buf->map, buf->mbuf,
		    epic_dma_map_rxbuf, desc, 0);
		if (error) {
			epic_free_rings(sc);
			return (error);
		}
		bus_dmamap_sync(sc->mtag, buf->map, BUS_DMASYNC_PREREAD);

		desc->buflength = buf->mbuf->m_len; /* Max RX buffer length */
		desc->status = 0x8000;		/* Set owner bit to NIC */
	}
	bus_dmamap_sync(sc->rtag, sc->rmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Create the spare DMA map. */
	error = bus_dmamap_create(sc->mtag, 0, &sc->sparemap);
	if (error) {
		epic_free_rings(sc);
		return (error);
	}

	/* Initialize the TX descriptor ring. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		struct epic_tx_buffer *buf = sc->tx_buffer + i;
		struct epic_tx_desc *desc = sc->tx_desc + i;

		desc->status = 0;
		desc->next = sc->tx_addr +
		    ((i + 1) & TX_RING_MASK) * sizeof(struct epic_tx_desc);

		if ((desc->next & 3) ||
		    ((desc->next & PAGE_MASK) + sizeof *desc) > PAGE_SIZE) {
			epic_free_rings(sc);
			return (EFAULT);
		}

		buf->mbuf = NULL;
		desc->bufaddr = sc->frag_addr +
		    i * sizeof(struct epic_frag_list);

		if ((desc->bufaddr & 3) ||
		    ((desc->bufaddr & PAGE_MASK) +
		    sizeof(struct epic_frag_list)) > PAGE_SIZE) {
			epic_free_rings(sc);
			return (EFAULT);
		}

		error = bus_dmamap_create(sc->mtag, 0, &buf->map);
		if (error) {
			epic_free_rings(sc);
			return (error);
		}
	}
	bus_dmamap_sync(sc->ttag, sc->tmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->ftag, sc->fmap, BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * EEPROM operation functions
 */
static void
epic_write_eepromreg(epic_softc_t *sc, u_int8_t val)
{
	u_int16_t i;

	CSR_WRITE_1(sc, EECTL, val);

	for (i = 0; i < 0xFF; i++) {
		if ((CSR_READ_1(sc, EECTL) & 0x20) == 0)
			break;
	}
}

static u_int8_t
epic_read_eepromreg(epic_softc_t *sc)
{

	return (CSR_READ_1(sc, EECTL));
}

static u_int8_t
epic_eeprom_clock(epic_softc_t *sc, u_int8_t val)
{

	epic_write_eepromreg(sc, val);
	epic_write_eepromreg(sc, (val | 0x4));
	epic_write_eepromreg(sc, val);

	return (epic_read_eepromreg(sc));
}

static void
epic_output_eepromw(epic_softc_t *sc, u_int16_t val)
{
	int i;

	for (i = 0xF; i >= 0; i--) {
		if (val & (1 << i))
			epic_eeprom_clock(sc, 0x0B);
		else
			epic_eeprom_clock(sc, 0x03);
	}
}

static u_int16_t
epic_input_eepromw(epic_softc_t *sc)
{
	u_int16_t retval = 0;
	int i;

	for (i = 0xF; i >= 0; i--) {
		if (epic_eeprom_clock(sc, 0x3) & 0x10)
			retval |= (1 << i);
	}

	return (retval);
}

static int
epic_read_eeprom(epic_softc_t *sc, u_int16_t loc)
{
	u_int16_t dataval;
	u_int16_t read_cmd;

	epic_write_eepromreg(sc, 3);

	if (epic_read_eepromreg(sc) & 0x40)
		read_cmd = (loc & 0x3F) | 0x180;
	else
		read_cmd = (loc & 0xFF) | 0x600;

	epic_output_eepromw(sc, read_cmd);

	dataval = epic_input_eepromw(sc);

	epic_write_eepromreg(sc, 1);

	return (dataval);
}

/*
 * Here goes MII read/write routines.
 */
static int
epic_read_phy_reg(epic_softc_t *sc, int phy, int reg)
{
	int i;

	CSR_WRITE_4(sc, MIICTL, ((reg << 4) | (phy << 9) | 0x01));

	for (i = 0; i < 0x100; i++) {
		if ((CSR_READ_4(sc, MIICTL) & 0x01) == 0)
			break;
		DELAY(1);
	}

	return (CSR_READ_4(sc, MIIDATA));
}

static void
epic_write_phy_reg(epic_softc_t *sc, int phy, int reg, int val)
{
	int i;

	CSR_WRITE_4(sc, MIIDATA, val);
	CSR_WRITE_4(sc, MIICTL, ((reg << 4) | (phy << 9) | 0x02));

	for(i = 0; i < 0x100; i++) {
		if ((CSR_READ_4(sc, MIICTL) & 0x02) == 0)
			break;
		DELAY(1);
	}
}

static int
epic_miibus_readreg(device_t dev, int phy, int reg)
{
	epic_softc_t *sc;

	sc = device_get_softc(dev);

	return (PHY_READ_2(sc, phy, reg));
}

static int
epic_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	epic_softc_t *sc;

	sc = device_get_softc(dev);

	PHY_WRITE_2(sc, phy, reg, data);

	return (0);
}

/*	$OpenBSD: gem.c,v 1.128 2023/11/10 15:51:20 bluhm Exp $	*/
/*	$NetBSD: gem.c,v 1.1 2001/09/16 00:11:43 eeh Exp $ */

/*
 *
 * Copyright (C) 2001 Eduardo Horvath.
 * All rights reserved.
 *
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Driver for Sun GEM ethernet controllers.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/atomic.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ic/gemreg.h>
#include <dev/ic/gemvar.h>

#define TRIES	10000

struct cfdriver gem_cd = {
	NULL, "gem", DV_IFNET
};

void		gem_start(struct ifqueue *);
void		gem_stop(struct ifnet *, int);
int		gem_ioctl(struct ifnet *, u_long, caddr_t);
void		gem_tick(void *);
void		gem_watchdog(struct ifnet *);
int		gem_init(struct ifnet *);
void		gem_init_regs(struct gem_softc *);
int		gem_ringsize(int);
int		gem_meminit(struct gem_softc *);
void		gem_mifinit(struct gem_softc *);
int		gem_bitwait(struct gem_softc *, bus_space_handle_t, int,
		    u_int32_t, u_int32_t);
void		gem_reset(struct gem_softc *);
int		gem_reset_rx(struct gem_softc *);
int		gem_reset_tx(struct gem_softc *);
int		gem_disable_rx(struct gem_softc *);
int		gem_disable_tx(struct gem_softc *);
void		gem_rx_watchdog(void *);
void		gem_rxdrain(struct gem_softc *);
void		gem_fill_rx_ring(struct gem_softc *);
int		gem_add_rxbuf(struct gem_softc *, int idx);
int		gem_load_mbuf(struct gem_softc *, struct gem_sxd *,
		    struct mbuf *);
void		gem_iff(struct gem_softc *);

/* MII methods & callbacks */
int		gem_mii_readreg(struct device *, int, int);
void		gem_mii_writereg(struct device *, int, int, int);
void		gem_mii_statchg(struct device *);
int		gem_pcs_readreg(struct device *, int, int);
void		gem_pcs_writereg(struct device *, int, int, int);

int		gem_mediachange(struct ifnet *);
void		gem_mediastatus(struct ifnet *, struct ifmediareq *);

int		gem_eint(struct gem_softc *, u_int);
int		gem_rint(struct gem_softc *);
int		gem_tint(struct gem_softc *, u_int32_t);
int		gem_pint(struct gem_softc *);

#ifdef GEM_DEBUG
#define	DPRINTF(sc, x)	if ((sc)->sc_arpcom.ac_if.if_flags & IFF_DEBUG) \
				printf x
#else
#define	DPRINTF(sc, x)	/* nothing */
#endif

/*
 * Attach a Gem interface to the system.
 */
void
gem_config(struct gem_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mii_data *mii = &sc->sc_mii;
	struct mii_softc *child;
	int i, error, mii_flags, phyad;
	struct ifmedia_entry *ifm;

	/* Make sure the chip is stopped. */
	ifp->if_softc = sc;
	gem_reset(sc);

	/*
	 * Allocate the control data structures, and create and load the
	 * DMA map for it.
	 */
	if ((error = bus_dmamem_alloc(sc->sc_dmatag,
	    sizeof(struct gem_control_data), PAGE_SIZE, 0, &sc->sc_cdseg,
	    1, &sc->sc_cdnseg, 0)) != 0) {
		printf("\n%s: unable to allocate control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_0;
	}

	/* XXX should map this in with correct endianness */
	if ((error = bus_dmamem_map(sc->sc_dmatag, &sc->sc_cdseg, sc->sc_cdnseg,
	    sizeof(struct gem_control_data), (caddr_t *)&sc->sc_control_data,
	    BUS_DMA_COHERENT)) != 0) {
		printf("\n%s: unable to map control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_1;
	}

	if ((error = bus_dmamap_create(sc->sc_dmatag,
	    sizeof(struct gem_control_data), 1,
	    sizeof(struct gem_control_data), 0, 0, &sc->sc_cddmamap)) != 0) {
		printf("\n%s: unable to create control data DMA map, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail_2;
	}

	if ((error = bus_dmamap_load(sc->sc_dmatag, sc->sc_cddmamap,
	    sc->sc_control_data, sizeof(struct gem_control_data), NULL,
	    0)) != 0) {
		printf("\n%s: unable to load control data DMA map, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_3;
	}

	/*
	 * Create the receive buffer DMA maps.
	 */
	for (i = 0; i < GEM_NRXDESC; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmatag, MCLBYTES, 1,
		    MCLBYTES, 0, 0, &sc->sc_rxsoft[i].rxs_dmamap)) != 0) {
			printf("\n%s: unable to create rx DMA map %d, "
			    "error = %d\n", sc->sc_dev.dv_xname, i, error);
			goto fail_5;
		}
		sc->sc_rxsoft[i].rxs_mbuf = NULL;
	}
	/*
	 * Create the transmit buffer DMA maps.
	 */
	for (i = 0; i < GEM_NTXDESC; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmatag, MCLBYTES,
		    GEM_NTXSEGS, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &sc->sc_txd[i].sd_map)) != 0) {
			printf("\n%s: unable to create tx DMA map %d, "
			    "error = %d\n", sc->sc_dev.dv_xname, i, error);
			goto fail_6;
		}
		sc->sc_txd[i].sd_mbuf = NULL;
	}

	/*
	 * From this point forward, the attachment cannot fail.  A failure
	 * before this point releases all resources that may have been
	 * allocated.
	 */

	/* Announce ourselves. */
	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/* Get RX FIFO size */
	sc->sc_rxfifosize = 64 *
	    bus_space_read_4(sc->sc_bustag, sc->sc_h1, GEM_RX_FIFO_SIZE);

	/* Initialize ifnet structure. */
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, sizeof ifp->if_xname);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_qstart = gem_start;
	ifp->if_ioctl = gem_ioctl;
	ifp->if_watchdog = gem_watchdog;
	ifq_init_maxlen(&ifp->if_snd, GEM_NTXDESC - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/* Initialize ifmedia structures and MII info */
	mii->mii_ifp = ifp;
	mii->mii_readreg = gem_mii_readreg;
	mii->mii_writereg = gem_mii_writereg;
	mii->mii_statchg = gem_mii_statchg;

	ifmedia_init(&mii->mii_media, 0, gem_mediachange, gem_mediastatus);

	/* Bad things will happen if we touch this register on ERI. */
	if (sc->sc_variant != GEM_SUN_ERI)
		bus_space_write_4(sc->sc_bustag, sc->sc_h1,
		    GEM_MII_DATAPATH_MODE, 0);

	gem_mifinit(sc);

	mii_flags = MIIF_DOPAUSE;

	/* 
	 * Look for an external PHY.
	 */
	if (sc->sc_mif_config & GEM_MIF_CONFIG_MDI1) {
		sc->sc_mif_config |= GEM_MIF_CONFIG_PHY_SEL;
		bus_space_write_4(sc->sc_bustag, sc->sc_h1,
	            GEM_MIF_CONFIG, sc->sc_mif_config);

		switch (sc->sc_variant) {
		case GEM_SUN_ERI:
			phyad = GEM_PHYAD_EXTERNAL;
			break;
		default:
			phyad = MII_PHY_ANY;
			break;
		}

		mii_attach(&sc->sc_dev, mii, 0xffffffff, phyad,
		    MII_OFFSET_ANY, mii_flags);
	}

	/*
	 * Fall back on an internal PHY if no external PHY was found.
	 * Note that with Apple (K2) GMACs GEM_MIF_CONFIG_MDI0 can't be
	 * trusted when the firmware has powered down the chip
	 */
	child = LIST_FIRST(&mii->mii_phys);
	if (child == NULL &&
	    (sc->sc_mif_config & GEM_MIF_CONFIG_MDI0 || GEM_IS_APPLE(sc))) {
		sc->sc_mif_config &= ~GEM_MIF_CONFIG_PHY_SEL;
		bus_space_write_4(sc->sc_bustag, sc->sc_h1,
	            GEM_MIF_CONFIG, sc->sc_mif_config);

		switch (sc->sc_variant) {
		case GEM_SUN_ERI:
		case GEM_APPLE_K2_GMAC:
			phyad = GEM_PHYAD_INTERNAL;
			break;
		case GEM_APPLE_GMAC:
			phyad = GEM_PHYAD_EXTERNAL;
			break;
		default:
			phyad = MII_PHY_ANY;
			break;
		}

		mii_attach(&sc->sc_dev, mii, 0xffffffff, phyad,
		    MII_OFFSET_ANY, mii_flags);
	}

	/* 
	 * Try the external PCS SERDES if we didn't find any MII
	 * devices.
	 */
	child = LIST_FIRST(&mii->mii_phys);
	if (child == NULL && sc->sc_variant != GEM_SUN_ERI) {
		bus_space_write_4(sc->sc_bustag, sc->sc_h1,
		    GEM_MII_DATAPATH_MODE, GEM_MII_DATAPATH_SERDES);

		bus_space_write_4(sc->sc_bustag, sc->sc_h1,
		    GEM_MII_SLINK_CONTROL,
		    GEM_MII_SLINK_LOOPBACK|GEM_MII_SLINK_EN_SYNC_D);

		bus_space_write_4(sc->sc_bustag, sc->sc_h1,
		     GEM_MII_CONFIG, GEM_MII_CONFIG_ENABLE);

		mii->mii_readreg = gem_pcs_readreg;
		mii->mii_writereg = gem_pcs_writereg;

		mii_flags |= MIIF_NOISOLATE;

		mii_attach(&sc->sc_dev, mii, 0xffffffff, MII_PHY_ANY,
		    MII_OFFSET_ANY, mii_flags);
	}

	child = LIST_FIRST(&mii->mii_phys);
	if (child == NULL) {
		/* No PHY attached */
		ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_MANUAL, 0, NULL);
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_MANUAL);
	} else {
		/*
		 * XXX - we can really do the following ONLY if the
		 * phy indeed has the auto negotiation capability!!
		 */
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_AUTO);
	}

	/* Check if we support GigE media. */
	mtx_enter(&ifmedia_mtx);
	TAILQ_FOREACH(ifm, &sc->sc_media.ifm_list, ifm_list) {
		if (IFM_SUBTYPE(ifm->ifm_media) == IFM_1000_T ||
		    IFM_SUBTYPE(ifm->ifm_media) == IFM_1000_SX ||
		    IFM_SUBTYPE(ifm->ifm_media) == IFM_1000_LX ||
		    IFM_SUBTYPE(ifm->ifm_media) == IFM_1000_CX) {
			sc->sc_flags |= GEM_GIGABIT;
			break;
		}
	}
	mtx_leave(&ifmedia_mtx);

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->sc_tick_ch, gem_tick, sc);
	timeout_set(&sc->sc_rx_watchdog, gem_rx_watchdog, sc);
	return;

	/*
	 * Free any resources we've allocated during the failed attach
	 * attempt.  Do this in reverse order and fall through.
	 */
 fail_6:
	for (i = 0; i < GEM_NTXDESC; i++) {
		if (sc->sc_txd[i].sd_map != NULL)
			bus_dmamap_destroy(sc->sc_dmatag,
			    sc->sc_txd[i].sd_map);
	}
 fail_5:
	for (i = 0; i < GEM_NRXDESC; i++) {
		if (sc->sc_rxsoft[i].rxs_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmatag,
			    sc->sc_rxsoft[i].rxs_dmamap);
	}
	bus_dmamap_unload(sc->sc_dmatag, sc->sc_cddmamap);
 fail_3:
	bus_dmamap_destroy(sc->sc_dmatag, sc->sc_cddmamap);
 fail_2:
	bus_dmamem_unmap(sc->sc_dmatag, (caddr_t)sc->sc_control_data,
	    sizeof(struct gem_control_data));
 fail_1:
	bus_dmamem_free(sc->sc_dmatag, &sc->sc_cdseg, sc->sc_cdnseg);
 fail_0:
	return;
}

void
gem_unconfig(struct gem_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int i;

	gem_stop(ifp, 1);

	for (i = 0; i < GEM_NTXDESC; i++) {
		if (sc->sc_txd[i].sd_map != NULL)
			bus_dmamap_destroy(sc->sc_dmatag,
			    sc->sc_txd[i].sd_map);
	}
	for (i = 0; i < GEM_NRXDESC; i++) {
		if (sc->sc_rxsoft[i].rxs_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmatag,
			    sc->sc_rxsoft[i].rxs_dmamap);
	}
	bus_dmamap_unload(sc->sc_dmatag, sc->sc_cddmamap);
	bus_dmamap_destroy(sc->sc_dmatag, sc->sc_cddmamap);
	bus_dmamem_unmap(sc->sc_dmatag, (caddr_t)sc->sc_control_data,
	    sizeof(struct gem_control_data));
	bus_dmamem_free(sc->sc_dmatag, &sc->sc_cdseg, sc->sc_cdnseg);

	/* Detach all PHYs */
	mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete all remaining media. */
	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);
}


void
gem_tick(void *arg)
{
	struct gem_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mac = sc->sc_h1;
	int s;
	u_int32_t v;

	s = splnet();
	/* unload collisions counters */
	v = bus_space_read_4(t, mac, GEM_MAC_EXCESS_COLL_CNT) +
	    bus_space_read_4(t, mac, GEM_MAC_LATE_COLL_CNT);
	ifp->if_collisions += v +
	    bus_space_read_4(t, mac, GEM_MAC_NORM_COLL_CNT) +
	    bus_space_read_4(t, mac, GEM_MAC_FIRST_COLL_CNT);
	ifp->if_oerrors += v;

	/* read error counters */
	ifp->if_ierrors +=
	    bus_space_read_4(t, mac, GEM_MAC_RX_LEN_ERR_CNT) +
	    bus_space_read_4(t, mac, GEM_MAC_RX_ALIGN_ERR) +
	    bus_space_read_4(t, mac, GEM_MAC_RX_CRC_ERR_CNT) +
	    bus_space_read_4(t, mac, GEM_MAC_RX_CODE_VIOL);

	/* clear the hardware counters */
	bus_space_write_4(t, mac, GEM_MAC_NORM_COLL_CNT, 0);
	bus_space_write_4(t, mac, GEM_MAC_FIRST_COLL_CNT, 0);
	bus_space_write_4(t, mac, GEM_MAC_EXCESS_COLL_CNT, 0);
	bus_space_write_4(t, mac, GEM_MAC_LATE_COLL_CNT, 0);
	bus_space_write_4(t, mac, GEM_MAC_RX_LEN_ERR_CNT, 0);
	bus_space_write_4(t, mac, GEM_MAC_RX_ALIGN_ERR, 0);
	bus_space_write_4(t, mac, GEM_MAC_RX_CRC_ERR_CNT, 0);
	bus_space_write_4(t, mac, GEM_MAC_RX_CODE_VIOL, 0);

	/*
	 * If buffer allocation fails, the receive ring may become
	 * empty. There is no receive interrupt to recover from that.
	 */
	if (if_rxr_inuse(&sc->sc_rx_ring) == 0) {
		gem_fill_rx_ring(sc);
		bus_space_write_4(t, mac, GEM_RX_KICK, sc->sc_rx_prod);
	}

	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick_ch, 1);
}

int
gem_bitwait(struct gem_softc *sc, bus_space_handle_t h, int r,
   u_int32_t clr, u_int32_t set)
{
	int i;
	u_int32_t reg;

	for (i = TRIES; i--; DELAY(100)) {
		reg = bus_space_read_4(sc->sc_bustag, h, r);
		if ((reg & clr) == 0 && (reg & set) == set)
			return (1);
	}

	return (0);
}

void
gem_reset(struct gem_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t h = sc->sc_h2;
	int s;

	s = splnet();
	DPRINTF(sc, ("%s: gem_reset\n", sc->sc_dev.dv_xname));
	gem_reset_rx(sc);
	gem_reset_tx(sc);

	/* Do a full reset */
	bus_space_write_4(t, h, GEM_RESET, GEM_RESET_RX|GEM_RESET_TX);
	if (!gem_bitwait(sc, h, GEM_RESET, GEM_RESET_RX | GEM_RESET_TX, 0))
		printf("%s: cannot reset device\n", sc->sc_dev.dv_xname);
	splx(s);
}


/*
 * Drain the receive queue.
 */
void
gem_rxdrain(struct gem_softc *sc)
{
	struct gem_rxsoft *rxs;
	int i;

	for (i = 0; i < GEM_NRXDESC; i++) {
		rxs = &sc->sc_rxsoft[i];
		if (rxs->rxs_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_dmatag, rxs->rxs_dmamap, 0,
			    rxs->rxs_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmatag, rxs->rxs_dmamap);
			m_freem(rxs->rxs_mbuf);
			rxs->rxs_mbuf = NULL;
		}
	}
	sc->sc_rx_prod = sc->sc_rx_cons = 0;
}

/*
 * Reset the whole thing.
 */
void
gem_stop(struct ifnet *ifp, int softonly)
{
	struct gem_softc *sc = (struct gem_softc *)ifp->if_softc;
	struct gem_sxd *sd;
	u_int32_t i;

	DPRINTF(sc, ("%s: gem_stop\n", sc->sc_dev.dv_xname));

	timeout_del(&sc->sc_tick_ch);

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	if (!softonly) {
		mii_down(&sc->sc_mii);

		gem_reset_rx(sc);
		gem_reset_tx(sc);
	}

	intr_barrier(sc->sc_ih);
	ifq_barrier(&ifp->if_snd);

	KASSERT((ifp->if_flags & IFF_RUNNING) == 0);

	/*
	 * Release any queued transmit buffers.
	 */
	for (i = 0; i < GEM_NTXDESC; i++) {
		sd = &sc->sc_txd[i];
		if (sd->sd_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_dmatag, sd->sd_map, 0,
			    sd->sd_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmatag, sd->sd_map);
			m_freem(sd->sd_mbuf);
			sd->sd_mbuf = NULL;
		}
	}
	sc->sc_tx_cnt = sc->sc_tx_prod = sc->sc_tx_cons = 0;

	gem_rxdrain(sc);
}


/*
 * Reset the receiver
 */
int
gem_reset_rx(struct gem_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t h = sc->sc_h1, h2 = sc->sc_h2;

	/*
	 * Resetting while DMA is in progress can cause a bus hang, so we
	 * disable DMA first.
	 */
	gem_disable_rx(sc);
	bus_space_write_4(t, h, GEM_RX_CONFIG, 0);
	/* Wait till it finishes */
	if (!gem_bitwait(sc, h, GEM_RX_CONFIG, 1, 0))
		printf("%s: cannot disable rx dma\n", sc->sc_dev.dv_xname);
	/* Wait 5ms extra. */
	delay(5000);

	/* Finally, reset the ERX */
	bus_space_write_4(t, h2, GEM_RESET, GEM_RESET_RX);
	/* Wait till it finishes */
	if (!gem_bitwait(sc, h2, GEM_RESET, GEM_RESET_RX, 0)) {
		printf("%s: cannot reset receiver\n", sc->sc_dev.dv_xname);
		return (1);
	}
	return (0);
}


/*
 * Reset the transmitter
 */
int
gem_reset_tx(struct gem_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t h = sc->sc_h1, h2 = sc->sc_h2;

	/*
	 * Resetting while DMA is in progress can cause a bus hang, so we
	 * disable DMA first.
	 */
	gem_disable_tx(sc);
	bus_space_write_4(t, h, GEM_TX_CONFIG, 0);
	/* Wait till it finishes */
	if (!gem_bitwait(sc, h, GEM_TX_CONFIG, 1, 0))
		printf("%s: cannot disable tx dma\n", sc->sc_dev.dv_xname);
	/* Wait 5ms extra. */
	delay(5000);

	/* Finally, reset the ETX */
	bus_space_write_4(t, h2, GEM_RESET, GEM_RESET_TX);
	/* Wait till it finishes */
	if (!gem_bitwait(sc, h2, GEM_RESET, GEM_RESET_TX, 0)) {
		printf("%s: cannot reset transmitter\n",
			sc->sc_dev.dv_xname);
		return (1);
	}
	return (0);
}

/*
 * Disable receiver.
 */
int
gem_disable_rx(struct gem_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t h = sc->sc_h1;
	u_int32_t cfg;

	/* Flip the enable bit */
	cfg = bus_space_read_4(t, h, GEM_MAC_RX_CONFIG);
	cfg &= ~GEM_MAC_RX_ENABLE;
	bus_space_write_4(t, h, GEM_MAC_RX_CONFIG, cfg);

	/* Wait for it to finish */
	return (gem_bitwait(sc, h, GEM_MAC_RX_CONFIG, GEM_MAC_RX_ENABLE, 0));
}

/*
 * Disable transmitter.
 */
int
gem_disable_tx(struct gem_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t h = sc->sc_h1;
	u_int32_t cfg;

	/* Flip the enable bit */
	cfg = bus_space_read_4(t, h, GEM_MAC_TX_CONFIG);
	cfg &= ~GEM_MAC_TX_ENABLE;
	bus_space_write_4(t, h, GEM_MAC_TX_CONFIG, cfg);

	/* Wait for it to finish */
	return (gem_bitwait(sc, h, GEM_MAC_TX_CONFIG, GEM_MAC_TX_ENABLE, 0));
}

/*
 * Initialize interface.
 */
int
gem_meminit(struct gem_softc *sc)
{
	int i;

	/*
	 * Initialize the transmit descriptor ring.
	 */
	for (i = 0; i < GEM_NTXDESC; i++) {
		sc->sc_txdescs[i].gd_flags = 0;
		sc->sc_txdescs[i].gd_addr = 0;
	}
	GEM_CDTXSYNC(sc, 0, GEM_NTXDESC,
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	/*
	 * Initialize the receive descriptor and receive job
	 * descriptor rings.
	 */
	for (i = 0; i < GEM_NRXDESC; i++) {
		sc->sc_rxdescs[i].gd_flags = 0;
		sc->sc_rxdescs[i].gd_addr = 0;
	}
	/* Hardware reads RX descriptors in multiples of four. */
	if_rxr_init(&sc->sc_rx_ring, 4, GEM_NRXDESC - 4);
	gem_fill_rx_ring(sc);

	return (0);
}

int
gem_ringsize(int sz)
{
	switch (sz) {
	case 32:
		return GEM_RING_SZ_32;
	case 64:
		return GEM_RING_SZ_64;
	case 128:
		return GEM_RING_SZ_128;
	case 256:
		return GEM_RING_SZ_256;
	case 512:
		return GEM_RING_SZ_512;
	case 1024:
		return GEM_RING_SZ_1024;
	case 2048:
		return GEM_RING_SZ_2048;
	case 4096:
		return GEM_RING_SZ_4096;
	case 8192:
		return GEM_RING_SZ_8192;
	default:
		printf("gem: invalid Receive Descriptor ring size %d\n", sz);
		return GEM_RING_SZ_32;
	}
}

/*
 * Initialization of interface; set up initialization block
 * and transmit/receive descriptor rings.
 */
int
gem_init(struct ifnet *ifp)
{

	struct gem_softc *sc = (struct gem_softc *)ifp->if_softc;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t h = sc->sc_h1;
	int s;
	u_int32_t v;

	s = splnet();

	DPRINTF(sc, ("%s: gem_init: calling stop\n", sc->sc_dev.dv_xname));
	/*
	 * Initialization sequence. The numbered steps below correspond
	 * to the sequence outlined in section 6.3.5.1 in the Ethernet
	 * Channel Engine manual (part of the PCIO manual).
	 * See also the STP2002-STQ document from Sun Microsystems.
	 */

	/* step 1 & 2. Reset the Ethernet Channel */
	gem_stop(ifp, 0);
	gem_reset(sc);
	DPRINTF(sc, ("%s: gem_init: restarting\n", sc->sc_dev.dv_xname));

	/* Re-initialize the MIF */
	gem_mifinit(sc);

	/* Call MI reset function if any */
	if (sc->sc_hwreset)
		(*sc->sc_hwreset)(sc);

	/* step 3. Setup data structures in host memory */
	gem_meminit(sc);

	/* step 4. TX MAC registers & counters */
	gem_init_regs(sc);

	/* step 5. RX MAC registers & counters */
	gem_iff(sc);

	/* step 6 & 7. Program Descriptor Ring Base Addresses */
	bus_space_write_4(t, h, GEM_TX_RING_PTR_HI, 
	    (((uint64_t)GEM_CDTXADDR(sc,0)) >> 32));
	bus_space_write_4(t, h, GEM_TX_RING_PTR_LO, GEM_CDTXADDR(sc, 0));

	bus_space_write_4(t, h, GEM_RX_RING_PTR_HI, 
	    (((uint64_t)GEM_CDRXADDR(sc,0)) >> 32));
	bus_space_write_4(t, h, GEM_RX_RING_PTR_LO, GEM_CDRXADDR(sc, 0));

	/* step 8. Global Configuration & Interrupt Mask */
	bus_space_write_4(t, h, GEM_INTMASK,
		      ~(GEM_INTR_TX_INTME|
			GEM_INTR_TX_EMPTY|
			GEM_INTR_RX_DONE|GEM_INTR_RX_NOBUF|
			GEM_INTR_RX_TAG_ERR|GEM_INTR_PCS|
			GEM_INTR_MAC_CONTROL|GEM_INTR_MIF|
			GEM_INTR_BERR));
	bus_space_write_4(t, h, GEM_MAC_RX_MASK,
	    GEM_MAC_RX_DONE|GEM_MAC_RX_FRAME_CNT);
	bus_space_write_4(t, h, GEM_MAC_TX_MASK, 0xffff); /* XXXX */
	bus_space_write_4(t, h, GEM_MAC_CONTROL_MASK, 0); /* XXXX */

	/* step 9. ETX Configuration: use mostly default values */

	/* Enable DMA */
	v = gem_ringsize(GEM_NTXDESC /*XXX*/);
	v |= ((sc->sc_variant == GEM_SUN_ERI ? 0x100 : 0x04ff) << 10) &
	    GEM_TX_CONFIG_TXFIFO_TH;
	bus_space_write_4(t, h, GEM_TX_CONFIG, v | GEM_TX_CONFIG_TXDMA_EN);
	bus_space_write_4(t, h, GEM_TX_KICK, 0);

	/* step 10. ERX Configuration */

	/* Encode Receive Descriptor ring size: four possible values */
	v = gem_ringsize(GEM_NRXDESC /*XXX*/);
	/* Enable DMA */
	bus_space_write_4(t, h, GEM_RX_CONFIG, 
		v|(GEM_THRSH_1024<<GEM_RX_CONFIG_FIFO_THRS_SHIFT)|
		(2<<GEM_RX_CONFIG_FBOFF_SHFT)|GEM_RX_CONFIG_RXDMA_EN|
		(0<<GEM_RX_CONFIG_CXM_START_SHFT));
	/*
	 * The following value is for an OFF Threshold of about 3/4 full
	 * and an ON Threshold of 1/4 full.
	 */
	bus_space_write_4(t, h, GEM_RX_PAUSE_THRESH,
	    (3 * sc->sc_rxfifosize / 256) |
	    ((sc->sc_rxfifosize / 256) << 12));
	bus_space_write_4(t, h, GEM_RX_BLANKING, (6 << 12) | 6);

	/* step 11. Configure Media */
	mii_mediachg(&sc->sc_mii);

	/* step 12. RX_MAC Configuration Register */
	v = bus_space_read_4(t, h, GEM_MAC_RX_CONFIG);
	v |= GEM_MAC_RX_ENABLE | GEM_MAC_RX_STRIP_CRC;
	bus_space_write_4(t, h, GEM_MAC_RX_CONFIG, v);

	/* step 14. Issue Transmit Pending command */

	/* Call MI initialization function if any */
	if (sc->sc_hwinit)
		(*sc->sc_hwinit)(sc);

	/* step 15.  Give the receiver a swift kick */
	bus_space_write_4(t, h, GEM_RX_KICK, sc->sc_rx_prod);

	/* Start the one second timer. */
	timeout_add_sec(&sc->sc_tick_ch, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	return (0);
}

void
gem_init_regs(struct gem_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t h = sc->sc_h1;
	u_int32_t v;

	/* These regs are not cleared on reset */
	sc->sc_inited = 0;
	if (!sc->sc_inited) {
		/* Load recommended values */
		bus_space_write_4(t, h, GEM_MAC_IPG0, 0x00);
		bus_space_write_4(t, h, GEM_MAC_IPG1, 0x08);
		bus_space_write_4(t, h, GEM_MAC_IPG2, 0x04);

		bus_space_write_4(t, h, GEM_MAC_MAC_MIN_FRAME, ETHER_MIN_LEN);
		/* Max frame and max burst size */
		bus_space_write_4(t, h, GEM_MAC_MAC_MAX_FRAME,
		    (ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN) | (0x2000 << 16));

		bus_space_write_4(t, h, GEM_MAC_PREAMBLE_LEN, 0x07);
		bus_space_write_4(t, h, GEM_MAC_JAM_SIZE, 0x04);
		bus_space_write_4(t, h, GEM_MAC_ATTEMPT_LIMIT, 0x10);
		bus_space_write_4(t, h, GEM_MAC_CONTROL_TYPE, 0x8088);
		bus_space_write_4(t, h, GEM_MAC_RANDOM_SEED,
		    ((sc->sc_arpcom.ac_enaddr[5]<<8)|sc->sc_arpcom.ac_enaddr[4])&0x3ff);

		/* Secondary MAC addr set to 0:0:0:0:0:0 */
		bus_space_write_4(t, h, GEM_MAC_ADDR3, 0);
		bus_space_write_4(t, h, GEM_MAC_ADDR4, 0);
		bus_space_write_4(t, h, GEM_MAC_ADDR5, 0);

		/* MAC control addr set to 0:1:c2:0:1:80 */
		bus_space_write_4(t, h, GEM_MAC_ADDR6, 0x0001);
		bus_space_write_4(t, h, GEM_MAC_ADDR7, 0xc200);
		bus_space_write_4(t, h, GEM_MAC_ADDR8, 0x0180);

		/* MAC filter addr set to 0:0:0:0:0:0 */
		bus_space_write_4(t, h, GEM_MAC_ADDR_FILTER0, 0);
		bus_space_write_4(t, h, GEM_MAC_ADDR_FILTER1, 0);
		bus_space_write_4(t, h, GEM_MAC_ADDR_FILTER2, 0);

		bus_space_write_4(t, h, GEM_MAC_ADR_FLT_MASK1_2, 0);
		bus_space_write_4(t, h, GEM_MAC_ADR_FLT_MASK0, 0);

		sc->sc_inited = 1;
	}

	/* Counters need to be zeroed */
	bus_space_write_4(t, h, GEM_MAC_NORM_COLL_CNT, 0);
	bus_space_write_4(t, h, GEM_MAC_FIRST_COLL_CNT, 0);
	bus_space_write_4(t, h, GEM_MAC_EXCESS_COLL_CNT, 0);
	bus_space_write_4(t, h, GEM_MAC_LATE_COLL_CNT, 0);
	bus_space_write_4(t, h, GEM_MAC_DEFER_TMR_CNT, 0);
	bus_space_write_4(t, h, GEM_MAC_PEAK_ATTEMPTS, 0);
	bus_space_write_4(t, h, GEM_MAC_RX_FRAME_COUNT, 0);
	bus_space_write_4(t, h, GEM_MAC_RX_LEN_ERR_CNT, 0);
	bus_space_write_4(t, h, GEM_MAC_RX_ALIGN_ERR, 0);
	bus_space_write_4(t, h, GEM_MAC_RX_CRC_ERR_CNT, 0);
	bus_space_write_4(t, h, GEM_MAC_RX_CODE_VIOL, 0);

	/* Set XOFF PAUSE time */
	bus_space_write_4(t, h, GEM_MAC_SEND_PAUSE_CMD, 0x1bf0);

	/*
	 * Set the internal arbitration to "infinite" bursts of the
	 * maximum length of 31 * 64 bytes so DMA transfers aren't
	 * split up in cache line size chunks. This greatly improves
	 * especially RX performance.
	 * Enable silicon bug workarounds for the Apple variants.
	 */
	v = GEM_CONFIG_TXDMA_LIMIT | GEM_CONFIG_RXDMA_LIMIT;
	if (sc->sc_pci)
		v |= GEM_CONFIG_BURST_INF;
	else
		v |= GEM_CONFIG_BURST_64;
	if (sc->sc_variant != GEM_SUN_GEM && sc->sc_variant != GEM_SUN_ERI)
		v |= GEM_CONFIG_RONPAULBIT | GEM_CONFIG_BUG2FIX;
	bus_space_write_4(t, h, GEM_CONFIG, v);

	/*
	 * Set the station address.
	 */
	bus_space_write_4(t, h, GEM_MAC_ADDR0, 
		(sc->sc_arpcom.ac_enaddr[4]<<8) | sc->sc_arpcom.ac_enaddr[5]);
	bus_space_write_4(t, h, GEM_MAC_ADDR1, 
		(sc->sc_arpcom.ac_enaddr[2]<<8) | sc->sc_arpcom.ac_enaddr[3]);
	bus_space_write_4(t, h, GEM_MAC_ADDR2, 
		(sc->sc_arpcom.ac_enaddr[0]<<8) | sc->sc_arpcom.ac_enaddr[1]);
}

/*
 * Receive interrupt.
 */
int
gem_rint(struct gem_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t h = sc->sc_h1;
	struct gem_rxsoft *rxs;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	u_int64_t rxstat;
	int i, len;

	if (if_rxr_inuse(&sc->sc_rx_ring) == 0)
		return (0);

	for (i = sc->sc_rx_cons; if_rxr_inuse(&sc->sc_rx_ring) > 0;
	    i = GEM_NEXTRX(i)) {
		rxs = &sc->sc_rxsoft[i];

		GEM_CDRXSYNC(sc, i,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

		rxstat = GEM_DMA_READ(sc, &sc->sc_rxdescs[i].gd_flags);

		if (rxstat & GEM_RD_OWN) {
			/* We have processed all of the receive buffers. */
			break;
		}

		bus_dmamap_sync(sc->sc_dmatag, rxs->rxs_dmamap, 0,
		    rxs->rxs_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmatag, rxs->rxs_dmamap);

		m = rxs->rxs_mbuf;
		rxs->rxs_mbuf = NULL;

		if_rxr_put(&sc->sc_rx_ring, 1);

		if (rxstat & GEM_RD_BAD_CRC) {
			ifp->if_ierrors++;
#ifdef GEM_DEBUG
			printf("%s: receive error: CRC error\n",
				sc->sc_dev.dv_xname);
#endif
			m_freem(m);
			continue;
		}

#ifdef GEM_DEBUG
		if (ifp->if_flags & IFF_DEBUG) {
			printf("    rxsoft %p descriptor %d: ", rxs, i);
			printf("gd_flags: 0x%016llx\t", (long long)
				GEM_DMA_READ(sc, &sc->sc_rxdescs[i].gd_flags));
			printf("gd_addr: 0x%016llx\n", (long long)
				GEM_DMA_READ(sc, &sc->sc_rxdescs[i].gd_addr));
		}
#endif

		/* No errors; receive the packet. */
		len = GEM_RD_BUFLEN(rxstat);

		m->m_data += 2; /* We're already off by two */
		m->m_pkthdr.len = m->m_len = len;

		ml_enqueue(&ml, m);
	}

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sc_rx_ring);

	/* Update the receive pointer. */
	sc->sc_rx_cons = i;
	gem_fill_rx_ring(sc);
	bus_space_write_4(t, h, GEM_RX_KICK, sc->sc_rx_prod);

	DPRINTF(sc, ("gem_rint: done sc->sc_rx_cons %d, complete %d\n",
		sc->sc_rx_cons, bus_space_read_4(t, h, GEM_RX_COMPLETION)));

	return (1);
}

void
gem_fill_rx_ring(struct gem_softc *sc)
{
	u_int slots;

	for (slots = if_rxr_get(&sc->sc_rx_ring, GEM_NRXDESC - 4);
	    slots > 0; slots--) {
		if (gem_add_rxbuf(sc, sc->sc_rx_prod))
			break;
	}
	if_rxr_put(&sc->sc_rx_ring, slots);
}

/*
 * Add a receive buffer to the indicated descriptor.
 */
int
gem_add_rxbuf(struct gem_softc *sc, int idx)
{
	struct gem_rxsoft *rxs = &sc->sc_rxsoft[idx];
	struct mbuf *m;
	int error;

	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (!m)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;

#ifdef GEM_DEBUG
/* bzero the packet to check dma */
	memset(m->m_ext.ext_buf, 0, m->m_ext.ext_size);
#endif

	rxs->rxs_mbuf = m;

	error = bus_dmamap_load_mbuf(sc->sc_dmatag, rxs->rxs_dmamap, m,
	    BUS_DMA_READ|BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: can't load rx DMA map %d, error = %d\n",
		    sc->sc_dev.dv_xname, idx, error);
		panic("gem_add_rxbuf");	/* XXX */
	}

	bus_dmamap_sync(sc->sc_dmatag, rxs->rxs_dmamap, 0,
	    rxs->rxs_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);

	GEM_INIT_RXDESC(sc, idx);

	sc->sc_rx_prod = GEM_NEXTRX(sc->sc_rx_prod);

	return (0);
}

int
gem_eint(struct gem_softc *sc, u_int status)
{
	if ((status & GEM_INTR_MIF) != 0) {
#ifdef GEM_DEBUG
		printf("%s: link status changed\n", sc->sc_dev.dv_xname);
#endif
		return (1);
	}

	printf("%s: status=%b\n", sc->sc_dev.dv_xname, status, GEM_INTR_BITS);
	return (1);
}

int
gem_pint(struct gem_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t seb = sc->sc_h1;
	u_int32_t status;

	status = bus_space_read_4(t, seb, GEM_MII_INTERRUP_STATUS);
	status |= bus_space_read_4(t, seb, GEM_MII_INTERRUP_STATUS);
#ifdef GEM_DEBUG
	if (status)
		printf("%s: link status changed\n", sc->sc_dev.dv_xname);
#endif
	return (1);
}

int
gem_intr(void *v)
{
	struct gem_softc *sc = (struct gem_softc *)v;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t seb = sc->sc_h1;
	u_int32_t status;
	int r = 0;

	status = bus_space_read_4(t, seb, GEM_STATUS);
	DPRINTF(sc, ("%s: gem_intr: cplt %xstatus %b\n",
		sc->sc_dev.dv_xname, (status>>19), status, GEM_INTR_BITS));

	if (status == 0xffffffff)
		return (0);

	if ((status & GEM_INTR_PCS) != 0)
		r |= gem_pint(sc);

	if ((status & (GEM_INTR_RX_TAG_ERR | GEM_INTR_BERR)) != 0)
		r |= gem_eint(sc, status);

	if ((status & (GEM_INTR_TX_EMPTY | GEM_INTR_TX_INTME)) != 0)
		r |= gem_tint(sc, status);

	if ((status & (GEM_INTR_RX_DONE | GEM_INTR_RX_NOBUF)) != 0)
		r |= gem_rint(sc);

	/* We should eventually do more than just print out error stats. */
	if (status & GEM_INTR_TX_MAC) {
		int txstat = bus_space_read_4(t, seb, GEM_MAC_TX_STATUS);
#ifdef GEM_DEBUG
		if (txstat & ~GEM_MAC_TX_XMIT_DONE)
			printf("%s: MAC tx fault, status %x\n",
			    sc->sc_dev.dv_xname, txstat);
#endif
		if (txstat & (GEM_MAC_TX_UNDERRUN | GEM_MAC_TX_PKT_TOO_LONG)) {
			KERNEL_LOCK();
			gem_init(ifp);
			KERNEL_UNLOCK();
		}
	}
	if (status & GEM_INTR_RX_MAC) {
		int rxstat = bus_space_read_4(t, seb, GEM_MAC_RX_STATUS);
#ifdef GEM_DEBUG
 		if (rxstat & ~GEM_MAC_RX_DONE)
 			printf("%s: MAC rx fault, status %x\n",
 			    sc->sc_dev.dv_xname, rxstat);
#endif
		if (rxstat & GEM_MAC_RX_OVERFLOW) {
			ifp->if_ierrors++;

			/*
			 * Apparently a silicon bug causes ERI to hang
			 * from time to time.  So if we detect an RX
			 * FIFO overflow, we fire off a timer, and
			 * check whether we're still making progress
			 * by looking at the RX FIFO write and read
			 * pointers.
			 */
			sc->sc_rx_fifo_wr_ptr =
				bus_space_read_4(t, seb, GEM_RX_FIFO_WR_PTR);
			sc->sc_rx_fifo_rd_ptr =
				bus_space_read_4(t, seb, GEM_RX_FIFO_RD_PTR);
			timeout_add_msec(&sc->sc_rx_watchdog, 400);
		}
#ifdef GEM_DEBUG
		else if (rxstat & ~(GEM_MAC_RX_DONE | GEM_MAC_RX_FRAME_CNT))
			printf("%s: MAC rx fault, status %x\n",
			    sc->sc_dev.dv_xname, rxstat);
#endif
	}
	return (r);
}

void
gem_rx_watchdog(void *arg)
{
	struct gem_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t h = sc->sc_h1;
	u_int32_t rx_fifo_wr_ptr;
	u_int32_t rx_fifo_rd_ptr;
	u_int32_t state;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	rx_fifo_wr_ptr = bus_space_read_4(t, h, GEM_RX_FIFO_WR_PTR);
	rx_fifo_rd_ptr = bus_space_read_4(t, h, GEM_RX_FIFO_RD_PTR);
	state = bus_space_read_4(t, h, GEM_MAC_MAC_STATE);
	if ((state & GEM_MAC_STATE_OVERFLOW) == GEM_MAC_STATE_OVERFLOW) {
		if ((rx_fifo_wr_ptr == rx_fifo_rd_ptr) ||
		     ((sc->sc_rx_fifo_wr_ptr == rx_fifo_wr_ptr) &&
		      (sc->sc_rx_fifo_rd_ptr == rx_fifo_rd_ptr))) {
			/*
			 * The RX state machine is still in overflow state and
			 * the RX FIFO write and read pointers seem to be
			 * stuck.  Whack the chip over the head to get things
			 * going again.
			 */
			gem_init(ifp);
		} else {
			/*
			 * We made some progress, but is not certain that the
			 * overflow condition has been resolved.  Check again.
			 */
			sc->sc_rx_fifo_wr_ptr = rx_fifo_wr_ptr;
			sc->sc_rx_fifo_rd_ptr = rx_fifo_rd_ptr;
			timeout_add_msec(&sc->sc_rx_watchdog, 400);
		}
	}
}

void
gem_watchdog(struct ifnet *ifp)
{
	struct gem_softc *sc = ifp->if_softc;

	DPRINTF(sc, ("gem_watchdog: GEM_RX_CONFIG %x GEM_MAC_RX_STATUS %x "
		"GEM_MAC_RX_CONFIG %x\n",
		bus_space_read_4(sc->sc_bustag, sc->sc_h1, GEM_RX_CONFIG),
		bus_space_read_4(sc->sc_bustag, sc->sc_h1, GEM_MAC_RX_STATUS),
		bus_space_read_4(sc->sc_bustag, sc->sc_h1, GEM_MAC_RX_CONFIG)));

	log(LOG_ERR, "%s: device timeout\n", sc->sc_dev.dv_xname);
	++ifp->if_oerrors;

	/* Try to get more packets going. */
	gem_init(ifp);
}

/*
 * Initialize the MII Management Interface
 */
void
gem_mifinit(struct gem_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mif = sc->sc_h1;

	/* Configure the MIF in frame mode */
	sc->sc_mif_config = bus_space_read_4(t, mif, GEM_MIF_CONFIG);
	sc->sc_mif_config &= ~GEM_MIF_CONFIG_BB_ENA;
	bus_space_write_4(t, mif, GEM_MIF_CONFIG, sc->sc_mif_config);
}

/*
 * MII interface
 *
 * The GEM MII interface supports at least three different operating modes:
 *
 * Bitbang mode is implemented using data, clock and output enable registers.
 *
 * Frame mode is implemented by loading a complete frame into the frame
 * register and polling the valid bit for completion.
 *
 * Polling mode uses the frame register but completion is indicated by
 * an interrupt.
 *
 */
int
gem_mii_readreg(struct device *self, int phy, int reg)
{
	struct gem_softc *sc = (void *)self;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mif = sc->sc_h1;
	int n;
	u_int32_t v;

#ifdef GEM_DEBUG
	if (sc->sc_debug)
		printf("gem_mii_readreg: phy %d reg %d\n", phy, reg);
#endif

	/* Construct the frame command */
	v = (reg << GEM_MIF_REG_SHIFT)	| (phy << GEM_MIF_PHY_SHIFT) |
		GEM_MIF_FRAME_READ;

	bus_space_write_4(t, mif, GEM_MIF_FRAME, v);
	for (n = 0; n < 100; n++) {
		DELAY(1);
		v = bus_space_read_4(t, mif, GEM_MIF_FRAME);
		if (v & GEM_MIF_FRAME_TA0)
			return (v & GEM_MIF_FRAME_DATA);
	}

	printf("%s: mii_read timeout\n", sc->sc_dev.dv_xname);
	return (0);
}

void
gem_mii_writereg(struct device *self, int phy, int reg, int val)
{
	struct gem_softc *sc = (void *)self;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mif = sc->sc_h1;
	int n;
	u_int32_t v;

#ifdef GEM_DEBUG
	if (sc->sc_debug)
		printf("gem_mii_writereg: phy %d reg %d val %x\n",
			phy, reg, val);
#endif

	/* Construct the frame command */
	v = GEM_MIF_FRAME_WRITE			|
	    (phy << GEM_MIF_PHY_SHIFT)		|
	    (reg << GEM_MIF_REG_SHIFT)		|
	    (val & GEM_MIF_FRAME_DATA);

	bus_space_write_4(t, mif, GEM_MIF_FRAME, v);
	for (n = 0; n < 100; n++) {
		DELAY(1);
		v = bus_space_read_4(t, mif, GEM_MIF_FRAME);
		if (v & GEM_MIF_FRAME_TA0)
			return;
	}

	printf("%s: mii_write timeout\n", sc->sc_dev.dv_xname);
}

void
gem_mii_statchg(struct device *dev)
{
	struct gem_softc *sc = (void *)dev;
#ifdef GEM_DEBUG
	uint64_t instance = IFM_INST(sc->sc_mii.mii_media.ifm_cur->ifm_media);
#endif
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mac = sc->sc_h1;
	u_int32_t v;

#ifdef GEM_DEBUG
	if (sc->sc_debug)
		printf("gem_mii_statchg: status change: phy = %lld\n", instance);
#endif

	/* Set tx full duplex options */
	bus_space_write_4(t, mac, GEM_MAC_TX_CONFIG, 0);
	delay(10000); /* reg must be cleared and delay before changing. */
	v = GEM_MAC_TX_ENA_IPG0|GEM_MAC_TX_NGU|GEM_MAC_TX_NGU_LIMIT|
		GEM_MAC_TX_ENABLE;
	if ((IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_FDX) != 0) {
		v |= GEM_MAC_TX_IGN_CARRIER|GEM_MAC_TX_IGN_COLLIS;
	}
	bus_space_write_4(t, mac, GEM_MAC_TX_CONFIG, v);

	/* XIF Configuration */
	v = GEM_MAC_XIF_TX_MII_ENA;
	v |= GEM_MAC_XIF_LINK_LED;

	/* External MII needs echo disable if half duplex. */
	if ((IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_FDX) != 0)
		/* turn on full duplex LED */
		v |= GEM_MAC_XIF_FDPLX_LED;
	else
		/* half duplex -- disable echo */
		v |= GEM_MAC_XIF_ECHO_DISABL;

	switch (IFM_SUBTYPE(sc->sc_mii.mii_media_active)) {
	case IFM_1000_T:  /* Gigabit using GMII interface */
	case IFM_1000_SX:
		v |= GEM_MAC_XIF_GMII_MODE;
		break;
	default:
		v &= ~GEM_MAC_XIF_GMII_MODE;
	}
	bus_space_write_4(t, mac, GEM_MAC_XIF_CONFIG, v);

	/*
	 * 802.3x flow control
	 */
	v = bus_space_read_4(t, mac, GEM_MAC_CONTROL_CONFIG);
	v &= ~(GEM_MAC_CC_RX_PAUSE | GEM_MAC_CC_TX_PAUSE);
	if ((IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_ETH_RXPAUSE) != 0)
		v |= GEM_MAC_CC_RX_PAUSE;
	if ((IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_ETH_TXPAUSE) != 0)
		v |= GEM_MAC_CC_TX_PAUSE;
	bus_space_write_4(t, mac, GEM_MAC_CONTROL_CONFIG, v);
}

int
gem_pcs_readreg(struct device *self, int phy, int reg)
{
	struct gem_softc *sc = (void *)self;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t pcs = sc->sc_h1;

#ifdef GEM_DEBUG
	if (sc->sc_debug)
		printf("gem_pcs_readreg: phy %d reg %d\n", phy, reg);
#endif

	if (phy != GEM_PHYAD_EXTERNAL)
		return (0);

	switch (reg) {
	case MII_BMCR:
		reg = GEM_MII_CONTROL;
		break;
	case MII_BMSR:
		reg = GEM_MII_STATUS;
		break;
	case MII_ANAR:
		reg = GEM_MII_ANAR;
		break;
	case MII_ANLPAR:
		reg = GEM_MII_ANLPAR;
		break;
	case MII_EXTSR:
		return (EXTSR_1000XFDX|EXTSR_1000XHDX);
	default:
		return (0);
	}

	return bus_space_read_4(t, pcs, reg);
}

void
gem_pcs_writereg(struct device *self, int phy, int reg, int val)
{
	struct gem_softc *sc = (void *)self;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t pcs = sc->sc_h1;
	int reset = 0;

#ifdef GEM_DEBUG
	if (sc->sc_debug)
		printf("gem_pcs_writereg: phy %d reg %d val %x\n",
			phy, reg, val);
#endif

	if (phy != GEM_PHYAD_EXTERNAL)
		return;

	if (reg == MII_ANAR)
		bus_space_write_4(t, pcs, GEM_MII_CONFIG, 0);

	switch (reg) {
	case MII_BMCR:
		reset = (val & GEM_MII_CONTROL_RESET);
		reg = GEM_MII_CONTROL;
		break;
	case MII_BMSR:
		reg = GEM_MII_STATUS;
		break;
	case MII_ANAR:
		reg = GEM_MII_ANAR;
		break;
	case MII_ANLPAR:
		reg = GEM_MII_ANLPAR;
		break;
	default:
		return;
	}

	bus_space_write_4(t, pcs, reg, val);

	if (reset)
		gem_bitwait(sc, pcs, GEM_MII_CONTROL, GEM_MII_CONTROL_RESET, 0);

	if (reg == GEM_MII_ANAR || reset) {
		bus_space_write_4(t, pcs, GEM_MII_SLINK_CONTROL,
		    GEM_MII_SLINK_LOOPBACK|GEM_MII_SLINK_EN_SYNC_D);
		bus_space_write_4(t, pcs, GEM_MII_CONFIG,
		    GEM_MII_CONFIG_ENABLE);
	}
}

int
gem_mediachange(struct ifnet *ifp)
{
	struct gem_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;

	if (mii->mii_instance) {
		struct mii_softc *miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}

	return (mii_mediachg(&sc->sc_mii));
}

void
gem_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct gem_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
}

/*
 * Process an ioctl request.
 */
int
gem_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct gem_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if ((ifp->if_flags & IFF_RUNNING) == 0)
			gem_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				gem_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				gem_stop(ifp, 0);
		}
#ifdef GEM_DEBUG
		sc->sc_debug = (ifp->if_flags & IFF_DEBUG) != 0 ? 1 : 0;
#endif
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCGIFRXR:
		error = if_rxr_ioctl((struct if_rxrinfo *)ifr->ifr_data,
		    NULL, MCLBYTES, &sc->sc_rx_ring);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			gem_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
gem_iff(struct gem_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct arpcom *ac = &sc->sc_arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t h = sc->sc_h1;
	u_int32_t crc, hash[16], rxcfg;
	int i;

	rxcfg = bus_space_read_4(t, h, GEM_MAC_RX_CONFIG);
	rxcfg &= ~(GEM_MAC_RX_HASH_FILTER | GEM_MAC_RX_PROMISCUOUS |
	    GEM_MAC_RX_PROMISC_GRP);
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxcfg |= GEM_MAC_RX_PROMISCUOUS;
		else
			rxcfg |= GEM_MAC_RX_PROMISC_GRP;
	} else {
		/*
		 * Set up multicast address filter by passing all multicast
		 * addresses through a crc generator, and then using the
		 * high order 8 bits as an index into the 256 bit logical
		 * address filter.  The high order 4 bits selects the word,
		 * while the other 4 bits select the bit within the word
		 * (where bit 0 is the MSB).
		 */

		rxcfg |= GEM_MAC_RX_HASH_FILTER;

		/* Clear hash table */
		for (i = 0; i < 16; i++)
			hash[i] = 0;

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			crc = ether_crc32_le(enm->enm_addrlo,
			    ETHER_ADDR_LEN);

			/* Just want the 8 most significant bits. */
			crc >>= 24;

			/* Set the corresponding bit in the filter. */
			hash[crc >> 4] |= 1 << (15 - (crc & 15));

			ETHER_NEXT_MULTI(step, enm);
		}

		/* Now load the hash table into the chip (if we are using it) */
		for (i = 0; i < 16; i++) {
			bus_space_write_4(t, h,
			    GEM_MAC_HASH0 + i * (GEM_MAC_HASH1 - GEM_MAC_HASH0),
			    hash[i]);
		}
	}

	bus_space_write_4(t, h, GEM_MAC_RX_CONFIG, rxcfg);
}

/*
 * Transmit interrupt.
 */
int
gem_tint(struct gem_softc *sc, u_int32_t status)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct gem_sxd *sd;
	u_int32_t cons, prod;
	int free = 0;

	prod = status >> 19;
	cons = sc->sc_tx_cons;
	while (cons != prod) {
		sd = &sc->sc_txd[cons];
		if (sd->sd_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_dmatag, sd->sd_map, 0,
			    sd->sd_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmatag, sd->sd_map);
			m_freem(sd->sd_mbuf);
			sd->sd_mbuf = NULL;
		}

		free = 1;

		cons++;
		cons &= GEM_NTXDESC - 1;
	}

	if (free == 0)
		return (0);

	sc->sc_tx_cons = cons;

	if (sc->sc_tx_prod == cons)
		ifp->if_timer = 0;

	if (ifq_is_oactive(&ifp->if_snd))
		ifq_restart(&ifp->if_snd);

	return (1);
}

int
gem_load_mbuf(struct gem_softc *sc, struct gem_sxd *sd, struct mbuf *m)
{
	int error;

	error = bus_dmamap_load_mbuf(sc->sc_dmatag, sd->sd_map, m,
	    BUS_DMA_NOWAIT);
	switch (error) {
	case 0:
		break;

	case EFBIG: /* mbuf chain is too fragmented */
		if (m_defrag(m, M_DONTWAIT) == 0 &&
		    bus_dmamap_load_mbuf(sc->sc_dmatag, sd->sd_map, m,
		    BUS_DMA_NOWAIT) == 0)
		    	break;
		/* FALLTHROUGH */
	default:
		return (1);
	}

	return (0);
}

void
gem_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct gem_softc *sc = ifp->if_softc;
	struct gem_sxd *sd;
	struct mbuf *m;
	uint64_t flags, nflags;
	bus_dmamap_t map;
	uint32_t prod;
	uint32_t free, used = 0;
	uint32_t first, last;
	int i;

	prod = sc->sc_tx_prod;

	/* figure out space */
	free = sc->sc_tx_cons;
	if (free <= prod)
		free += GEM_NTXDESC;
	free -= prod;

	bus_dmamap_sync(sc->sc_dmatag, sc->sc_cddmamap,
	    0, sizeof(struct gem_desc) * GEM_NTXDESC,
	    BUS_DMASYNC_PREWRITE);

	for (;;) {
		if (used + GEM_NTXSEGS + 1 > free) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		first = prod;
		sd = &sc->sc_txd[first];
		map = sd->sd_map;

		if (gem_load_mbuf(sc, sd, m)) {
			m_freem(m);
			ifp->if_oerrors++;
			continue;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		bus_dmamap_sync(sc->sc_dmatag, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		nflags = GEM_TD_START_OF_PACKET;
		for (i = 0; i < map->dm_nsegs; i++) {
			flags = nflags |
			    (map->dm_segs[i].ds_len & GEM_TD_BUFSIZE);

			GEM_DMA_WRITE(sc, &sc->sc_txdescs[prod].gd_addr,
			    map->dm_segs[i].ds_addr);
			GEM_DMA_WRITE(sc, &sc->sc_txdescs[prod].gd_flags,
			    flags);

			last = prod;
			prod++;
			prod &= GEM_NTXDESC - 1;

			nflags = 0;
		}
		GEM_DMA_WRITE(sc, &sc->sc_txdescs[last].gd_flags,
		    GEM_TD_END_OF_PACKET | flags);

		used += map->dm_nsegs;
		sc->sc_txd[last].sd_mbuf = m;
		sc->sc_txd[first].sd_map = sc->sc_txd[last].sd_map;
		sc->sc_txd[last].sd_map = map;
	}

	bus_dmamap_sync(sc->sc_dmatag, sc->sc_cddmamap,
	    0, sizeof(struct gem_desc) * GEM_NTXDESC,
	    BUS_DMASYNC_POSTWRITE);

	if (used == 0)
		return;

	/* Commit. */
	sc->sc_tx_prod = prod;

	/* Transmit. */
	bus_space_write_4(sc->sc_bustag, sc->sc_h1, GEM_TX_KICK, prod);

	/* Set timeout in case hardware has problems transmitting. */
	ifp->if_timer = 5;
}

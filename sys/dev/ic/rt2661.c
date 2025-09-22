/*	$OpenBSD: rt2661.c,v 1.100 2024/04/14 03:26:25 jsg Exp $	*/

/*-
 * Copyright (c) 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
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

/*-
 * Ralink Technology RT2561, RT2561S and RT2661 chipset driver
 * http://www.ralinktech.com/
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/timeout.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/intr.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_node.h>

#include <dev/ic/rt2661var.h>
#include <dev/ic/rt2661reg.h>

#include <dev/pci/pcidevs.h>

#ifdef RAL_DEBUG
#define DPRINTF(x)	do { if (rt2661_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (rt2661_debug >= (n)) printf x; } while (0)
int rt2661_debug = 1;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

void		rt2661_attachhook(struct device *);
int		rt2661_alloc_tx_ring(struct rt2661_softc *,
		    struct rt2661_tx_ring *, int);
void		rt2661_reset_tx_ring(struct rt2661_softc *,
		    struct rt2661_tx_ring *);
void		rt2661_free_tx_ring(struct rt2661_softc *,
		    struct rt2661_tx_ring *);
int		rt2661_alloc_rx_ring(struct rt2661_softc *,
		    struct rt2661_rx_ring *, int);
void		rt2661_reset_rx_ring(struct rt2661_softc *,
		    struct rt2661_rx_ring *);
void		rt2661_free_rx_ring(struct rt2661_softc *,
		    struct rt2661_rx_ring *);
struct		ieee80211_node *rt2661_node_alloc(struct ieee80211com *);
void		rt2661_node_free(struct ieee80211com *,
		    struct ieee80211_node *);
int		rt2661_media_change(struct ifnet *);
void		rt2661_next_scan(void *);
void		rt2661_iter_func(void *, struct ieee80211_node *);
void		rt2661_updatestats(void *);
void		rt2661_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);
int		rt2661_newstate(struct ieee80211com *, enum ieee80211_state,
		    int);
uint16_t	rt2661_eeprom_read(struct rt2661_softc *, uint8_t);
void		rt2661_tx_intr(struct rt2661_softc *);
void		rt2661_tx_dma_intr(struct rt2661_softc *,
		    struct rt2661_tx_ring *);
void		rt2661_rx_intr(struct rt2661_softc *);
#ifndef IEEE80211_STA_ONLY
void		rt2661_mcu_beacon_expire(struct rt2661_softc *);
#endif
void		rt2661_mcu_wakeup(struct rt2661_softc *);
void		rt2661_mcu_cmd_intr(struct rt2661_softc *);
int		rt2661_intr(void *);
#if NBPFILTER > 0
uint8_t		rt2661_rxrate(const struct rt2661_rx_desc *);
#endif
int		rt2661_ack_rate(struct ieee80211com *, int);
uint16_t	rt2661_txtime(int, int, uint32_t);
uint8_t		rt2661_plcp_signal(int);
void		rt2661_setup_tx_desc(struct rt2661_softc *,
		    struct rt2661_tx_desc *, uint32_t, uint16_t, int, int,
		    const bus_dma_segment_t *, int, int, u_int8_t);
int		rt2661_tx_mgt(struct rt2661_softc *, struct mbuf *,
		    struct ieee80211_node *);
int		rt2661_tx_data(struct rt2661_softc *, struct mbuf *,
		    struct ieee80211_node *, int);
void		rt2661_start(struct ifnet *);
void		rt2661_watchdog(struct ifnet *);
int		rt2661_ioctl(struct ifnet *, u_long, caddr_t);
void		rt2661_bbp_write(struct rt2661_softc *, uint8_t, uint8_t);
uint8_t		rt2661_bbp_read(struct rt2661_softc *, uint8_t);
void		rt2661_rf_write(struct rt2661_softc *, uint8_t, uint32_t);
int		rt2661_tx_cmd(struct rt2661_softc *, uint8_t, uint16_t);
void		rt2661_select_antenna(struct rt2661_softc *);
void		rt2661_enable_mrr(struct rt2661_softc *);
void		rt2661_set_txpreamble(struct rt2661_softc *);
void		rt2661_set_basicrates(struct rt2661_softc *);
void		rt2661_select_band(struct rt2661_softc *,
		    struct ieee80211_channel *);
void		rt2661_set_chan(struct rt2661_softc *,
		    struct ieee80211_channel *);
void		rt2661_set_bssid(struct rt2661_softc *, const uint8_t *);
void		rt2661_set_macaddr(struct rt2661_softc *, const uint8_t *);
void		rt2661_update_promisc(struct rt2661_softc *);
void		rt2661_updateslot(struct ieee80211com *);
void		rt2661_set_slottime(struct rt2661_softc *);
const char	*rt2661_get_rf(int);
void		rt2661_read_eeprom(struct rt2661_softc *);
int		rt2661_bbp_init(struct rt2661_softc *);
int		rt2661_init(struct ifnet *);
void		rt2661_stop(struct ifnet *, int);
int		rt2661_load_microcode(struct rt2661_softc *);
void		rt2661_rx_tune(struct rt2661_softc *);
#ifdef notyet
void		rt2661_radar_start(struct rt2661_softc *);
int		rt2661_radar_stop(struct rt2661_softc *);
#endif
#ifndef IEEE80211_STA_ONLY
int		rt2661_prepare_beacon(struct rt2661_softc *);
#endif
void		rt2661_enable_tsf_sync(struct rt2661_softc *);
int		rt2661_get_rssi(struct rt2661_softc *, uint8_t);
struct		rt2661_amrr_node *rt2661_amrr_node_alloc(struct ieee80211com *,
		    struct rt2661_node *);
void 		rt2661_amrr_node_free(struct rt2661_softc *,
		    struct rt2661_amrr_node *);
void		rt2661_amrr_node_free_all(struct rt2661_softc *);
void		rt2661_amrr_node_free_unused(struct rt2661_softc *);
struct 		rt2661_amrr_node *rt2661_amrr_node_find(struct rt2661_softc *,
		    u_int8_t);

static const struct {
	uint32_t	reg;
	uint32_t	val;
} rt2661_def_mac[] = {
	RT2661_DEF_MAC
};

static const struct {
	uint8_t	reg;
	uint8_t	val;
} rt2661_def_bbp[] = {
	RT2661_DEF_BBP
};

static const struct rfprog {
	uint8_t		chan;
	uint32_t	r1, r2, r3, r4;
}  rt2661_rf5225_1[] = {
	RT2661_RF5225_1
}, rt2661_rf5225_2[] = {
	RT2661_RF5225_2
};

int
rt2661_attach(void *xsc, int id)
{
	struct rt2661_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t val;
	int error, ac, ntries;

	sc->sc_id = id;

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 15;
	timeout_set(&sc->amrr_to, rt2661_updatestats, sc);
	timeout_set(&sc->scan_to, rt2661_next_scan, sc);

	TAILQ_INIT(&sc->amn);

	/* wait for NIC to initialize */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((val = RAL_READ(sc, RT2661_MAC_CSR0)) != 0)
			break;
		DELAY(1000);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for NIC to initialize\n",
		    sc->sc_dev.dv_xname);
		return EIO;
	}

	/* retrieve RF rev. no and various other things from EEPROM */
	rt2661_read_eeprom(sc);
	printf(", address %s\n", ether_sprintf(ic->ic_myaddr));

	printf("%s: MAC/BBP RT%X, RF %s\n", sc->sc_dev.dv_xname, val,
	    rt2661_get_rf(sc->rf_rev));

	/*
	 * Allocate Tx and Rx rings.
	 */
	for (ac = 0; ac < 4; ac++) {
		error = rt2661_alloc_tx_ring(sc, &sc->txq[ac],
		    RT2661_TX_RING_COUNT);
		if (error != 0) {
			printf("%s: could not allocate Tx ring %d\n",
			    sc->sc_dev.dv_xname, ac);
			goto fail1;
		}
	}

	error = rt2661_alloc_tx_ring(sc, &sc->mgtq, RT2661_MGT_RING_COUNT);
	if (error != 0) {
		printf("%s: could not allocate Mgt ring\n",
		    sc->sc_dev.dv_xname);
		goto fail1;
	}

	error = rt2661_alloc_rx_ring(sc, &sc->rxq, RT2661_RX_RING_COUNT);
	if (error != 0) {
		printf("%s: could not allocate Rx ring\n",
		    sc->sc_dev.dv_xname);
		goto fail2;
	}

	config_mountroot(xsc, rt2661_attachhook);

	return 0;

fail2:	rt2661_free_tx_ring(sc, &sc->mgtq);
fail1:	while (--ac >= 0)
		rt2661_free_tx_ring(sc, &sc->txq[ac]);
	return ENXIO;
}

void
rt2661_attachhook(struct device *self)
{
	struct rt2661_softc *sc = (struct rt2661_softc *)self;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	const char *name = NULL;
	int i, error;

	switch (sc->sc_id) {
	case PCI_PRODUCT_RALINK_RT2561:
		name = "ral-rt2561";
		break;
	case PCI_PRODUCT_RALINK_RT2561S:
		name = "ral-rt2561s";
		break;
	case PCI_PRODUCT_RALINK_RT2661:
		name = "ral-rt2661";
		break;
	}
	if ((error = loadfirmware(name, &sc->ucode, &sc->ucsize)) != 0) {
		printf("%s: error %d, could not read firmware %s\n",
		    sc->sc_dev.dv_xname, error, name);
		return;
	}

	ic->ic_phytype = IEEE80211_T_OFDM; /* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA; /* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
#ifndef IEEE80211_STA_ONLY
	    IEEE80211_C_IBSS |		/* IBSS mode supported */
	    IEEE80211_C_HOSTAP |	/* HostAP mode supported */
#endif
	    IEEE80211_C_TXPMGT |	/* tx power management */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WEP |		/* s/w WEP */
	    IEEE80211_C_RSN;		/* WPA/RSN */

	if (sc->rf_rev == RT2661_RF_5225 || sc->rf_rev == RT2661_RF_5325) {
		/* set supported .11a rates */
		ic->ic_sup_rates[IEEE80211_MODE_11A] =
		    ieee80211_std_rateset_11a;

		/* set supported .11a channels */
		for (i = 36; i <= 64; i += 4) {
			ic->ic_channels[i].ic_freq =
			    ieee80211_ieee2mhz(i, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[i].ic_flags = IEEE80211_CHAN_A;
		}
		for (i = 100; i <= 140; i += 4) {
			ic->ic_channels[i].ic_freq =
			    ieee80211_ieee2mhz(i, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[i].ic_flags = IEEE80211_CHAN_A;
		}
		for (i = 149; i <= 165; i += 4) {
			ic->ic_channels[i].ic_freq =
			    ieee80211_ieee2mhz(i, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[i].ic_flags = IEEE80211_CHAN_A;
		}
	}

	/* set supported .11b and .11g rates */
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	/* set supported .11b and .11g channels (1 through 14) */
	for (i = 1; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags =
		    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
	}

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = rt2661_ioctl;
	ifp->if_start = rt2661_start;
	ifp->if_watchdog = rt2661_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_node_alloc = rt2661_node_alloc;
	sc->sc_node_free = ic->ic_node_free;
	ic->ic_node_free = rt2661_node_free;
	ic->ic_newassoc = rt2661_newassoc;
	ic->ic_updateslot = rt2661_updateslot;

	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = rt2661_newstate;
	ieee80211_media_init(ifp, rt2661_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + 64);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(RT2661_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(RT2661_TX_RADIOTAP_PRESENT);
#endif
}

int
rt2661_detach(void *xsc)
{
	struct rt2661_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int ac;

	timeout_del(&sc->scan_to);
	timeout_del(&sc->amrr_to);

	ieee80211_ifdetach(ifp);	/* free all nodes */
	rt2661_amrr_node_free_all(sc);
	if_detach(ifp);

	for (ac = 0; ac < 4; ac++)
		rt2661_free_tx_ring(sc, &sc->txq[ac]);
	rt2661_free_tx_ring(sc, &sc->mgtq);
	rt2661_free_rx_ring(sc, &sc->rxq);

	if (sc->ucode != NULL)
		free(sc->ucode, M_DEVBUF, sc->ucsize);

	return 0;
}

void
rt2661_suspend(void *xsc)
{
	struct rt2661_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (ifp->if_flags & IFF_RUNNING) {
		rt2661_stop(ifp, 1);
		sc->sc_flags &= ~RT2661_FWLOADED;
	}
}

void
rt2661_wakeup(void *xsc)
{
	struct rt2661_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (ifp->if_flags & IFF_UP)
		rt2661_init(ifp);	
}

int
rt2661_alloc_tx_ring(struct rt2661_softc *sc, struct rt2661_tx_ring *ring,
    int count)
{
	int i, nsegs, error;

	ring->count = count;
	ring->queued = 0;
	ring->cur = ring->next = ring->stat = 0;

	error = bus_dmamap_create(sc->sc_dmat, count * RT2661_TX_DESC_SIZE, 1,
	    count * RT2661_TX_DESC_SIZE, 0, BUS_DMA_NOWAIT, &ring->map);
	if (error != 0) {
		printf("%s: could not create desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, count * RT2661_TX_DESC_SIZE,
	    PAGE_SIZE, 0, &ring->seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs,
	    count * RT2661_TX_DESC_SIZE, (caddr_t *)&ring->desc,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, ring->desc,
	    count * RT2661_TX_DESC_SIZE, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ring->physaddr = ring->map->dm_segs->ds_addr;

	ring->data = mallocarray(count, sizeof (struct rt2661_tx_data),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ring->data == NULL) {
		printf("%s: could not allocate soft data\n",
		    sc->sc_dev.dv_xname);
		error = ENOMEM;
		goto fail;
	}

	for (i = 0; i < count; i++) {
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    RT2661_MAX_SCATTER, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &ring->data[i].map);
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}

	return 0;

fail:	rt2661_free_tx_ring(sc, ring);
	return error;
}

void
rt2661_reset_tx_ring(struct rt2661_softc *sc, struct rt2661_tx_ring *ring)
{
	int i;

	for (i = 0; i < ring->count; i++) {
		struct rt2661_tx_desc *desc = &ring->desc[i];
		struct rt2661_tx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}

		/*
		 * The node has already been freed at that point so don't call
		 * ieee80211_release_node() here.
		 */
		data->ni = NULL;

		desc->flags = 0;
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ring->queued = 0;
	ring->cur = ring->next = ring->stat = 0;
}

void
rt2661_free_tx_ring(struct rt2661_softc *sc, struct rt2661_tx_ring *ring)
{
	int i;

	if (ring->desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)ring->desc,
		    ring->count * RT2661_TX_DESC_SIZE);
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}

	if (ring->data != NULL) {
		for (i = 0; i < ring->count; i++) {
			struct rt2661_tx_data *data = &ring->data[i];

			if (data->m != NULL) {
				bus_dmamap_sync(sc->sc_dmat, data->map, 0,
				    data->map->dm_mapsize,
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->sc_dmat, data->map);
				m_freem(data->m);
			}
			/*
			 * The node has already been freed at that point so
			 * don't call ieee80211_release_node() here.
			 */
			data->ni = NULL;

			if (data->map != NULL)
				bus_dmamap_destroy(sc->sc_dmat, data->map);
		}
		free(ring->data, M_DEVBUF, ring->count * sizeof *ring->data);
	}
}

int
rt2661_alloc_rx_ring(struct rt2661_softc *sc, struct rt2661_rx_ring *ring,
    int count)
{
	int i, nsegs, error;

	ring->count = count;
	ring->cur = ring->next = 0;

	error = bus_dmamap_create(sc->sc_dmat, count * RT2661_RX_DESC_SIZE, 1,
	    count * RT2661_RX_DESC_SIZE, 0, BUS_DMA_NOWAIT, &ring->map);
	if (error != 0) {
		printf("%s: could not create desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, count * RT2661_RX_DESC_SIZE,
	    PAGE_SIZE, 0, &ring->seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs,
	    count * RT2661_RX_DESC_SIZE, (caddr_t *)&ring->desc,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, ring->desc,
	    count * RT2661_RX_DESC_SIZE, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ring->physaddr = ring->map->dm_segs->ds_addr;

	ring->data = mallocarray(count, sizeof (struct rt2661_rx_data),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ring->data == NULL) {
		printf("%s: could not allocate soft data\n",
		    sc->sc_dev.dv_xname);
		error = ENOMEM;
		goto fail;
	}

	/*
	 * Pre-allocate Rx buffers and populate Rx ring.
	 */
	for (i = 0; i < count; i++) {
		struct rt2661_rx_desc *desc = &sc->rxq.desc[i];
		struct rt2661_rx_data *data = &sc->rxq.data[i];

		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &data->map);
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		MGETHDR(data->m, M_DONTWAIT, MT_DATA);
		if (data->m == NULL) {
			printf("%s: could not allocate rx mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		MCLGET(data->m, M_DONTWAIT);
		if (!(data->m->m_flags & M_EXT)) {
			printf("%s: could not allocate rx mbuf cluster\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		error = bus_dmamap_load(sc->sc_dmat, data->map,
		    mtod(data->m, void *), MCLBYTES, NULL, BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: could not load rx buf DMA map",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		desc->flags = htole32(RT2661_RX_BUSY);
		desc->physaddr = htole32(data->map->dm_segs->ds_addr);
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	return 0;

fail:	rt2661_free_rx_ring(sc, ring);
	return error;
}

void
rt2661_reset_rx_ring(struct rt2661_softc *sc, struct rt2661_rx_ring *ring)
{
	int i;

	for (i = 0; i < ring->count; i++)
		ring->desc[i].flags = htole32(RT2661_RX_BUSY);

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ring->cur = ring->next = 0;
}

void
rt2661_free_rx_ring(struct rt2661_softc *sc, struct rt2661_rx_ring *ring)
{
	int i;

	if (ring->desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)ring->desc,
		    ring->count * RT2661_RX_DESC_SIZE);
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}

	if (ring->data != NULL) {
		for (i = 0; i < ring->count; i++) {
			struct rt2661_rx_data *data = &ring->data[i];

			if (data->m != NULL) {
				bus_dmamap_sync(sc->sc_dmat, data->map, 0,
				    data->map->dm_mapsize,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(sc->sc_dmat, data->map);
				m_freem(data->m);
			}

			if (data->map != NULL)
				bus_dmamap_destroy(sc->sc_dmat, data->map);
		}
		free(ring->data, M_DEVBUF, ring->count * sizeof *ring->data);
	}
}

struct rt2661_amrr_node *
rt2661_amrr_node_alloc(struct ieee80211com *ic, struct rt2661_node *rn)
{
	struct rt2661_softc *sc = ic->ic_softc;
	struct rt2661_amrr_node *amn;
	int s;

	if (sc->amn_count >= RT2661_AMRR_NODES_MAX)
		rt2661_amrr_node_free_unused(sc);
	if (sc->amn_count >= RT2661_AMRR_NODES_MAX)
		return NULL;

	amn = malloc(sizeof (struct rt2661_amrr_node), M_DEVBUF,
	    M_NOWAIT | M_ZERO);

	if (amn) {
		s = splnet();
		amn->id = sc->amn_count++;
		amn->rn = rn;
		TAILQ_INSERT_TAIL(&sc->amn, amn, entry);
		splx(s);
	}

	return amn;
}

void
rt2661_amrr_node_free(struct rt2661_softc *sc, struct rt2661_amrr_node *amn)
{
	int s;

	s = splnet();
	if (amn->rn)
		amn->rn->amn = NULL;
	TAILQ_REMOVE(&sc->amn, amn, entry);
	sc->amn_count--;
	splx(s);
	free(amn, M_DEVBUF, sizeof *amn);
}

void
rt2661_amrr_node_free_all(struct rt2661_softc *sc)
{
	struct rt2661_amrr_node *amn, *a;
	int s;

	s = splnet();
	TAILQ_FOREACH_SAFE(amn, &sc->amn, entry, a)
		rt2661_amrr_node_free(sc, amn);
	splx(s);
}

void
rt2661_amrr_node_free_unused(struct rt2661_softc *sc)
{
	struct rt2661_amrr_node *amn, *a;
	int s;

	s = splnet();
	TAILQ_FOREACH_SAFE(amn, &sc->amn, entry, a) {
		if (amn->rn == NULL)
			rt2661_amrr_node_free(sc, amn);
	}
	splx(s);
}

struct rt2661_amrr_node *
rt2661_amrr_node_find(struct rt2661_softc *sc, u_int8_t id)
{
	struct rt2661_amrr_node *amn, *a, *ret = NULL;
	int s;

	if (id == RT2661_AMRR_INVALID_ID)
		return NULL;

	s = splnet();
	TAILQ_FOREACH_SAFE(amn, &sc->amn, entry, a) {
		/* If the corresponding node was freed, free the amrr node. */
		if (amn->rn == NULL)
			rt2661_amrr_node_free(sc, amn);
		else if (amn->id == id)
			ret = amn;
	}
	splx(s);

	return ret;
}

struct ieee80211_node *
rt2661_node_alloc(struct ieee80211com *ic)
{
	struct rt2661_node *rn;

	rn = malloc(sizeof (struct rt2661_node), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (rn == NULL)
		return NULL;

	rn->amn = rt2661_amrr_node_alloc(ic, rn);
	return (struct ieee80211_node *)rn;
}

void
rt2661_node_free(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct rt2661_softc *sc = ic->ic_softc;
	struct rt2661_node *rn = (struct rt2661_node *)ni;

	if (rn->amn)
		rn->amn->rn = NULL;
	sc->sc_node_free(ic, ni);
}

int
rt2661_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		rt2661_init(ifp);

	return 0;
}

/*
 * This function is called periodically (every 200ms) during scanning to
 * switch from one channel to another.
 */
void
rt2661_next_scan(void *arg)
{
	struct rt2661_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	s = splnet();
	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);
	splx(s);
}

/*
 * This function is called for each neighbor node.
 */
void
rt2661_iter_func(void *arg, struct ieee80211_node *ni)
{
	struct rt2661_softc *sc = arg;
	struct rt2661_node *rn = (struct rt2661_node *)ni;

	if (rn->amn)
		ieee80211_amrr_choose(&sc->amrr, ni, &rn->amn->amn);
}

/*
 * This function is called periodically (every 500ms) in RUN state to update
 * various settings like rate control statistics or Rx sensitivity.
 */
void
rt2661_updatestats(void *arg)
{
	struct rt2661_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int s;

	s = splnet();
	if (ic->ic_opmode == IEEE80211_M_STA)
		rt2661_iter_func(sc, ic->ic_bss);
	else
		ieee80211_iterate_nodes(ic, rt2661_iter_func, arg);

	/* update rx sensitivity and free unused amrr nodes every 1 sec */
	if (++sc->ncalls & 1) {
		rt2661_rx_tune(sc);
		rt2661_amrr_node_free_unused(sc);
	}
	splx(s);

	timeout_add_msec(&sc->amrr_to, 500);
}

void
rt2661_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct rt2661_softc *sc = ic->ic_softc;
	struct rt2661_node *rn = (struct rt2661_node *)ni;
	int i;

	if (rn->amn)
		ieee80211_amrr_node_init(&sc->amrr, &rn->amn->amn);

	/* set rate to some reasonable initial value */
	for (i = ni->ni_rates.rs_nrates - 1;
	     i > 0 && (ni->ni_rates.rs_rates[i] & IEEE80211_RATE_VAL) > 72;
	     i--);
	ni->ni_txrate = i;
}

int
rt2661_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct rt2661_softc *sc = ic->ic_if.if_softc;
	enum ieee80211_state ostate;
	struct ieee80211_node *ni;
	uint32_t tmp;

	ostate = ic->ic_state;
	timeout_del(&sc->scan_to);
	timeout_del(&sc->amrr_to);

	switch (nstate) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* abort TSF synchronization */
			tmp = RAL_READ(sc, RT2661_TXRX_CSR9);
			RAL_WRITE(sc, RT2661_TXRX_CSR9, tmp & ~0x00ffffff);
		}
		break;

	case IEEE80211_S_SCAN:
		rt2661_set_chan(sc, ic->ic_bss->ni_chan);
		timeout_add_msec(&sc->scan_to, 200);
		break;

	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		rt2661_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_RUN:
		rt2661_set_chan(sc, ic->ic_bss->ni_chan);

		ni = ic->ic_bss;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			rt2661_set_slottime(sc);
			rt2661_enable_mrr(sc);
			rt2661_set_txpreamble(sc);
			rt2661_set_basicrates(sc);
			rt2661_set_bssid(sc, ni->ni_bssid);
		}

#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS)
			rt2661_prepare_beacon(sc);
#endif

		if (ic->ic_opmode == IEEE80211_M_STA) {
			/* fake a join to init the tx rate */
			rt2661_newassoc(ic, ni, 1);
		}

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			sc->ncalls = 0;
			sc->avg_rssi = -95;	/* reset EMA */
			timeout_add_msec(&sc->amrr_to, 500);
			rt2661_enable_tsf_sync(sc);
		}
		break;
	}

	return sc->sc_newstate(ic, nstate, arg);
}

/*
 * Read 16 bits at address 'addr' from the serial EEPROM (either 93C46 or
 * 93C66).
 */
uint16_t
rt2661_eeprom_read(struct rt2661_softc *sc, uint8_t addr)
{
	uint32_t tmp;
	uint16_t val;
	int n;

	/* clock C once before the first command */
	RT2661_EEPROM_CTL(sc, 0);

	RT2661_EEPROM_CTL(sc, RT2661_S);
	RT2661_EEPROM_CTL(sc, RT2661_S | RT2661_C);
	RT2661_EEPROM_CTL(sc, RT2661_S);

	/* write start bit (1) */
	RT2661_EEPROM_CTL(sc, RT2661_S | RT2661_D);
	RT2661_EEPROM_CTL(sc, RT2661_S | RT2661_D | RT2661_C);

	/* write READ opcode (10) */
	RT2661_EEPROM_CTL(sc, RT2661_S | RT2661_D);
	RT2661_EEPROM_CTL(sc, RT2661_S | RT2661_D | RT2661_C);
	RT2661_EEPROM_CTL(sc, RT2661_S);
	RT2661_EEPROM_CTL(sc, RT2661_S | RT2661_C);

	/* write address (A5-A0 or A7-A0) */
	n = (RAL_READ(sc, RT2661_E2PROM_CSR) & RT2661_93C46) ? 5 : 7;
	for (; n >= 0; n--) {
		RT2661_EEPROM_CTL(sc, RT2661_S |
		    (((addr >> n) & 1) << RT2661_SHIFT_D));
		RT2661_EEPROM_CTL(sc, RT2661_S |
		    (((addr >> n) & 1) << RT2661_SHIFT_D) | RT2661_C);
	}

	RT2661_EEPROM_CTL(sc, RT2661_S);

	/* read data Q15-Q0 */
	val = 0;
	for (n = 15; n >= 0; n--) {
		RT2661_EEPROM_CTL(sc, RT2661_S | RT2661_C);
		tmp = RAL_READ(sc, RT2661_E2PROM_CSR);
		val |= ((tmp & RT2661_Q) >> RT2661_SHIFT_Q) << n;
		RT2661_EEPROM_CTL(sc, RT2661_S);
	}

	RT2661_EEPROM_CTL(sc, 0);

	/* clear Chip Select and clock C */
	RT2661_EEPROM_CTL(sc, RT2661_S);
	RT2661_EEPROM_CTL(sc, 0);
	RT2661_EEPROM_CTL(sc, RT2661_C);

	return val;
}

/* The TX interrupt handler accumulates statistics based on whether frames
 * were sent successfully by the ASIC. */
void
rt2661_tx_intr(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct rt2661_amrr_node *amn;
	int retrycnt;
	u_int8_t amrr_id;

	for (;;) {
		const uint32_t val = RAL_READ(sc, RT2661_STA_CSR4);
		if (!(val & RT2661_TX_STAT_VALID))
			break;

		/* retrieve rate control algorithm context */
		amrr_id = RT2661_TX_PRIV_DATA(val);
		amn = rt2661_amrr_node_find(sc, amrr_id);

		switch (RT2661_TX_RESULT(val)) {
		case RT2661_TX_SUCCESS:
			retrycnt = RT2661_TX_RETRYCNT(val);

			DPRINTFN(10, ("data frame sent successfully after "
			    "%d retries\n", retrycnt));
			if (amn) {
				amn->amn.amn_txcnt++;
				if (retrycnt > 0)
					amn->amn.amn_retrycnt++;
			}
			break;

		case RT2661_TX_RETRY_FAIL:
			DPRINTFN(9, ("sending data frame failed (too much "
			    "retries)\n"));
			if (amn) {
				amn->amn.amn_txcnt++;
				amn->amn.amn_retrycnt++;
			}
			ifp->if_oerrors++;
			break;

		default:
			/* other failure */
			printf("%s: sending data frame failed 0x%08x\n",
			    sc->sc_dev.dv_xname, val);
			ifp->if_oerrors++;
		}

		DPRINTFN(15, ("tx done amrr_id=%hhu amn=0x%x\n", amrr_id, amn));
	}
}

/* The TX DMA interrupt handler processes frames which have been offloaded
 * to the ASIC for transmission. We can free all resources corresponding
 * to the frame here. */
void
rt2661_tx_dma_intr(struct rt2661_softc *sc, struct rt2661_tx_ring *txq)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	for (;;) {
		struct rt2661_tx_desc *desc = &txq->desc[txq->next];
		struct rt2661_tx_data *data = &txq->data[txq->next];

		bus_dmamap_sync(sc->sc_dmat, txq->map,
		    txq->next * RT2661_TX_DESC_SIZE, RT2661_TX_DESC_SIZE,
		    BUS_DMASYNC_POSTREAD);

		if ((letoh32(desc->flags) & RT2661_TX_BUSY) ||
		    !(letoh32(desc->flags) & RT2661_TX_VALID))
			break;

		/* descriptor is no longer valid */
		desc->flags &= ~htole32(RT2661_TX_VALID);

		bus_dmamap_sync(sc->sc_dmat, txq->map,
		    txq->next * RT2661_TX_DESC_SIZE, RT2661_TX_DESC_SIZE,
		    BUS_DMASYNC_PREWRITE);

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, data->map);
		m_freem(data->m);
		data->m = NULL;
		ieee80211_release_node(ic, data->ni);
		data->ni = NULL;

		DPRINTFN(15, ("tx dma done q=%p idx=%u\n", txq, txq->next));

		txq->queued--;
		if (++txq->next >= txq->count)	/* faster than % count */
			txq->next = 0;
	}

	if (sc->mgtq.queued == 0 && sc->txq[0].queued == 0)
		sc->sc_tx_timer = 0;
	if (sc->mgtq.queued < RT2661_MGT_RING_COUNT &&
	    sc->txq[0].queued < RT2661_TX_RING_COUNT - 1) {
		if (sc->mgtq.queued < RT2661_MGT_RING_COUNT)
			sc->sc_flags &= ~RT2661_MGT_OACTIVE;
		if (sc->txq[0].queued < RT2661_TX_RING_COUNT - 1)
			sc->sc_flags &= ~RT2661_DATA_OACTIVE;
		if (!(sc->sc_flags & (RT2661_MGT_OACTIVE|RT2661_DATA_OACTIVE)))
			ifq_clr_oactive(&ifp->if_snd);
		rt2661_start(ifp);
	}
}

void
rt2661_rx_intr(struct rt2661_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct mbuf *mnew, *m;
	int error, rssi;

	for (;;) {
		struct rt2661_rx_desc *desc = &sc->rxq.desc[sc->rxq.cur];
		struct rt2661_rx_data *data = &sc->rxq.data[sc->rxq.cur];

		bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
		    sc->rxq.cur * RT2661_RX_DESC_SIZE, RT2661_RX_DESC_SIZE,
		    BUS_DMASYNC_POSTREAD);

		if (letoh32(desc->flags) & RT2661_RX_BUSY)
			break;

		if ((letoh32(desc->flags) & RT2661_RX_PHY_ERROR) ||
		    (letoh32(desc->flags) & RT2661_RX_CRC_ERROR)) {
			/*
			 * This should not happen since we did not request
			 * to receive those frames when we filled TXRX_CSR0.
			 */
			DPRINTFN(5, ("PHY or CRC error flags 0x%08x\n",
			    letoh32(desc->flags)));
			ifp->if_ierrors++;
			goto skip;
		}

		if ((letoh32(desc->flags) & RT2661_RX_CIPHER_MASK) != 0) {
			ifp->if_ierrors++;
			goto skip;
		}

		/*
		 * Try to allocate a new mbuf for this ring element and load it
		 * before processing the current mbuf. If the ring element
		 * cannot be loaded, drop the received packet and reuse the old
		 * mbuf. In the unlikely case that the old mbuf can't be
		 * reloaded either, explicitly panic.
		 */
		MGETHDR(mnew, M_DONTWAIT, MT_DATA);
		if (mnew == NULL) {
			ifp->if_ierrors++;
			goto skip;
		}
		MCLGET(mnew, M_DONTWAIT);
		if (!(mnew->m_flags & M_EXT)) {
			m_freem(mnew);
			ifp->if_ierrors++;
			goto skip;
		}

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, data->map);

		error = bus_dmamap_load(sc->sc_dmat, data->map,
		    mtod(mnew, void *), MCLBYTES, NULL, BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(mnew);

			/* try to reload the old mbuf */
			error = bus_dmamap_load(sc->sc_dmat, data->map,
			    mtod(data->m, void *), MCLBYTES, NULL,
			    BUS_DMA_NOWAIT);
			if (error != 0) {
				/* very unlikely that it will fail... */
				panic("%s: could not load old rx mbuf",
				    sc->sc_dev.dv_xname);
			}
			/* physical address may have changed */
			desc->physaddr = htole32(data->map->dm_segs->ds_addr);
			ifp->if_ierrors++;
			goto skip;
		}

		/*
	 	 * New mbuf successfully loaded, update Rx ring and continue
		 * processing.
		 */
		m = data->m;
		data->m = mnew;
		desc->physaddr = htole32(data->map->dm_segs->ds_addr);

		/* finalize mbuf */
		m->m_pkthdr.len = m->m_len =
		    (letoh32(desc->flags) >> 16) & 0xfff;

#if NBPFILTER > 0
		if (sc->sc_drvbpf != NULL) {
			struct rt2661_rx_radiotap_header *tap = &sc->sc_rxtap;
			uint32_t tsf_lo, tsf_hi;

			/* get timestamp (low and high 32 bits) */
			tsf_hi = RAL_READ(sc, RT2661_TXRX_CSR13);
			tsf_lo = RAL_READ(sc, RT2661_TXRX_CSR12);

			tap->wr_tsf =
			    htole64(((uint64_t)tsf_hi << 32) | tsf_lo);
			tap->wr_flags = 0;
			tap->wr_rate = rt2661_rxrate(desc);
			tap->wr_chan_freq = htole16(sc->sc_curchan->ic_freq);
			tap->wr_chan_flags = htole16(sc->sc_curchan->ic_flags);
			tap->wr_antsignal = desc->rssi;

			bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_rxtap_len, m,
			    BPF_DIRECTION_IN);
		}
#endif

		wh = mtod(m, struct ieee80211_frame *);
		ni = ieee80211_find_rxnode(ic, wh);

		/* send the frame to the 802.11 layer */
		memset(&rxi, 0, sizeof(rxi));
		rxi.rxi_rssi = desc->rssi;
		ieee80211_inputm(ifp, m, ni, &rxi, &ml);

		/*-
		 * Keep track of the average RSSI using an Exponential Moving
		 * Average (EMA) of 8 Wilder's days:
		 *     avg = (1 / N) x rssi + ((N - 1) / N) x avg
		 */
		rssi = rt2661_get_rssi(sc, desc->rssi);
		sc->avg_rssi = (rssi + 7 * sc->avg_rssi) / 8;

		/* node is no longer needed */
		ieee80211_release_node(ic, ni);

skip:		desc->flags |= htole32(RT2661_RX_BUSY);

		bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
		    sc->rxq.cur * RT2661_RX_DESC_SIZE, RT2661_RX_DESC_SIZE,
		    BUS_DMASYNC_PREWRITE);

		DPRINTFN(15, ("rx intr idx=%u\n", sc->rxq.cur));

		sc->rxq.cur = (sc->rxq.cur + 1) % RT2661_RX_RING_COUNT;
	}
	if_input(ifp, &ml);
}

#ifndef IEEE80211_STA_ONLY
/*
 * This function is called in HostAP or IBSS modes when it's time to send a
 * new beacon (every ni_intval milliseconds).
 */
void
rt2661_mcu_beacon_expire(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if (sc->sc_flags & RT2661_UPDATE_SLOT) {
		sc->sc_flags &= ~RT2661_UPDATE_SLOT;
		sc->sc_flags |= RT2661_SET_SLOTTIME;
	} else if (sc->sc_flags & RT2661_SET_SLOTTIME) {
		sc->sc_flags &= ~RT2661_SET_SLOTTIME;
		rt2661_set_slottime(sc);
	}

	if (ic->ic_curmode == IEEE80211_MODE_11G) {
		/* update ERP Information Element */
		RAL_WRITE_1(sc, sc->erp_csr, ic->ic_bss->ni_erp);
		RAL_RW_BARRIER_1(sc, sc->erp_csr);
	}

	DPRINTFN(15, ("beacon expired\n"));
}
#endif

void
rt2661_mcu_wakeup(struct rt2661_softc *sc)
{
	RAL_WRITE(sc, RT2661_MAC_CSR11, 5 << 16);

	RAL_WRITE(sc, RT2661_SOFT_RESET_CSR, 0x7);
	RAL_WRITE(sc, RT2661_IO_CNTL_CSR, 0x18);
	RAL_WRITE(sc, RT2661_PCI_USEC_CSR, 0x20);

	/* send wakeup command to MCU */
	rt2661_tx_cmd(sc, RT2661_MCU_CMD_WAKEUP, 0);
}

void
rt2661_mcu_cmd_intr(struct rt2661_softc *sc)
{
	RAL_READ(sc, RT2661_M2H_CMD_DONE_CSR);
	RAL_WRITE(sc, RT2661_M2H_CMD_DONE_CSR, 0xffffffff);
}

int
rt2661_intr(void *arg)
{
	struct rt2661_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t r1, r2;

	r1 = RAL_READ(sc, RT2661_INT_SOURCE_CSR);
	r2 = RAL_READ(sc, RT2661_MCU_INT_SOURCE_CSR);
	if (__predict_false(r1 == 0xffffffff && r2 == 0xffffffff))
		return 0;	/* device likely went away */
	if (r1 == 0 && r2 == 0)
		return 0;	/* not for us */

	/* disable MAC and MCU interrupts */
	RAL_WRITE(sc, RT2661_INT_MASK_CSR, 0xffffff7f);
	RAL_WRITE(sc, RT2661_MCU_INT_MASK_CSR, 0xffffffff);

	/* acknowledge interrupts */
	RAL_WRITE(sc, RT2661_INT_SOURCE_CSR, r1);
	RAL_WRITE(sc, RT2661_MCU_INT_SOURCE_CSR, r2);

	/* don't re-enable interrupts if we're shutting down */
	if (!(ifp->if_flags & IFF_RUNNING))
		return 0;

	if (r1 & RT2661_MGT_DONE)
		rt2661_tx_dma_intr(sc, &sc->mgtq);

	if (r1 & RT2661_RX_DONE)
		rt2661_rx_intr(sc);

	if (r1 & RT2661_TX0_DMA_DONE)
		rt2661_tx_dma_intr(sc, &sc->txq[0]);

	if (r1 & RT2661_TX1_DMA_DONE)
		rt2661_tx_dma_intr(sc, &sc->txq[1]);

	if (r1 & RT2661_TX2_DMA_DONE)
		rt2661_tx_dma_intr(sc, &sc->txq[2]);

	if (r1 & RT2661_TX3_DMA_DONE)
		rt2661_tx_dma_intr(sc, &sc->txq[3]);

	if (r1 & RT2661_TX_DONE)
		rt2661_tx_intr(sc);

	if (r2 & RT2661_MCU_CMD_DONE)
		rt2661_mcu_cmd_intr(sc);

#ifndef IEEE80211_STA_ONLY
	if (r2 & RT2661_MCU_BEACON_EXPIRE)
		rt2661_mcu_beacon_expire(sc);
#endif

	if (r2 & RT2661_MCU_WAKEUP)
		rt2661_mcu_wakeup(sc);

	/* re-enable MAC and MCU interrupts */
	RAL_WRITE(sc, RT2661_INT_MASK_CSR, 0x0000ff10);
	RAL_WRITE(sc, RT2661_MCU_INT_MASK_CSR, 0);

	return 1;
}

/* quickly determine if a given rate is CCK or OFDM */
#define RAL_RATE_IS_OFDM(rate) ((rate) >= 12 && (rate) != 22)

#define RAL_ACK_SIZE	14	/* 10 + 4(FCS) */
#define RAL_CTS_SIZE	14	/* 10 + 4(FCS) */

/*
 * This function is only used by the Rx radiotap code. It returns the rate at
 * which a given frame was received.
 */
#if NBPFILTER > 0
uint8_t
rt2661_rxrate(const struct rt2661_rx_desc *desc)
{
	if (letoh32(desc->flags) & RT2661_RX_OFDM) {
		/* reverse function of rt2661_plcp_signal */
		switch (desc->rate & 0xf) {
		case 0xb:	return 12;
		case 0xf:	return 18;
		case 0xa:	return 24;
		case 0xe:	return 36;
		case 0x9:	return 48;
		case 0xd:	return 72;
		case 0x8:	return 96;
		case 0xc:	return 108;
		}
	} else {
		if (desc->rate == 10)
			return 2;
		if (desc->rate == 20)
			return 4;
		if (desc->rate == 55)
			return 11;
		if (desc->rate == 110)
			return 22;
	}
	return 2;	/* should not get there */
}
#endif

/*
 * Return the expected ack rate for a frame transmitted at rate `rate'.
 */
int
rt2661_ack_rate(struct ieee80211com *ic, int rate)
{
	switch (rate) {
	/* CCK rates */
	case 2:
		return 2;
	case 4:
	case 11:
	case 22:
		return (ic->ic_curmode == IEEE80211_MODE_11B) ? 4 : rate;

	/* OFDM rates */
	case 12:
	case 18:
		return 12;
	case 24:
	case 36:
		return 24;
	case 48:
	case 72:
	case 96:
	case 108:
		return 48;
	}

	/* default to 1Mbps */
	return 2;
}

/*
 * Compute the duration (in us) needed to transmit `len' bytes at rate `rate'.
 * The function automatically determines the operating mode depending on the
 * given rate. `flags' indicates whether short preamble is in use or not.
 */
uint16_t
rt2661_txtime(int len, int rate, uint32_t flags)
{
	uint16_t txtime;

	if (RAL_RATE_IS_OFDM(rate)) {
		/* IEEE Std 802.11g-2003, pp. 44 */
		txtime = (8 + 4 * len + 3 + rate - 1) / rate;
		txtime = 16 + 4 + 4 * txtime + 6;
	} else {
		/* IEEE Std 802.11b-1999, pp. 28 */
		txtime = (16 * len + rate - 1) / rate;
		if (rate != 2 && (flags & IEEE80211_F_SHPREAMBLE))
			txtime +=  72 + 24;
		else
			txtime += 144 + 48;
	}
	return txtime;
}

uint8_t
rt2661_plcp_signal(int rate)
{
	switch (rate) {
	/* CCK rates (returned values are device-dependent) */
	case 2:		return 0x0;
	case 4:		return 0x1;
	case 11:	return 0x2;
	case 22:	return 0x3;

	/* OFDM rates (cf IEEE Std 802.11a-1999, pp. 14 Table 80) */
	case 12:	return 0xb;
	case 18:	return 0xf;
	case 24:	return 0xa;
	case 36:	return 0xe;
	case 48:	return 0x9;
	case 72:	return 0xd;
	case 96:	return 0x8;
	case 108:	return 0xc;

	/* unsupported rates (should not get there) */
	default:	return 0xff;
	}
}

void
rt2661_setup_tx_desc(struct rt2661_softc *sc, struct rt2661_tx_desc *desc,
    uint32_t flags, uint16_t xflags, int len, int rate,
    const bus_dma_segment_t *segs, int nsegs, int ac, u_int8_t amrr_id)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t plcp_length;
	int i, remainder;

	desc->flags = htole32(flags);
	desc->flags |= htole32(len << 16);
	desc->flags |= htole32(RT2661_TX_BUSY | RT2661_TX_VALID);

	desc->xflags = htole16(xflags);
	desc->xflags |= htole16(nsegs << 13);

	desc->wme = htole16(
	    RT2661_QID(ac) |
	    RT2661_AIFSN(2) |
	    RT2661_LOGCWMIN(4) |
	    RT2661_LOGCWMAX(10));

	/*
	 * Remember the ID of the AMRR node to update when Tx completes.
	 * This field is driver private data only. It will be made available
	 * by the NIC in STA_CSR4 on Tx interrupts.
	 */
	desc->priv_data = amrr_id;

	/* setup PLCP fields */
	desc->plcp_signal  = rt2661_plcp_signal(rate);
	desc->plcp_service = 4;

	len += IEEE80211_CRC_LEN;
	if (RAL_RATE_IS_OFDM(rate)) {
		desc->flags |= htole32(RT2661_TX_OFDM);

		plcp_length = len & 0xfff;
		desc->plcp_length_hi = plcp_length >> 6;
		desc->plcp_length_lo = plcp_length & 0x3f;
	} else {
		plcp_length = (16 * len + rate - 1) / rate;
		if (rate == 22) {
			remainder = (16 * len) % 22;
			if (remainder != 0 && remainder < 7)
				desc->plcp_service |= RT2661_PLCP_LENGEXT;
		}
		desc->plcp_length_hi = plcp_length >> 8;
		desc->plcp_length_lo = plcp_length & 0xff;

		if (rate != 2 && (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			desc->plcp_signal |= 0x08;
	}

	/* RT2x61 supports scatter with up to 5 segments */
	for (i = 0; i < nsegs; i++) {
		desc->addr[i] = htole32(segs[i].ds_addr);
		desc->len [i] = htole16(segs[i].ds_len);
	}
}

int
rt2661_tx_mgt(struct rt2661_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2661_tx_desc *desc;
	struct rt2661_tx_data *data;
	struct ieee80211_frame *wh;
	uint16_t dur;
	uint32_t flags = 0;
	int rate, error;

	desc = &sc->mgtq.desc[sc->mgtq.cur];
	data = &sc->mgtq.data[sc->mgtq.cur];

	/* send mgt frames at the lowest available rate */
	rate = IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ? 12 : 2;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m0,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		m_freem(m0);
		return error;
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct rt2661_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_chan_freq = htole16(sc->sc_curchan->ic_freq);
		tap->wt_chan_flags = htole16(sc->sc_curchan->ic_flags);

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len, m0,
		    BPF_DIRECTION_OUT);
	}
#endif

	data->m = m0;
	data->ni = ni;

	wh = mtod(m0, struct ieee80211_frame *);

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2661_TX_NEED_ACK;

		dur = rt2661_txtime(RAL_ACK_SIZE, rate, ic->ic_flags) +
		    sc->sifs;
		*(uint16_t *)wh->i_dur = htole16(dur);

#ifndef IEEE80211_STA_ONLY
		/* tell hardware to set timestamp in probe responses */
		if ((wh->i_fc[0] &
		    (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
		    (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP))
			flags |= RT2661_TX_TIMESTAMP;
#endif
	}

	rt2661_setup_tx_desc(sc, desc, flags, 0 /* XXX HWSEQ */,
	    m0->m_pkthdr.len, rate, data->map->dm_segs, data->map->dm_nsegs,
	    RT2661_QID_MGT, RT2661_AMRR_INVALID_ID);

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->mgtq.map,
	    sc->mgtq.cur * RT2661_TX_DESC_SIZE, RT2661_TX_DESC_SIZE,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(10, ("sending mgt frame len=%u idx=%u rate=%u\n",
	    m0->m_pkthdr.len, sc->mgtq.cur, rate));

	/* kick mgt */
	sc->mgtq.queued++;
	sc->mgtq.cur = (sc->mgtq.cur + 1) % RT2661_MGT_RING_COUNT;
	RAL_WRITE(sc, RT2661_TX_CNTL_CSR, RT2661_KICK_MGT);

	return 0;
}

int
rt2661_tx_data(struct rt2661_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni, int ac)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2661_tx_ring *txq = &sc->txq[ac];
	struct rt2661_node *rn;
	struct rt2661_tx_desc *desc;
	struct rt2661_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	struct mbuf *m1;
	uint16_t dur;
	uint32_t flags = 0;
	int pktlen, rate, needcts = 0, needrts = 0, error;

	rn = ((ni == ic->ic_bss) ? NULL : (struct rt2661_node *)ni);
	wh = mtod(m0, struct ieee80211_frame *);

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_get_txkey(ic, wh, ni);

		if ((m0 = ieee80211_encrypt(ic, m0, k)) == NULL)
			return ENOBUFS;

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	/* compute actual packet length (including CRC and crypto overhead) */
	pktlen = m0->m_pkthdr.len + IEEE80211_CRC_LEN;

	/* pickup a rate */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/* multicast frames are sent at the lowest avail. rate */
		rate = ni->ni_rates.rs_rates[0];
	} else if (ic->ic_fixed_rate != -1) {
		rate = ic->ic_sup_rates[ic->ic_curmode].
		    rs_rates[ic->ic_fixed_rate];
	} else
		rate = ni->ni_rates.rs_rates[ni->ni_txrate];
	if (rate == 0)
		rate = 2;	/* XXX should not happen */
	rate &= IEEE80211_RATE_VAL;

	/*
	 * Packet Bursting: backoff after ppb=8 frames to give other STAs a
	 * chance to contend for the wireless medium.
	 */
	if (ic->ic_opmode == IEEE80211_M_STA && (ni->ni_txseq & 7))
		flags |= RT2661_TX_IFS_SIFS;

	/* check if RTS/CTS or CTS-to-self protection must be used */
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/* multicast frames are not sent at OFDM rates in 802.11b/g */
		if (pktlen > ic->ic_rtsthreshold) {
			needrts = 1;	/* RTS/CTS based on frame length */
		} else if ((ic->ic_flags & IEEE80211_F_USEPROT) &&
		    RAL_RATE_IS_OFDM(rate)) {
			if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
				needcts = 1;	/* CTS-to-self */
			else if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
				needrts = 1;	/* RTS/CTS */
		}
	}
	if (needrts || needcts) {
		struct mbuf *mprot;
		int protrate, ackrate;

		protrate = IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ? 12 : 2;
		ackrate  = rt2661_ack_rate(ic, rate);

		dur = rt2661_txtime(pktlen, rate, ic->ic_flags) +
		      rt2661_txtime(RAL_ACK_SIZE, ackrate, ic->ic_flags) +
		      2 * sc->sifs;
		if (needrts) {
			dur += rt2661_txtime(RAL_CTS_SIZE, rt2661_ack_rate(ic,
			    protrate), ic->ic_flags) + sc->sifs;
			mprot = ieee80211_get_rts(ic, wh, dur);
		} else {
			mprot = ieee80211_get_cts_to_self(ic, dur);
		}
		if (mprot == NULL) {
			printf("%s: could not allocate protection frame\n",
			    sc->sc_dev.dv_xname);
			m_freem(m0);
			return ENOBUFS;
		}

		desc = &txq->desc[txq->cur];
		data = &txq->data[txq->cur];

		error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, mprot,
		    BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: can't map mbuf (error %d)\n",
			    sc->sc_dev.dv_xname, error);
			m_freem(mprot);
			m_freem(m0);
			return error;
		}

		data->m = mprot;
		/* avoid multiple free() of the same node for each fragment */
		data->ni = ieee80211_ref_node(ni);

		/* XXX may want to pass the protection frame to BPF */

		rt2661_setup_tx_desc(sc, desc,
		    (needrts ? RT2661_TX_NEED_ACK : 0) | RT2661_TX_MORE_FRAG,
		    0, mprot->m_pkthdr.len, protrate, data->map->dm_segs,
		    data->map->dm_nsegs, ac,
	    	    (rn && rn->amn) ? rn->amn->id : RT2661_AMRR_INVALID_ID);

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_PREWRITE);
		bus_dmamap_sync(sc->sc_dmat, txq->map,
		    txq->cur * RT2661_TX_DESC_SIZE, RT2661_TX_DESC_SIZE,
		    BUS_DMASYNC_PREWRITE);

		txq->queued++;
		txq->cur = (txq->cur + 1) % RT2661_TX_RING_COUNT;

		flags |= RT2661_TX_LONG_RETRY | RT2661_TX_IFS_SIFS;
	}

	data = &txq->data[txq->cur];
	desc = &txq->desc[txq->cur];

	error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m0,
	    BUS_DMA_NOWAIT);
	if (error != 0 && error != EFBIG) {
		printf("%s: can't map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		m_freem(m0);
		return error;
	}
	if (error != 0) {
		/* too many fragments, linearize */
		MGETHDR(m1, M_DONTWAIT, MT_DATA);
		if (m1 == NULL) {
			m_freem(m0);
			return ENOBUFS;
		}
		if (m0->m_pkthdr.len > MHLEN) {
			MCLGET(m1, M_DONTWAIT);
			if (!(m1->m_flags & M_EXT)) {
				m_freem(m0);
				m_freem(m1);
				return ENOBUFS;
			}
		}
		m_copydata(m0, 0, m0->m_pkthdr.len, mtod(m1, caddr_t));
		m1->m_pkthdr.len = m1->m_len = m0->m_pkthdr.len;
		m_freem(m0);
		m0 = m1;

		error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m0,
		    BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: can't map mbuf (error %d)\n",
			    sc->sc_dev.dv_xname, error);
			m_freem(m0);
			return error;
		}

		/* packet header have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct rt2661_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_chan_freq = htole16(sc->sc_curchan->ic_freq);
		tap->wt_chan_flags = htole16(sc->sc_curchan->ic_flags);

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len, m0,
		    BPF_DIRECTION_OUT);
	}
#endif

	data->m = m0;
	data->ni = ni;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2661_TX_NEED_ACK;

		dur = rt2661_txtime(RAL_ACK_SIZE, rt2661_ack_rate(ic, rate),
		    ic->ic_flags) + sc->sifs;
		*(uint16_t *)wh->i_dur = htole16(dur);
	}

	rt2661_setup_tx_desc(sc, desc, flags, 0, m0->m_pkthdr.len, rate,
	    data->map->dm_segs, data->map->dm_nsegs, ac,
	    (rn && rn->amn) ? rn->amn->id : RT2661_AMRR_INVALID_ID);

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, txq->map, txq->cur * RT2661_TX_DESC_SIZE,
	    RT2661_TX_DESC_SIZE, BUS_DMASYNC_PREWRITE);

	DPRINTFN(10, ("sending data frame len=%u idx=%u rate=%u\n",
	    m0->m_pkthdr.len, txq->cur, rate));

	/* kick Tx */
	txq->queued++;
	txq->cur = (txq->cur + 1) % RT2661_TX_RING_COUNT;
	RAL_WRITE(sc, RT2661_TX_CNTL_CSR, 1);

	return 0;
}

void
rt2661_start(struct ifnet *ifp)
{
	struct rt2661_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct mbuf *m0;
	struct ieee80211_node *ni;

	/*
	 * net80211 may still try to send management frames even if the
	 * IFF_RUNNING flag is not set...
	 */
	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (mq_len(&ic->ic_mgtq) > 0) {
			if (sc->mgtq.queued >= RT2661_MGT_RING_COUNT) {
				ifq_set_oactive(&ifp->if_snd);
				break;
			}

			m0 = mq_dequeue(&ic->ic_mgtq);
			if (m0 == NULL)
				continue;
			ni = m0->m_pkthdr.ph_cookie;
#if NBPFILTER > 0
			if (ic->ic_rawbpf != NULL)
				bpf_mtap(ic->ic_rawbpf, m0, BPF_DIRECTION_OUT);
#endif
			if (rt2661_tx_mgt(sc, m0, ni) != 0)
				break;

		} else {
			if (sc->txq[0].queued >= RT2661_TX_RING_COUNT - 1) {
				ifq_set_oactive(&ifp->if_snd);
				break;
			}

			if (ic->ic_state != IEEE80211_S_RUN)
				break;

			m0 = ifq_dequeue(&ifp->if_snd);
			if (m0 == NULL)
				break;
#if NBPFILTER > 0
			if (ifp->if_bpf != NULL)
				bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif
			m0 = ieee80211_encap(ifp, m0, &ni);
			if (m0 == NULL)
				continue;
#if NBPFILTER > 0
			if (ic->ic_rawbpf != NULL)
				bpf_mtap(ic->ic_rawbpf, m0,
				    BPF_DIRECTION_OUT);
#endif
			if (rt2661_tx_data(sc, m0, ni, 0) != 0) {
				if (ni != NULL)
					ieee80211_release_node(ic, ni);
				ifp->if_oerrors++;
				break;
			}
		}

		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

void
rt2661_watchdog(struct ifnet *ifp)
{
	struct rt2661_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			rt2661_init(ifp);
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
rt2661_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct rt2661_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				rt2661_update_promisc(sc);
			else
				rt2661_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				rt2661_stop(ifp, 1);
		}
		break;

	case SIOCS80211CHANNEL:
		/*
		 * This allows for fast channel switching in monitor mode
		 * (used by kismet). In IBSS mode, we must explicitly reset
		 * the interface to generate a new beacon frame.
		 */
		error = ieee80211_ioctl(ifp, cmd, data);
		if (error == ENETRESET &&
		    ic->ic_opmode == IEEE80211_M_MONITOR) {
			if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
			    (IFF_UP | IFF_RUNNING))
				rt2661_set_chan(sc, ic->ic_ibss_chan);
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			rt2661_init(ifp);
		error = 0;
	}

	splx(s);

	return error;
}

void
rt2661_bbp_write(struct rt2661_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2661_PHY_CSR3) & RT2661_BBP_BUSY))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not write to BBP\n", sc->sc_dev.dv_xname);
		return;
	}

	tmp = RT2661_BBP_BUSY | (reg & 0x7f) << 8 | val;
	RAL_WRITE(sc, RT2661_PHY_CSR3, tmp);

	DPRINTFN(15, ("BBP R%u <- 0x%02x\n", reg, val));
}

uint8_t
rt2661_bbp_read(struct rt2661_softc *sc, uint8_t reg)
{
	uint32_t val;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2661_PHY_CSR3) & RT2661_BBP_BUSY))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not read from BBP\n", sc->sc_dev.dv_xname);
		return 0;
	}

	val = RT2661_BBP_BUSY | RT2661_BBP_READ | reg << 8;
	RAL_WRITE(sc, RT2661_PHY_CSR3, val);

	for (ntries = 0; ntries < 100; ntries++) {
		val = RAL_READ(sc, RT2661_PHY_CSR3);
		if (!(val & RT2661_BBP_BUSY))
			return val & 0xff;
		DELAY(1);
	}

	printf("%s: could not read from BBP\n", sc->sc_dev.dv_xname);
	return 0;
}

void
rt2661_rf_write(struct rt2661_softc *sc, uint8_t reg, uint32_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2661_PHY_CSR4) & RT2661_RF_BUSY))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not write to RF\n", sc->sc_dev.dv_xname);
		return;
	}

	tmp = RT2661_RF_BUSY | RT2661_RF_21BIT | (val & 0x1fffff) << 2 |
	    (reg & 3);
	RAL_WRITE(sc, RT2661_PHY_CSR4, tmp);

	/* remember last written value in sc */
	sc->rf_regs[reg] = val;

	DPRINTFN(15, ("RF R[%u] <- 0x%05x\n", reg & 3, val & 0x1fffff));
}

int
rt2661_tx_cmd(struct rt2661_softc *sc, uint8_t cmd, uint16_t arg)
{
	if (RAL_READ(sc, RT2661_H2M_MAILBOX_CSR) & RT2661_H2M_BUSY)
		return EIO;	/* there is already a command pending */

	RAL_WRITE(sc, RT2661_H2M_MAILBOX_CSR,
	    RT2661_H2M_BUSY | RT2661_TOKEN_NO_INTR << 16 | arg);

	RAL_WRITE(sc, RT2661_HOST_CMD_CSR, RT2661_KICK_CMD | cmd);

	return 0;
}

void
rt2661_select_antenna(struct rt2661_softc *sc)
{
	uint8_t bbp4, bbp77;
	uint32_t tmp;

	bbp4  = rt2661_bbp_read(sc,  4);
	bbp77 = rt2661_bbp_read(sc, 77);

	/* TBD */

	/* make sure Rx is disabled before switching antenna */
	tmp = RAL_READ(sc, RT2661_TXRX_CSR0);
	RAL_WRITE(sc, RT2661_TXRX_CSR0, tmp | RT2661_DISABLE_RX);

	rt2661_bbp_write(sc,  4, bbp4);
	rt2661_bbp_write(sc, 77, bbp77);

	/* restore Rx filter */
	RAL_WRITE(sc, RT2661_TXRX_CSR0, tmp);
}

/*
 * Enable multi-rate retries for frames sent at OFDM rates.
 * In 802.11b/g mode, allow fallback to CCK rates.
 */
void
rt2661_enable_mrr(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2661_TXRX_CSR4);

	tmp &= ~RT2661_MRR_CCK_FALLBACK;
	if (!IEEE80211_IS_CHAN_5GHZ(ic->ic_bss->ni_chan))
		tmp |= RT2661_MRR_CCK_FALLBACK;
	tmp |= RT2661_MRR_ENABLED;

	RAL_WRITE(sc, RT2661_TXRX_CSR4, tmp);
}

void
rt2661_set_txpreamble(struct rt2661_softc *sc)
{
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2661_TXRX_CSR4);

	tmp &= ~RT2661_SHORT_PREAMBLE;
	if (sc->sc_ic.ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= RT2661_SHORT_PREAMBLE;

	RAL_WRITE(sc, RT2661_TXRX_CSR4, tmp);
}

void
rt2661_set_basicrates(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* update basic rate set */
	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		/* 11b basic rates: 1, 2Mbps */
		RAL_WRITE(sc, RT2661_TXRX_CSR5, 0x3);
	} else if (ic->ic_curmode == IEEE80211_MODE_11A) {
		/* 11a basic rates: 6, 12, 24Mbps */
		RAL_WRITE(sc, RT2661_TXRX_CSR5, 0x150);
	} else {
		/* 11b/g basic rates: 1, 2, 5.5, 11Mbps */
		RAL_WRITE(sc, RT2661_TXRX_CSR5, 0xf);
	}
}

/*
 * Reprogram MAC/BBP to switch to a new band.  Values taken from the reference
 * driver.
 */
void
rt2661_select_band(struct rt2661_softc *sc, struct ieee80211_channel *c)
{
	uint8_t bbp17, bbp35, bbp96, bbp97, bbp98, bbp104;
	uint32_t tmp;

	/* update all BBP registers that depend on the band */
	bbp17 = 0x20; bbp96 = 0x48; bbp104 = 0x2c;
	bbp35 = 0x50; bbp97 = 0x48; bbp98  = 0x48;
	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		bbp17 += 0x08; bbp96 += 0x10; bbp104 += 0x0c;
		bbp35 += 0x10; bbp97 += 0x10; bbp98  += 0x10;
	}
	if ((IEEE80211_IS_CHAN_2GHZ(c) && sc->ext_2ghz_lna) ||
	    (IEEE80211_IS_CHAN_5GHZ(c) && sc->ext_5ghz_lna)) {
		bbp17 += 0x10; bbp96 += 0x10; bbp104 += 0x10;
	}

	sc->bbp17 = bbp17;
	rt2661_bbp_write(sc,  17, bbp17);
	rt2661_bbp_write(sc,  96, bbp96);
	rt2661_bbp_write(sc, 104, bbp104);

	if ((IEEE80211_IS_CHAN_2GHZ(c) && sc->ext_2ghz_lna) ||
	    (IEEE80211_IS_CHAN_5GHZ(c) && sc->ext_5ghz_lna)) {
		rt2661_bbp_write(sc, 75, 0x80);
		rt2661_bbp_write(sc, 86, 0x80);
		rt2661_bbp_write(sc, 88, 0x80);
	}

	rt2661_bbp_write(sc, 35, bbp35);
	rt2661_bbp_write(sc, 97, bbp97);
	rt2661_bbp_write(sc, 98, bbp98);

	tmp = RAL_READ(sc, RT2661_PHY_CSR0);
	tmp &= ~(RT2661_PA_PE_2GHZ | RT2661_PA_PE_5GHZ);
	if (IEEE80211_IS_CHAN_2GHZ(c))
		tmp |= RT2661_PA_PE_2GHZ;
	else
		tmp |= RT2661_PA_PE_5GHZ;
	RAL_WRITE(sc, RT2661_PHY_CSR0, tmp);

	/* 802.11a uses a 16 microseconds short interframe space */
	sc->sifs = IEEE80211_IS_CHAN_5GHZ(c) ? 16 : 10;
}

void
rt2661_set_chan(struct rt2661_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct rfprog *rfprog;
	uint8_t bbp3, bbp94 = RT2661_BBPR94_DEFAULT;
	int8_t power;
	u_int i, chan;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return;

	/* select the appropriate RF settings based on what EEPROM says */
	rfprog = (sc->rfprog == 0) ? rt2661_rf5225_1 : rt2661_rf5225_2;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rfprog[i].chan != chan; i++)
		;

	power = sc->txpow[i];
	if (power < 0) {
		bbp94 += power;
		power = 0;
	} else if (power > 31) {
		bbp94 += power - 31;
		power = 31;
	}

	/*
	 * If we are switching from the 2GHz band to the 5GHz band or
	 * vice-versa, BBP registers need to be reprogrammed.
	 */
	if (c->ic_flags != sc->sc_curchan->ic_flags) {
		rt2661_select_band(sc, c);
		rt2661_select_antenna(sc);
	}
	sc->sc_curchan = c;

	rt2661_rf_write(sc, RAL_RF1, rfprog[i].r1);
	rt2661_rf_write(sc, RAL_RF2, rfprog[i].r2);
	rt2661_rf_write(sc, RAL_RF3, rfprog[i].r3 | power << 7);
	rt2661_rf_write(sc, RAL_RF4, rfprog[i].r4 | sc->rffreq << 10);

	DELAY(200);

	rt2661_rf_write(sc, RAL_RF1, rfprog[i].r1);
	rt2661_rf_write(sc, RAL_RF2, rfprog[i].r2);
	rt2661_rf_write(sc, RAL_RF3, rfprog[i].r3 | power << 7 | 1);
	rt2661_rf_write(sc, RAL_RF4, rfprog[i].r4 | sc->rffreq << 10);

	DELAY(200);

	rt2661_rf_write(sc, RAL_RF1, rfprog[i].r1);
	rt2661_rf_write(sc, RAL_RF2, rfprog[i].r2);
	rt2661_rf_write(sc, RAL_RF3, rfprog[i].r3 | power << 7);
	rt2661_rf_write(sc, RAL_RF4, rfprog[i].r4 | sc->rffreq << 10);

	/* enable smart mode for MIMO-capable RFs */
	bbp3 = rt2661_bbp_read(sc, 3);

	bbp3 &= ~RT2661_SMART_MODE;
	if (sc->rf_rev == RT2661_RF_5325 || sc->rf_rev == RT2661_RF_2529)
		bbp3 |= RT2661_SMART_MODE;

	rt2661_bbp_write(sc, 3, bbp3);

	if (bbp94 != RT2661_BBPR94_DEFAULT)
		rt2661_bbp_write(sc, 94, bbp94);

	/* 5GHz radio needs a 1ms delay here */
	if (IEEE80211_IS_CHAN_5GHZ(c))
		DELAY(1000);
}

void
rt2661_set_bssid(struct rt2661_softc *sc, const uint8_t *bssid)
{
	uint32_t tmp;

	tmp = bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24;
	RAL_WRITE(sc, RT2661_MAC_CSR4, tmp);

	tmp = bssid[4] | bssid[5] << 8 | RT2661_ONE_BSSID << 16;
	RAL_WRITE(sc, RT2661_MAC_CSR5, tmp);
}

void
rt2661_set_macaddr(struct rt2661_softc *sc, const uint8_t *addr)
{
	uint32_t tmp;

	tmp = addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24;
	RAL_WRITE(sc, RT2661_MAC_CSR2, tmp);

	tmp = addr[4] | addr[5] << 8 | 0xff << 16;
	RAL_WRITE(sc, RT2661_MAC_CSR3, tmp);
}

void
rt2661_update_promisc(struct rt2661_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2661_TXRX_CSR0);

	tmp &= ~RT2661_DROP_NOT_TO_ME;
	if (!(ifp->if_flags & IFF_PROMISC))
		tmp |= RT2661_DROP_NOT_TO_ME;

	RAL_WRITE(sc, RT2661_TXRX_CSR0, tmp);

	DPRINTF(("%s promiscuous mode\n", (ifp->if_flags & IFF_PROMISC) ?
	    "entering" : "leaving"));
}

void
rt2661_updateslot(struct ieee80211com *ic)
{
	struct rt2661_softc *sc = ic->ic_if.if_softc;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
		/*
		 * In HostAP mode, we defer setting of new slot time until
		 * updated ERP Information Element has propagated to all
		 * associated STAs.
		 */
		sc->sc_flags |= RT2661_UPDATE_SLOT;
	} else
#endif
		rt2661_set_slottime(sc);
}

void
rt2661_set_slottime(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t slottime;
	uint32_t tmp;

	slottime = (ic->ic_flags & IEEE80211_F_SHSLOT) ?
	    IEEE80211_DUR_DS_SHSLOT: IEEE80211_DUR_DS_SLOT;

	tmp = RAL_READ(sc, RT2661_MAC_CSR9);
	tmp = (tmp & ~0xff) | slottime;
	RAL_WRITE(sc, RT2661_MAC_CSR9, tmp);

	DPRINTF(("setting slot time to %uus\n", slottime));
}

const char *
rt2661_get_rf(int rev)
{
	switch (rev) {
	case RT2661_RF_5225:	return "RT5225";
	case RT2661_RF_5325:	return "RT5325 (MIMO XR)";
	case RT2661_RF_2527:	return "RT2527";
	case RT2661_RF_2529:	return "RT2529 (MIMO XR)";
	default:		return "unknown";
	}
}

void
rt2661_read_eeprom(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t val;
	int i;

	/* read MAC address */
	val = rt2661_eeprom_read(sc, RT2661_EEPROM_MAC01);
	ic->ic_myaddr[0] = val & 0xff;
	ic->ic_myaddr[1] = val >> 8;

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_MAC23);
	ic->ic_myaddr[2] = val & 0xff;
	ic->ic_myaddr[3] = val >> 8;

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_MAC45);
	ic->ic_myaddr[4] = val & 0xff;
	ic->ic_myaddr[5] = val >> 8;

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_ANTENNA);
	/* XXX: test if different from 0xffff? */
	sc->rf_rev   = (val >> 11) & 0x1f;
	sc->hw_radio = (val >> 10) & 0x1;
	sc->rx_ant   = (val >> 4)  & 0x3;
	sc->tx_ant   = (val >> 2)  & 0x3;
	sc->nb_ant   = val & 0x3;

	DPRINTF(("RF revision=%d\n", sc->rf_rev));

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_CONFIG2);
	sc->ext_5ghz_lna = (val >> 6) & 0x1;
	sc->ext_2ghz_lna = (val >> 4) & 0x1;

	DPRINTF(("External 2GHz LNA=%d\nExternal 5GHz LNA=%d\n",
	    sc->ext_2ghz_lna, sc->ext_5ghz_lna));

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_RSSI_2GHZ_OFFSET);
	if ((val & 0xff) != 0xff)
		sc->rssi_2ghz_corr = (int8_t)(val & 0xff);	/* signed */

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_RSSI_5GHZ_OFFSET);
	if ((val & 0xff) != 0xff)
		sc->rssi_5ghz_corr = (int8_t)(val & 0xff);	/* signed */

	/* adjust RSSI correction for external low-noise amplifier */
	if (sc->ext_2ghz_lna)
		sc->rssi_2ghz_corr -= 14;
	if (sc->ext_5ghz_lna)
		sc->rssi_5ghz_corr -= 14;

	DPRINTF(("RSSI 2GHz corr=%d\nRSSI 5GHz corr=%d\n",
	    sc->rssi_2ghz_corr, sc->rssi_5ghz_corr));

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_FREQ_OFFSET);
	if ((val >> 8) != 0xff)
		sc->rfprog = (val >> 8) & 0x3;
	if ((val & 0xff) != 0xff)
		sc->rffreq = val & 0xff;

	DPRINTF(("RF prog=%d\nRF freq=%d\n", sc->rfprog, sc->rffreq));

	/* read Tx power for all a/b/g channels */
	for (i = 0; i < 19; i++) {
		val = rt2661_eeprom_read(sc, RT2661_EEPROM_TXPOWER + i);
		sc->txpow[i * 2] = (int8_t)(val >> 8);		/* signed */
		DPRINTF(("Channel=%d Tx power=%d\n",
		    rt2661_rf5225_1[i * 2].chan, sc->txpow[i * 2]));
		sc->txpow[i * 2 + 1] = (int8_t)(val & 0xff);	/* signed */
		DPRINTF(("Channel=%d Tx power=%d\n",
		    rt2661_rf5225_1[i * 2 + 1].chan, sc->txpow[i * 2 + 1]));
	}

	/* read vendor-specific BBP values */
	for (i = 0; i < 16; i++) {
		val = rt2661_eeprom_read(sc, RT2661_EEPROM_BBP_BASE + i);
		if (val == 0 || val == 0xffff)
			continue;	/* skip invalid entries */
		sc->bbp_prom[i].reg = val >> 8;
		sc->bbp_prom[i].val = val & 0xff;
		DPRINTF(("BBP R%d=%02x\n", sc->bbp_prom[i].reg,
		    sc->bbp_prom[i].val));
	}
}

int
rt2661_bbp_init(struct rt2661_softc *sc)
{
	int i, ntries;

	/* wait for BBP to be ready */
	for (ntries = 0; ntries < 100; ntries++) {
		const uint8_t val = rt2661_bbp_read(sc, 0);
		if (val != 0 && val != 0xff)
			break;
		DELAY(100);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for BBP\n", sc->sc_dev.dv_xname);
		return EIO;
	}

	/* initialize BBP registers to default values */
	for (i = 0; i < nitems(rt2661_def_bbp); i++) {
		rt2661_bbp_write(sc, rt2661_def_bbp[i].reg,
		    rt2661_def_bbp[i].val);
	}

	/* write vendor-specific BBP values (from EEPROM) */
	for (i = 0; i < 16; i++) {
		if (sc->bbp_prom[i].reg == 0)
			continue;
		rt2661_bbp_write(sc, sc->bbp_prom[i].reg, sc->bbp_prom[i].val);
	}

	return 0;
}

int
rt2661_init(struct ifnet *ifp)
{
	struct rt2661_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp, sta[3];
	int i, ntries;

	/* for CardBus, power on the socket */
	if (!(sc->sc_flags & RT2661_ENABLED)) {
		if (sc->sc_enable != NULL && (*sc->sc_enable)(sc) != 0) {
			printf("%s: could not enable device\n",
			    sc->sc_dev.dv_xname);
			return EIO;
		}
		sc->sc_flags |= RT2661_ENABLED;
	}

	rt2661_stop(ifp, 0);

	if (!(sc->sc_flags & RT2661_FWLOADED)) {
		if (rt2661_load_microcode(sc) != 0) {
			printf("%s: could not load 8051 microcode\n",
			    sc->sc_dev.dv_xname);
			rt2661_stop(ifp, 1);
			return EIO;
		}
		sc->sc_flags |= RT2661_FWLOADED;
	}

	/* initialize Tx rings */
	RAL_WRITE(sc, RT2661_AC1_BASE_CSR, sc->txq[1].physaddr);
	RAL_WRITE(sc, RT2661_AC0_BASE_CSR, sc->txq[0].physaddr);
	RAL_WRITE(sc, RT2661_AC2_BASE_CSR, sc->txq[2].physaddr);
	RAL_WRITE(sc, RT2661_AC3_BASE_CSR, sc->txq[3].physaddr);

	/* initialize Mgt ring */
	RAL_WRITE(sc, RT2661_MGT_BASE_CSR, sc->mgtq.physaddr);

	/* initialize Rx ring */
	RAL_WRITE(sc, RT2661_RX_BASE_CSR, sc->rxq.physaddr);

	/* initialize Tx rings sizes */
	RAL_WRITE(sc, RT2661_TX_RING_CSR0,
	    RT2661_TX_RING_COUNT << 24 |
	    RT2661_TX_RING_COUNT << 16 |
	    RT2661_TX_RING_COUNT <<  8 |
	    RT2661_TX_RING_COUNT);

	RAL_WRITE(sc, RT2661_TX_RING_CSR1,
	    RT2661_TX_DESC_WSIZE << 16 |
	    RT2661_TX_RING_COUNT <<  8 |	/* XXX: HCCA ring unused */
	    RT2661_MGT_RING_COUNT);

	/* initialize Rx rings */
	RAL_WRITE(sc, RT2661_RX_RING_CSR,
	    RT2661_RX_DESC_BACK  << 16 |
	    RT2661_RX_DESC_WSIZE <<  8 |
	    RT2661_RX_RING_COUNT);

	/* XXX: some magic here */
	RAL_WRITE(sc, RT2661_TX_DMA_DST_CSR, 0xaa);

	/* load base addresses of all 5 Tx rings (4 data + 1 mgt) */
	RAL_WRITE(sc, RT2661_LOAD_TX_RING_CSR, 0x1f);

	/* load base address of Rx ring */
	RAL_WRITE(sc, RT2661_RX_CNTL_CSR, 2);

	/* initialize MAC registers to default values */
	for (i = 0; i < nitems(rt2661_def_mac); i++)
		RAL_WRITE(sc, rt2661_def_mac[i].reg, rt2661_def_mac[i].val);

	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	rt2661_set_macaddr(sc, ic->ic_myaddr);

	/* set host ready */
	RAL_WRITE(sc, RT2661_MAC_CSR1, 3);
	RAL_WRITE(sc, RT2661_MAC_CSR1, 0);

	/* wait for BBP/RF to wakeup */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (RAL_READ(sc, RT2661_MAC_CSR12) & 8)
			break;
		DELAY(1000);
	}
	if (ntries == 1000) {
		printf("timeout waiting for BBP/RF to wakeup\n");
		rt2661_stop(ifp, 1);
		return EIO;
	}

	if (rt2661_bbp_init(sc) != 0) {
		rt2661_stop(ifp, 1);
		return EIO;
	}

	/* select default channel */
	sc->sc_curchan = ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	rt2661_select_band(sc, sc->sc_curchan);
	rt2661_select_antenna(sc);
	rt2661_set_chan(sc, sc->sc_curchan);

	/* update Rx filter */
	tmp = RAL_READ(sc, RT2661_TXRX_CSR0) & 0xffff;

	tmp |= RT2661_DROP_PHY_ERROR | RT2661_DROP_CRC_ERROR;
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RT2661_DROP_CTL | RT2661_DROP_VER_ERROR |
		       RT2661_DROP_ACKCTS;
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode != IEEE80211_M_HOSTAP)
#endif
			tmp |= RT2661_DROP_TODS;
		if (!(ifp->if_flags & IFF_PROMISC))
			tmp |= RT2661_DROP_NOT_TO_ME;
	}

	RAL_WRITE(sc, RT2661_TXRX_CSR0, tmp);

	/* clear STA registers */
	RAL_READ_REGION_4(sc, RT2661_STA_CSR0, sta, nitems(sta));

	/* initialize ASIC */
	RAL_WRITE(sc, RT2661_MAC_CSR1, 4);

	/* clear any pending interrupt */
	RAL_WRITE(sc, RT2661_INT_SOURCE_CSR, 0xffffffff);

	/* enable interrupts */
	RAL_WRITE(sc, RT2661_INT_MASK_CSR, 0x0000ff10);
	RAL_WRITE(sc, RT2661_MCU_INT_MASK_CSR, 0);

	/* kick Rx */
	RAL_WRITE(sc, RT2661_RX_CNTL_CSR, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);

	return 0;
}

void
rt2661_stop(struct ifnet *ifp, int disable)
{
	struct rt2661_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	int ac;

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);	/* free all nodes */
	rt2661_amrr_node_free_all(sc);

	/* abort Tx (for all 5 Tx rings) */
	RAL_WRITE(sc, RT2661_TX_CNTL_CSR, 0x1f << 16);

	/* disable Rx (value remains after reset!) */
	tmp = RAL_READ(sc, RT2661_TXRX_CSR0);
	RAL_WRITE(sc, RT2661_TXRX_CSR0, tmp | RT2661_DISABLE_RX);

	/* reset ASIC */
	RAL_WRITE(sc, RT2661_MAC_CSR1, 3);
	RAL_WRITE(sc, RT2661_MAC_CSR1, 0);

	/* disable interrupts */
	RAL_WRITE(sc, RT2661_INT_MASK_CSR, 0xffffff7f);
	RAL_WRITE(sc, RT2661_MCU_INT_MASK_CSR, 0xffffffff);

	/* clear any pending interrupt */
	RAL_WRITE(sc, RT2661_INT_SOURCE_CSR, 0xffffffff);
	RAL_WRITE(sc, RT2661_MCU_INT_SOURCE_CSR, 0xffffffff);

	/* reset Tx and Rx rings */
	for (ac = 0; ac < 4; ac++)
		rt2661_reset_tx_ring(sc, &sc->txq[ac]);
	rt2661_reset_tx_ring(sc, &sc->mgtq);
	rt2661_reset_rx_ring(sc, &sc->rxq);

	/* for CardBus, power down the socket */
	if (disable && sc->sc_disable != NULL) {
		if (sc->sc_flags & RT2661_ENABLED) {
			(*sc->sc_disable)(sc);
			sc->sc_flags &= ~(RT2661_ENABLED | RT2661_FWLOADED);
		}
	}
}

int
rt2661_load_microcode(struct rt2661_softc *sc)
{
	int ntries;

	/* reset 8051 */
	RAL_WRITE(sc, RT2661_MCU_CNTL_CSR, RT2661_MCU_RESET);

	/* cancel any pending Host to MCU command */
	RAL_WRITE(sc, RT2661_H2M_MAILBOX_CSR, 0);
	RAL_WRITE(sc, RT2661_M2H_CMD_DONE_CSR, 0xffffffff);
	RAL_WRITE(sc, RT2661_HOST_CMD_CSR, 0);

	/* write 8051's microcode */
	RAL_WRITE(sc, RT2661_MCU_CNTL_CSR, RT2661_MCU_RESET | RT2661_MCU_SEL);
	RAL_WRITE_REGION_1(sc, RT2661_MCU_CODE_BASE, sc->ucode, sc->ucsize);
	RAL_WRITE(sc, RT2661_MCU_CNTL_CSR, RT2661_MCU_RESET);

	/* kick 8051's ass */
	RAL_WRITE(sc, RT2661_MCU_CNTL_CSR, 0);

	/* wait for 8051 to initialize */
	for (ntries = 0; ntries < 500; ntries++) {
		if (RAL_READ(sc, RT2661_MCU_CNTL_CSR) & RT2661_MCU_READY)
			break;
		DELAY(100);
	}
	if (ntries == 500) {
		printf("%s: timeout waiting for MCU to initialize\n",
		    sc->sc_dev.dv_xname);
		return EIO;
	}
	return 0;
}

/*
 * Dynamically tune Rx sensitivity (BBP register 17) based on average RSSI and
 * false CCA count.  This function is called periodically (every seconds) when
 * in the RUN state.  Values taken from the reference driver.
 */
void
rt2661_rx_tune(struct rt2661_softc *sc)
{
	uint8_t bbp17;
	uint16_t cca;
	int lo, hi, dbm;

	/*
	 * Tuning range depends on operating band and on the presence of an
	 * external low-noise amplifier.
	 */
	lo = 0x20;
	if (IEEE80211_IS_CHAN_5GHZ(sc->sc_curchan))
		lo += 0x08;
	if ((IEEE80211_IS_CHAN_2GHZ(sc->sc_curchan) && sc->ext_2ghz_lna) ||
	    (IEEE80211_IS_CHAN_5GHZ(sc->sc_curchan) && sc->ext_5ghz_lna))
		lo += 0x10;
	hi = lo + 0x20;

	dbm = sc->avg_rssi;
	/* retrieve false CCA count since last call (clear on read) */
	cca = RAL_READ(sc, RT2661_STA_CSR1) & 0xffff;

	DPRINTFN(2, ("RSSI=%ddBm false CCA=%d\n", dbm, cca));

	if (dbm < -74) {
		/* very bad RSSI, tune using false CCA count */
		bbp17 = sc->bbp17; /* current value */

		hi -= 2 * (-74 - dbm);
		if (hi < lo)
			hi = lo;

		if (bbp17 > hi)
			bbp17 = hi;
		else if (cca > 512)
			bbp17 = min(bbp17 + 1, hi);
		else if (cca < 100)
			bbp17 = max(bbp17 - 1, lo);

	} else if (dbm < -66) {
		bbp17 = lo + 0x08;
	} else if (dbm < -58) {
		bbp17 = lo + 0x10;
	} else if (dbm < -35) {
		bbp17 = hi;
	} else {	/* very good RSSI >= -35dBm */
		bbp17 = 0x60;	/* very low sensitivity */
	}

	if (bbp17 != sc->bbp17) {
		DPRINTF(("BBP17 %x->%x\n", sc->bbp17, bbp17));
		rt2661_bbp_write(sc, 17, bbp17);
		sc->bbp17 = bbp17;
	}
}

#ifdef notyet
/*
 * Enter/Leave radar detection mode.
 * This is for 802.11h additional regulatory domains.
 */
void
rt2661_radar_start(struct rt2661_softc *sc)
{
	uint32_t tmp;

	/* disable Rx */
	tmp = RAL_READ(sc, RT2661_TXRX_CSR0);
	RAL_WRITE(sc, RT2661_TXRX_CSR0, tmp | RT2661_DISABLE_RX);

	rt2661_bbp_write(sc, 82, 0x20);
	rt2661_bbp_write(sc, 83, 0x00);
	rt2661_bbp_write(sc, 84, 0x40);

	/* save current BBP registers values */
	sc->bbp18 = rt2661_bbp_read(sc, 18);
	sc->bbp21 = rt2661_bbp_read(sc, 21);
	sc->bbp22 = rt2661_bbp_read(sc, 22);
	sc->bbp16 = rt2661_bbp_read(sc, 16);
	sc->bbp17 = rt2661_bbp_read(sc, 17);
	sc->bbp64 = rt2661_bbp_read(sc, 64);

	rt2661_bbp_write(sc, 18, 0xff);
	rt2661_bbp_write(sc, 21, 0x3f);
	rt2661_bbp_write(sc, 22, 0x3f);
	rt2661_bbp_write(sc, 16, 0xbd);
	rt2661_bbp_write(sc, 17, sc->ext_5ghz_lna ? 0x44 : 0x34);
	rt2661_bbp_write(sc, 64, 0x21);

	/* restore Rx filter */
	RAL_WRITE(sc, RT2661_TXRX_CSR0, tmp);
}

int
rt2661_radar_stop(struct rt2661_softc *sc)
{
	uint8_t bbp66;

	/* read radar detection result */
	bbp66 = rt2661_bbp_read(sc, 66);

	/* restore BBP registers values */
	rt2661_bbp_write(sc, 16, sc->bbp16);
	rt2661_bbp_write(sc, 17, sc->bbp17);
	rt2661_bbp_write(sc, 18, sc->bbp18);
	rt2661_bbp_write(sc, 21, sc->bbp21);
	rt2661_bbp_write(sc, 22, sc->bbp22);
	rt2661_bbp_write(sc, 64, sc->bbp64);

	return bbp66 == 1;
}
#endif

#ifndef IEEE80211_STA_ONLY
int
rt2661_prepare_beacon(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct rt2661_tx_desc desc;
	struct mbuf *m0;
	int rate;

	m0 = ieee80211_beacon_alloc(ic, ni);
	if (m0 == NULL) {
		printf("%s: could not allocate beacon frame\n",
		    sc->sc_dev.dv_xname);
		return ENOBUFS;
	}

	/* send beacons at the lowest available rate */
	rate = IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ? 12 : 2;

	memset(&desc, 0, sizeof(desc));
	rt2661_setup_tx_desc(sc, &desc, RT2661_TX_TIMESTAMP, RT2661_TX_HWSEQ,
	    m0->m_pkthdr.len, rate, NULL, 0, RT2661_QID_MGT,
	    RT2661_AMRR_INVALID_ID);

	/* copy the first 24 bytes of Tx descriptor into NIC memory */
	RAL_WRITE_REGION_1(sc, RT2661_HW_BEACON_BASE0, (uint8_t *)&desc, 24);

	/* copy beacon header and payload into NIC memory */
	RAL_WRITE_REGION_1(sc, RT2661_HW_BEACON_BASE0 + 24,
	    mtod(m0, uint8_t *), m0->m_pkthdr.len);

	m_freem(m0);

	/*
	 * Store offset of ERP Information Element so that we can update it
	 * dynamically when the slot time changes.
	 * XXX: this is ugly since it depends on how net80211 builds beacon
	 * frames but ieee80211_beacon_alloc() doesn't store offsets for us.
	 */
	if (ic->ic_curmode == IEEE80211_MODE_11G) {
		sc->erp_csr =
		    RT2661_HW_BEACON_BASE0 + 24 +
		    sizeof (struct ieee80211_frame) +
		    8 + 2 + 2 +
		    ((ic->ic_userflags & IEEE80211_F_HIDENWID) ?
			1 : 2 + ni->ni_esslen) +
		    2 + min(ni->ni_rates.rs_nrates, IEEE80211_RATE_SIZE) +
		    2 + 1 +
		    ((ic->ic_opmode == IEEE80211_M_IBSS) ? 4 : 6) +
		    2;
	}

	return 0;
}
#endif

/*
 * Enable TSF synchronization and tell h/w to start sending beacons for IBSS
 * and HostAP operating modes.
 */
void
rt2661_enable_tsf_sync(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode != IEEE80211_M_STA) {
		/*
		 * Change default 16ms TBTT adjustment to 8ms.
		 * Must be done before enabling beacon generation.
		 */
		RAL_WRITE(sc, RT2661_TXRX_CSR10, 1 << 12 | 8);
	}
#endif
	tmp = RAL_READ(sc, RT2661_TXRX_CSR9) & 0xff000000;

	/* set beacon interval (in 1/16ms unit) */
	tmp |= ic->ic_bss->ni_intval * 16;

	tmp |= RT2661_TSF_TICKING | RT2661_ENABLE_TBTT;
	if (ic->ic_opmode == IEEE80211_M_STA)
		tmp |= RT2661_TSF_MODE(1);
#ifndef IEEE80211_STA_ONLY
	else
		tmp |= RT2661_TSF_MODE(2) | RT2661_GENERATE_BEACON;
#endif
	RAL_WRITE(sc, RT2661_TXRX_CSR9, tmp);
}

/*
 * Retrieve the "Received Signal Strength Indicator" from the raw values
 * contained in Rx descriptors.  The computation depends on which band the
 * frame was received.  Correction values taken from the reference driver.
 */
int
rt2661_get_rssi(struct rt2661_softc *sc, uint8_t raw)
{
	int lna, agc, rssi;

	lna = (raw >> 5) & 0x3;
	agc = raw & 0x1f;

	rssi = 2 * agc;

	if (IEEE80211_IS_CHAN_2GHZ(sc->sc_curchan)) {
		rssi += sc->rssi_2ghz_corr;

		if (lna == 1)
			rssi -= 64;
		else if (lna == 2)
			rssi -= 74;
		else if (lna == 3)
			rssi -= 90;
	} else {
		rssi += sc->rssi_5ghz_corr;

		if (lna == 1)
			rssi -= 64;
		else if (lna == 2)
			rssi -= 86;
		else if (lna == 3)
			rssi -= 100;
	}
	return rssi;
}

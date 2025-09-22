/*	$OpenBSD: rt2560.c,v 1.90 2022/04/21 21:03:02 stsp Exp $  */

/*-
 * Copyright (c) 2005, 2006
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
 * Ralink Technology RT2560 chipset driver
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

#include <dev/ic/rt2560reg.h>
#include <dev/ic/rt2560var.h>

#ifdef RAL_DEBUG
#define DPRINTF(x)	do { if (rt2560_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (rt2560_debug >= (n)) printf x; } while (0)
int rt2560_debug = 1;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

int		rt2560_alloc_tx_ring(struct rt2560_softc *,
		    struct rt2560_tx_ring *, int);
void		rt2560_reset_tx_ring(struct rt2560_softc *,
		    struct rt2560_tx_ring *);
void		rt2560_free_tx_ring(struct rt2560_softc *,
		    struct rt2560_tx_ring *);
int		rt2560_alloc_rx_ring(struct rt2560_softc *,
		    struct rt2560_rx_ring *, int);
void		rt2560_reset_rx_ring(struct rt2560_softc *,
		    struct rt2560_rx_ring *);
void		rt2560_free_rx_ring(struct rt2560_softc *,
		    struct rt2560_rx_ring *);
struct		ieee80211_node *rt2560_node_alloc(struct ieee80211com *);
int		rt2560_media_change(struct ifnet *);
void		rt2560_next_scan(void *);
void		rt2560_iter_func(void *, struct ieee80211_node *);
void		rt2560_amrr_timeout(void *);
void		rt2560_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);
int		rt2560_newstate(struct ieee80211com *, enum ieee80211_state,
		    int);
uint16_t	rt2560_eeprom_read(struct rt2560_softc *, uint8_t);
void		rt2560_encryption_intr(struct rt2560_softc *);
void		rt2560_tx_intr(struct rt2560_softc *);
void		rt2560_prio_intr(struct rt2560_softc *);
void		rt2560_decryption_intr(struct rt2560_softc *);
void		rt2560_rx_intr(struct rt2560_softc *);
#ifndef IEEE80211_STA_ONLY
void		rt2560_beacon_expire(struct rt2560_softc *);
#endif
void		rt2560_wakeup_expire(struct rt2560_softc *);
#if NBPFILTER > 0
uint8_t		rt2560_rxrate(const struct rt2560_rx_desc *);
#endif
int		rt2560_ack_rate(struct ieee80211com *, int);
uint16_t	rt2560_txtime(int, int, uint32_t);
uint8_t		rt2560_plcp_signal(int);
void		rt2560_setup_tx_desc(struct rt2560_softc *,
		    struct rt2560_tx_desc *, uint32_t, int, int, int,
		    bus_addr_t);
#ifndef IEEE80211_STA_ONLY
int		rt2560_tx_bcn(struct rt2560_softc *, struct mbuf *,
		    struct ieee80211_node *);
#endif
int		rt2560_tx_mgt(struct rt2560_softc *, struct mbuf *,
		    struct ieee80211_node *);
int		rt2560_tx_data(struct rt2560_softc *, struct mbuf *,
		    struct ieee80211_node *);
void		rt2560_start(struct ifnet *);
void		rt2560_watchdog(struct ifnet *);
int		rt2560_ioctl(struct ifnet *, u_long, caddr_t);
void		rt2560_bbp_write(struct rt2560_softc *, uint8_t, uint8_t);
uint8_t		rt2560_bbp_read(struct rt2560_softc *, uint8_t);
void		rt2560_rf_write(struct rt2560_softc *, uint8_t, uint32_t);
void		rt2560_set_chan(struct rt2560_softc *,
		    struct ieee80211_channel *);
void		rt2560_disable_rf_tune(struct rt2560_softc *);
void		rt2560_enable_tsf_sync(struct rt2560_softc *);
void		rt2560_update_plcp(struct rt2560_softc *);
void		rt2560_updateslot(struct ieee80211com *);
void		rt2560_set_slottime(struct rt2560_softc *);
void		rt2560_set_basicrates(struct rt2560_softc *);
void		rt2560_update_led(struct rt2560_softc *, int, int);
void		rt2560_set_bssid(struct rt2560_softc *, uint8_t *);
void		rt2560_set_macaddr(struct rt2560_softc *, uint8_t *);
void		rt2560_get_macaddr(struct rt2560_softc *, uint8_t *);
void		rt2560_update_promisc(struct rt2560_softc *);
void		rt2560_set_txantenna(struct rt2560_softc *, int);
void		rt2560_set_rxantenna(struct rt2560_softc *, int);
const char	*rt2560_get_rf(int);
void		rt2560_read_eeprom(struct rt2560_softc *);
int		rt2560_bbp_init(struct rt2560_softc *);
int		rt2560_init(struct ifnet *);
void		rt2560_stop(struct ifnet *, int);

static const struct {
	uint32_t	reg;
	uint32_t	val;
} rt2560_def_mac[] = {
	RT2560_DEF_MAC
};

static const struct {
	uint8_t	reg;
	uint8_t	val;
} rt2560_def_bbp[] = {
	RT2560_DEF_BBP
};

static const uint32_t rt2560_rf2522_r2[]    = RT2560_RF2522_R2;
static const uint32_t rt2560_rf2523_r2[]    = RT2560_RF2523_R2;
static const uint32_t rt2560_rf2524_r2[]    = RT2560_RF2524_R2;
static const uint32_t rt2560_rf2525_r2[]    = RT2560_RF2525_R2;
static const uint32_t rt2560_rf2525_hi_r2[] = RT2560_RF2525_HI_R2;
static const uint32_t rt2560_rf2525e_r2[]   = RT2560_RF2525E_R2;
static const uint32_t rt2560_rf2526_r2[]    = RT2560_RF2526_R2;
static const uint32_t rt2560_rf2526_hi_r2[] = RT2560_RF2526_HI_R2;

int
rt2560_attach(void *xsc, int id)
{
	struct rt2560_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int error, i;

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 15;
	timeout_set(&sc->amrr_to, rt2560_amrr_timeout, sc);
	timeout_set(&sc->scan_to, rt2560_next_scan, sc);

	/* retrieve RT2560 rev. no */
	sc->asic_rev = RAL_READ(sc, RT2560_CSR0);

	/* retrieve MAC address */
	rt2560_get_macaddr(sc, ic->ic_myaddr);
	printf(", address %s\n", ether_sprintf(ic->ic_myaddr));

	/* retrieve RF rev. no and various other things from EEPROM */
	rt2560_read_eeprom(sc);

	printf("%s: MAC/BBP RT2560 (rev 0x%02x), RF %s\n", sc->sc_dev.dv_xname,
	    sc->asic_rev, rt2560_get_rf(sc->rf_rev));

	/*
	 * Allocate Tx and Rx rings.
	 */
	error = rt2560_alloc_tx_ring(sc, &sc->txq, RT2560_TX_RING_COUNT);
	if (error != 0) {
		printf("%s: could not allocate Tx ring\n",
		    sc->sc_dev.dv_xname);
		goto fail1;
	}
	error = rt2560_alloc_tx_ring(sc, &sc->atimq, RT2560_ATIM_RING_COUNT);
	if (error != 0) {
		printf("%s: could not allocate ATIM ring\n",
		    sc->sc_dev.dv_xname);
		goto fail2;
	}
	error = rt2560_alloc_tx_ring(sc, &sc->prioq, RT2560_PRIO_RING_COUNT);
	if (error != 0) {
		printf("%s: could not allocate Prio ring\n",
		    sc->sc_dev.dv_xname);
		goto fail3;
	}
	error = rt2560_alloc_tx_ring(sc, &sc->bcnq, RT2560_BEACON_RING_COUNT);
	if (error != 0) {
		printf("%s: could not allocate Beacon ring\n",
		    sc->sc_dev.dv_xname);
		goto fail4;
	}
	error = rt2560_alloc_rx_ring(sc, &sc->rxq, RT2560_RX_RING_COUNT);
	if (error != 0) {
		printf("%s: could not allocate Rx ring\n",
		    sc->sc_dev.dv_xname);
		goto fail5;
	}

	ic->ic_phytype = IEEE80211_T_OFDM; /* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA; /* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
#ifndef IEEE80211_STA_ONLY
	    IEEE80211_C_IBSS |		/* IBSS mode supported */
	    IEEE80211_C_HOSTAP |	/* HostAp mode supported */
#endif
	    IEEE80211_C_TXPMGT |	/* tx power management */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WEP |		/* s/w WEP */
	    IEEE80211_C_RSN;		/* WPA/RSN */

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
	ifp->if_ioctl = rt2560_ioctl;
	ifp->if_start = rt2560_start;
	ifp->if_watchdog = rt2560_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_node_alloc = rt2560_node_alloc;
	ic->ic_newassoc = rt2560_newassoc;
	ic->ic_updateslot = rt2560_updateslot;

	/* XXX RTS causes throughput problems -- where is the bug? */
	ic->ic_rtsthreshold = IEEE80211_RTS_MAX;

	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = rt2560_newstate;
	ieee80211_media_init(ifp, rt2560_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + 64);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(RT2560_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(RT2560_TX_RADIOTAP_PRESENT);
#endif
	return 0;

fail5:	rt2560_free_tx_ring(sc, &sc->bcnq);
fail4:	rt2560_free_tx_ring(sc, &sc->prioq);
fail3:	rt2560_free_tx_ring(sc, &sc->atimq);
fail2:	rt2560_free_tx_ring(sc, &sc->txq);
fail1:	return ENXIO;
}

int
rt2560_detach(void *xsc)
{
	struct rt2560_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	timeout_del(&sc->scan_to);
	timeout_del(&sc->amrr_to);

	ieee80211_ifdetach(ifp);	/* free all nodes */
	if_detach(ifp);

	rt2560_free_tx_ring(sc, &sc->txq);
	rt2560_free_tx_ring(sc, &sc->atimq);
	rt2560_free_tx_ring(sc, &sc->prioq);
	rt2560_free_tx_ring(sc, &sc->bcnq);
	rt2560_free_rx_ring(sc, &sc->rxq);

	return 0;
}

void
rt2560_suspend(void *xsc)
{
	struct rt2560_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (ifp->if_flags & IFF_RUNNING)
		rt2560_stop(ifp, 1);
}

void
rt2560_wakeup(void *xsc)
{
	struct rt2560_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (ifp->if_flags & IFF_UP)
		rt2560_init(ifp);
}

int
rt2560_alloc_tx_ring(struct rt2560_softc *sc, struct rt2560_tx_ring *ring,
    int count)
{
	int i, nsegs, error;

	ring->count = count;
	ring->queued = 0;
	ring->cur = ring->next = 0;
	ring->cur_encrypt = ring->next_encrypt = 0;

	error = bus_dmamap_create(sc->sc_dmat, count * RT2560_TX_DESC_SIZE, 1,
	    count * RT2560_TX_DESC_SIZE, 0, BUS_DMA_NOWAIT, &ring->map);
	if (error != 0) {
		printf("%s: could not create desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, count * RT2560_TX_DESC_SIZE,
	    PAGE_SIZE, 0, &ring->seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs,
	    count * RT2560_TX_DESC_SIZE, (caddr_t *)&ring->desc,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, ring->desc,
	    count * RT2560_TX_DESC_SIZE, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ring->physaddr = ring->map->dm_segs->ds_addr;

	ring->data = mallocarray(count, sizeof (struct rt2560_tx_data),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ring->data == NULL) {
		printf("%s: could not allocate soft data\n",
		    sc->sc_dev.dv_xname);
		error = ENOMEM;
		goto fail;
	}

	for (i = 0; i < count; i++) {
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    RT2560_MAX_SCATTER, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &ring->data[i].map);
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}

	return 0;

fail:	rt2560_free_tx_ring(sc, ring);
	return error;
}

void
rt2560_reset_tx_ring(struct rt2560_softc *sc, struct rt2560_tx_ring *ring)
{
	int i;

	for (i = 0; i < ring->count; i++) {
		struct rt2560_tx_desc *desc = &ring->desc[i];
		struct rt2560_tx_data *data = &ring->data[i];

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
	ring->cur = ring->next = 0;
	ring->cur_encrypt = ring->next_encrypt = 0;
}

void
rt2560_free_tx_ring(struct rt2560_softc *sc, struct rt2560_tx_ring *ring)
{
	int i;

	if (ring->desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)ring->desc,
		    ring->count * RT2560_TX_DESC_SIZE);
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}

	if (ring->data != NULL) {
		for (i = 0; i < ring->count; i++) {
			struct rt2560_tx_data *data = &ring->data[i];

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
rt2560_alloc_rx_ring(struct rt2560_softc *sc, struct rt2560_rx_ring *ring,
    int count)
{
	int i, nsegs, error;

	ring->count = count;
	ring->cur = ring->next = 0;
	ring->cur_decrypt = 0;

	error = bus_dmamap_create(sc->sc_dmat, count * RT2560_RX_DESC_SIZE, 1,
	    count * RT2560_RX_DESC_SIZE, 0, BUS_DMA_NOWAIT, &ring->map);
	if (error != 0) {
		printf("%s: could not create desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, count * RT2560_RX_DESC_SIZE,
	    PAGE_SIZE, 0, &ring->seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs,
	    count * RT2560_RX_DESC_SIZE, (caddr_t *)&ring->desc,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, ring->desc,
	    count * RT2560_RX_DESC_SIZE, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ring->physaddr = ring->map->dm_segs->ds_addr;

	ring->data = mallocarray(count, sizeof (struct rt2560_rx_data),
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
		struct rt2560_rx_desc *desc = &sc->rxq.desc[i];
		struct rt2560_rx_data *data = &sc->rxq.data[i];

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

		desc->flags = htole32(RT2560_RX_BUSY);
		desc->physaddr = htole32(data->map->dm_segs->ds_addr);
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	return 0;

fail:	rt2560_free_rx_ring(sc, ring);
	return error;
}

void
rt2560_reset_rx_ring(struct rt2560_softc *sc, struct rt2560_rx_ring *ring)
{
	int i;

	for (i = 0; i < ring->count; i++) {
		ring->desc[i].flags = htole32(RT2560_RX_BUSY);
		ring->data[i].drop = 0;
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ring->cur = ring->next = 0;
	ring->cur_decrypt = 0;
}

void
rt2560_free_rx_ring(struct rt2560_softc *sc, struct rt2560_rx_ring *ring)
{
	int i;

	if (ring->desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)ring->desc,
		    ring->count * RT2560_RX_DESC_SIZE);
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}

	if (ring->data != NULL) {
		for (i = 0; i < ring->count; i++) {
			struct rt2560_rx_data *data = &ring->data[i];

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

struct ieee80211_node *
rt2560_node_alloc(struct ieee80211com *ic)
{
	return malloc(sizeof (struct rt2560_node), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
}

int
rt2560_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		rt2560_init(ifp);

	return 0;
}

/*
 * This function is called periodically (every 200ms) during scanning to
 * switch from one channel to another.
 */
void
rt2560_next_scan(void *arg)
{
	struct rt2560_softc *sc = arg;
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
rt2560_iter_func(void *arg, struct ieee80211_node *ni)
{
	struct rt2560_softc *sc = arg;
	struct rt2560_node *rn = (struct rt2560_node *)ni;

	ieee80211_amrr_choose(&sc->amrr, ni, &rn->amn);
}

void
rt2560_amrr_timeout(void *arg)
{
	struct rt2560_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int s;

	s = splnet();
	if (ic->ic_opmode == IEEE80211_M_STA)
		rt2560_iter_func(sc, ic->ic_bss);
#ifndef IEEE80211_STA_ONLY
	else
		ieee80211_iterate_nodes(ic, rt2560_iter_func, sc);
#endif
	splx(s);

	timeout_add_msec(&sc->amrr_to, 500);
}

void
rt2560_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct rt2560_softc *sc = ic->ic_softc;
	int i;

	ieee80211_amrr_node_init(&sc->amrr, &((struct rt2560_node *)ni)->amn);

	/* set rate to some reasonable initial value */
	for (i = ni->ni_rates.rs_nrates - 1;
	     i > 0 && (ni->ni_rates.rs_rates[i] & IEEE80211_RATE_VAL) > 72;
	     i--);
	ni->ni_txrate = i;
}

int
rt2560_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct rt2560_softc *sc = ic->ic_if.if_softc;
	enum ieee80211_state ostate;
	struct ieee80211_node *ni;
	int error = 0;

	ostate = ic->ic_state;
	timeout_del(&sc->scan_to);
	timeout_del(&sc->amrr_to);

	switch (nstate) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* abort TSF synchronization */
			RAL_WRITE(sc, RT2560_CSR14, 0);

			/* turn association led off */
			rt2560_update_led(sc, 0, 0);
		}
		break;

	case IEEE80211_S_SCAN:
		rt2560_set_chan(sc, ic->ic_bss->ni_chan);
		timeout_add_msec(&sc->scan_to, 200);
		break;

	case IEEE80211_S_AUTH:
		rt2560_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_ASSOC:
		rt2560_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_RUN:
		rt2560_set_chan(sc, ic->ic_bss->ni_chan);

		ni = ic->ic_bss;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			rt2560_update_plcp(sc);
			rt2560_set_slottime(sc);
			rt2560_set_basicrates(sc);
			rt2560_set_bssid(sc, ni->ni_bssid);
		}

#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS) {
			struct mbuf *m = ieee80211_beacon_alloc(ic, ni);
			if (m == NULL) {
				printf("%s: could not allocate beacon\n",
				    sc->sc_dev.dv_xname);
				error = ENOBUFS;
				break;
			}

			error = rt2560_tx_bcn(sc, m, ni);
			if (error != 0)
				break;
		}
#endif

		/* turn association led on */
		rt2560_update_led(sc, 1, 0);

		if (ic->ic_opmode == IEEE80211_M_STA) {
			/* fake a join to init the tx rate */
			rt2560_newassoc(ic, ni, 1);
		}

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			/* start automatic rate control timer */
			if (ic->ic_fixed_rate == -1)
				timeout_add_msec(&sc->amrr_to, 500);

			rt2560_enable_tsf_sync(sc);
		}
		break;
	}

	return (error != 0) ? error : sc->sc_newstate(ic, nstate, arg);
}

/*
 * Read 16 bits at address 'addr' from the serial EEPROM (either 93C46 or
 * 93C66).
 */
uint16_t
rt2560_eeprom_read(struct rt2560_softc *sc, uint8_t addr)
{
	uint32_t tmp;
	uint16_t val;
	int n;

	/* clock C once before the first command */
	RT2560_EEPROM_CTL(sc, 0);

	RT2560_EEPROM_CTL(sc, RT2560_S);
	RT2560_EEPROM_CTL(sc, RT2560_S | RT2560_C);
	RT2560_EEPROM_CTL(sc, RT2560_S);

	/* write start bit (1) */
	RT2560_EEPROM_CTL(sc, RT2560_S | RT2560_D);
	RT2560_EEPROM_CTL(sc, RT2560_S | RT2560_D | RT2560_C);

	/* write READ opcode (10) */
	RT2560_EEPROM_CTL(sc, RT2560_S | RT2560_D);
	RT2560_EEPROM_CTL(sc, RT2560_S | RT2560_D | RT2560_C);
	RT2560_EEPROM_CTL(sc, RT2560_S);
	RT2560_EEPROM_CTL(sc, RT2560_S | RT2560_C);

	/* write address (A5-A0 or A7-A0) */
	n = (RAL_READ(sc, RT2560_CSR21) & RT2560_93C46) ? 5 : 7;
	for (; n >= 0; n--) {
		RT2560_EEPROM_CTL(sc, RT2560_S |
		    (((addr >> n) & 1) << RT2560_SHIFT_D));
		RT2560_EEPROM_CTL(sc, RT2560_S |
		    (((addr >> n) & 1) << RT2560_SHIFT_D) | RT2560_C);
	}

	RT2560_EEPROM_CTL(sc, RT2560_S);

	/* read data Q15-Q0 */
	val = 0;
	for (n = 15; n >= 0; n--) {
		RT2560_EEPROM_CTL(sc, RT2560_S | RT2560_C);
		tmp = RAL_READ(sc, RT2560_CSR21);
		val |= ((tmp & RT2560_Q) >> RT2560_SHIFT_Q) << n;
		RT2560_EEPROM_CTL(sc, RT2560_S);
	}

	RT2560_EEPROM_CTL(sc, 0);

	/* clear Chip Select and clock C */
	RT2560_EEPROM_CTL(sc, RT2560_S);
	RT2560_EEPROM_CTL(sc, 0);
	RT2560_EEPROM_CTL(sc, RT2560_C);

	return val;
}

/*
 * Some frames were processed by the hardware cipher engine and are ready for
 * transmission.
 */
void
rt2560_encryption_intr(struct rt2560_softc *sc)
{
	int hw;

	/* retrieve last descriptor index processed by cipher engine */
	hw = (RAL_READ(sc, RT2560_SECCSR1) - sc->txq.physaddr) /
	    RT2560_TX_DESC_SIZE;

	for (; sc->txq.next_encrypt != hw;) {
		struct rt2560_tx_desc *desc =
		    &sc->txq.desc[sc->txq.next_encrypt];

		bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
		    sc->txq.next_encrypt * RT2560_TX_DESC_SIZE,
		    RT2560_TX_DESC_SIZE, BUS_DMASYNC_POSTREAD);

		if (letoh32(desc->flags) &
		    (RT2560_TX_BUSY | RT2560_TX_CIPHER_BUSY))
			break;

		/* for TKIP, swap eiv field to fix a bug in ASIC */
		if ((letoh32(desc->flags) & RT2560_TX_CIPHER_MASK) ==
		    RT2560_TX_CIPHER_TKIP)
			desc->eiv = swap32(desc->eiv);

		/* mark the frame ready for transmission */
		desc->flags |= htole32(RT2560_TX_BUSY | RT2560_TX_VALID);

		bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
		    sc->txq.next_encrypt * RT2560_TX_DESC_SIZE,
		    RT2560_TX_DESC_SIZE, BUS_DMASYNC_PREWRITE);

		DPRINTFN(15, ("encryption done idx=%u\n",
		    sc->txq.next_encrypt));

		sc->txq.next_encrypt =
		    (sc->txq.next_encrypt + 1) % RT2560_TX_RING_COUNT;
	}

	/* kick Tx */
	RAL_WRITE(sc, RT2560_TXCSR0, RT2560_KICK_TX);
}

void
rt2560_tx_intr(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	for (;;) {
		struct rt2560_tx_desc *desc = &sc->txq.desc[sc->txq.next];
		struct rt2560_tx_data *data = &sc->txq.data[sc->txq.next];
		struct rt2560_node *rn;

		bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
		    sc->txq.next * RT2560_TX_DESC_SIZE, RT2560_TX_DESC_SIZE,
		    BUS_DMASYNC_POSTREAD);

		if ((letoh32(desc->flags) & RT2560_TX_BUSY) ||
		    (letoh32(desc->flags) & RT2560_TX_CIPHER_BUSY) ||
		    !(letoh32(desc->flags) & RT2560_TX_VALID))
			break;

		rn = (struct rt2560_node *)data->ni;

		switch (letoh32(desc->flags) & RT2560_TX_RESULT_MASK) {
		case RT2560_TX_SUCCESS:
			DPRINTFN(10, ("data frame sent successfully\n"));
			rn->amn.amn_txcnt++;
			break;

		case RT2560_TX_SUCCESS_RETRY:
			DPRINTFN(9, ("data frame sent after %u retries\n",
			    (letoh32(desc->flags) >> 5) & 0x7));
			rn->amn.amn_txcnt++;
			rn->amn.amn_retrycnt++;
			break;

		case RT2560_TX_FAIL_RETRY:
			DPRINTFN(9, ("sending data frame failed (too much "
			    "retries)\n"));
			rn->amn.amn_txcnt++;
			rn->amn.amn_retrycnt++;
			ifp->if_oerrors++;
			break;

		case RT2560_TX_FAIL_INVALID:
		case RT2560_TX_FAIL_OTHER:
		default:
			DPRINTF(("%s: sending data frame failed 0x%08x\n",
			    sc->sc_dev.dv_xname, letoh32(desc->flags)));
			ifp->if_oerrors++;
			break;
		}

		/* descriptor is no longer valid */
		desc->flags &= ~htole32(RT2560_TX_VALID);

		bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
		    sc->txq.next * RT2560_TX_DESC_SIZE, RT2560_TX_DESC_SIZE,
		    BUS_DMASYNC_PREWRITE);

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, data->map);
		m_freem(data->m);
		data->m = NULL;
		ieee80211_release_node(ic, data->ni);
		data->ni = NULL;

		DPRINTFN(15, ("tx done idx=%u\n", sc->txq.next));

		sc->txq.queued--;
		sc->txq.next = (sc->txq.next + 1) % RT2560_TX_RING_COUNT;
	}

	if (sc->txq.queued == 0 && sc->prioq.queued == 0)
		sc->sc_tx_timer = 0;
	if (sc->txq.queued < RT2560_TX_RING_COUNT - 1) {
		sc->sc_flags &= ~RT2560_DATA_OACTIVE;
		if (!(sc->sc_flags & (RT2560_DATA_OACTIVE|RT2560_PRIO_OACTIVE)))
			ifq_clr_oactive(&ifp->if_snd);
		rt2560_start(ifp);
	}
}

void
rt2560_prio_intr(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	for (;;) {
		struct rt2560_tx_desc *desc = &sc->prioq.desc[sc->prioq.next];
		struct rt2560_tx_data *data = &sc->prioq.data[sc->prioq.next];

		bus_dmamap_sync(sc->sc_dmat, sc->prioq.map,
		    sc->prioq.next * RT2560_TX_DESC_SIZE, RT2560_TX_DESC_SIZE,
		    BUS_DMASYNC_POSTREAD);

		if ((letoh32(desc->flags) & RT2560_TX_BUSY) ||
		    !(letoh32(desc->flags) & RT2560_TX_VALID))
			break;

		switch (letoh32(desc->flags) & RT2560_TX_RESULT_MASK) {
		case RT2560_TX_SUCCESS:
			DPRINTFN(10, ("mgt frame sent successfully\n"));
			break;

		case RT2560_TX_SUCCESS_RETRY:
			DPRINTFN(9, ("mgt frame sent after %u retries\n",
			    (letoh32(desc->flags) >> 5) & 0x7));
			break;

		case RT2560_TX_FAIL_RETRY:
			DPRINTFN(9, ("sending mgt frame failed (too much "
			    "retries)\n"));
			break;

		case RT2560_TX_FAIL_INVALID:
		case RT2560_TX_FAIL_OTHER:
		default:
			DPRINTF(("%s: sending mgt frame failed 0x%08x\n",
			    sc->sc_dev.dv_xname, letoh32(desc->flags)));
			break;
		}

		/* descriptor is no longer valid */
		desc->flags &= ~htole32(RT2560_TX_VALID);

		bus_dmamap_sync(sc->sc_dmat, sc->prioq.map,
		    sc->prioq.next * RT2560_TX_DESC_SIZE, RT2560_TX_DESC_SIZE,
		    BUS_DMASYNC_PREWRITE);

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, data->map);
		m_freem(data->m);
		data->m = NULL;
		ieee80211_release_node(ic, data->ni);
		data->ni = NULL;

		DPRINTFN(15, ("prio done idx=%u\n", sc->prioq.next));

		sc->prioq.queued--;
		sc->prioq.next = (sc->prioq.next + 1) % RT2560_PRIO_RING_COUNT;
	}

	if (sc->txq.queued == 0 && sc->prioq.queued == 0)
		sc->sc_tx_timer = 0;
	if (sc->prioq.queued < RT2560_PRIO_RING_COUNT) {
		sc->sc_flags &= ~RT2560_PRIO_OACTIVE;
		if (!(sc->sc_flags & (RT2560_DATA_OACTIVE|RT2560_PRIO_OACTIVE)))
			ifq_clr_oactive(&ifp->if_snd);
		rt2560_start(ifp);
	}
}

/*
 * Some frames were processed by the hardware cipher engine and are ready for
 * transmission to the IEEE802.11 layer.
 */
void
rt2560_decryption_intr(struct rt2560_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct mbuf *mnew, *m;
	int hw, error;

	/* retrieve last descriptor index processed by cipher engine */
	hw = (RAL_READ(sc, RT2560_SECCSR0) - sc->rxq.physaddr) /
	    RT2560_RX_DESC_SIZE;

	for (; sc->rxq.cur_decrypt != hw;) {
		struct rt2560_rx_desc *desc =
		    &sc->rxq.desc[sc->rxq.cur_decrypt];
		struct rt2560_rx_data *data =
		    &sc->rxq.data[sc->rxq.cur_decrypt];

		bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
		    sc->rxq.cur_decrypt * RT2560_TX_DESC_SIZE,
		    RT2560_TX_DESC_SIZE, BUS_DMASYNC_POSTREAD);

		if (letoh32(desc->flags) &
		    (RT2560_RX_BUSY | RT2560_RX_CIPHER_BUSY))
			break;

		if (data->drop) {
			ifp->if_ierrors++;
			goto skip;
		}

		if ((letoh32(desc->flags) & RT2560_RX_CIPHER_MASK) != 0 &&
		    (letoh32(desc->flags) & RT2560_RX_ICV_ERROR)) {
			ifp->if_ierrors++;
			goto skip;
		}

		/*
		 * Try to allocate a new mbuf for this ring element and load it
		 * before processing the current mbuf.  If the ring element
		 * cannot be loaded, drop the received packet and reuse the old
		 * mbuf.  In the unlikely case that the old mbuf can't be
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
			struct rt2560_rx_radiotap_header *tap = &sc->sc_rxtap;
			uint32_t tsf_lo, tsf_hi;

			/* get timestamp (low and high 32 bits) */
			tsf_hi = RAL_READ(sc, RT2560_CSR17);
			tsf_lo = RAL_READ(sc, RT2560_CSR16);

			tap->wr_tsf =
			    htole64(((uint64_t)tsf_hi << 32) | tsf_lo);
			tap->wr_flags = 0;
			tap->wr_rate = rt2560_rxrate(desc);
			tap->wr_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
			tap->wr_chan_flags =
			    htole16(ic->ic_ibss_chan->ic_flags);
			tap->wr_antenna = sc->rx_ant;
			tap->wr_antsignal = desc->rssi;

			bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len, m,
			    BPF_DIRECTION_IN);
		}
#endif
		wh = mtod(m, struct ieee80211_frame *);
		ni = ieee80211_find_rxnode(ic, wh);

		/* send the frame to the 802.11 layer */
		memset(&rxi, 0, sizeof(rxi));
		rxi.rxi_rssi = desc->rssi;
		rxi.rxi_tstamp = 0;	/* unused */
		ieee80211_inputm(ifp, m, ni, &rxi, &ml);

		/* node is no longer needed */
		ieee80211_release_node(ic, ni);

skip:		desc->flags = htole32(RT2560_RX_BUSY);

		bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
		    sc->rxq.cur_decrypt * RT2560_TX_DESC_SIZE,
		    RT2560_TX_DESC_SIZE, BUS_DMASYNC_PREWRITE);

		DPRINTFN(15, ("decryption done idx=%u\n", sc->rxq.cur_decrypt));

		sc->rxq.cur_decrypt =
		    (sc->rxq.cur_decrypt + 1) % RT2560_RX_RING_COUNT;
	}
	if_input(ifp, &ml);
}

/*
 * Some frames were received. Pass them to the hardware cipher engine before
 * sending them to the 802.11 layer.
 */
void
rt2560_rx_intr(struct rt2560_softc *sc)
{
	for (;;) {
		struct rt2560_rx_desc *desc = &sc->rxq.desc[sc->rxq.cur];
		struct rt2560_rx_data *data = &sc->rxq.data[sc->rxq.cur];

		bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
		    sc->rxq.cur * RT2560_RX_DESC_SIZE, RT2560_RX_DESC_SIZE,
		    BUS_DMASYNC_POSTREAD);

		if (letoh32(desc->flags) &
		    (RT2560_RX_BUSY | RT2560_RX_CIPHER_BUSY))
			break;

		data->drop = 0;

		if (letoh32(desc->flags) &
		    (RT2560_RX_PHY_ERROR | RT2560_RX_CRC_ERROR)) {
			/*
			 * This should not happen since we did not request
			 * to receive those frames when we filled RXCSR0.
			 */
			DPRINTFN(5, ("PHY or CRC error flags 0x%08x\n",
			    letoh32(desc->flags)));
			data->drop = 1;
		}

		if (((letoh32(desc->flags) >> 16) & 0xfff) > MCLBYTES) {
			DPRINTFN(5, ("bad length\n"));
			data->drop = 1;
		}

		/* mark the frame for decryption */
		desc->flags |= htole32(RT2560_RX_CIPHER_BUSY);

		bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
		    sc->rxq.cur * RT2560_RX_DESC_SIZE, RT2560_RX_DESC_SIZE,
		    BUS_DMASYNC_PREWRITE);

		DPRINTFN(15, ("rx done idx=%u\n", sc->rxq.cur));

		sc->rxq.cur = (sc->rxq.cur + 1) % RT2560_RX_RING_COUNT;
	}

	/* kick decrypt */
	RAL_WRITE(sc, RT2560_SECCSR0, RT2560_KICK_DECRYPT);
}

#ifndef IEEE80211_STA_ONLY
/*
 * This function is called in HostAP or IBSS modes when it's time to send a
 * new beacon (every ni_intval milliseconds).
 */
void
rt2560_beacon_expire(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2560_tx_data *data;

	if (ic->ic_opmode != IEEE80211_M_IBSS &&
	    ic->ic_opmode != IEEE80211_M_HOSTAP)
		return;

	data = &sc->bcnq.data[sc->bcnq.next];

	if (sc->sc_flags & RT2560_UPDATE_SLOT) {
		sc->sc_flags &= ~RT2560_UPDATE_SLOT;
		sc->sc_flags |= RT2560_SET_SLOTTIME;
	} else if (sc->sc_flags & RT2560_SET_SLOTTIME) {
		sc->sc_flags &= ~RT2560_SET_SLOTTIME;
		rt2560_set_slottime(sc);
	}

	if (ic->ic_curmode == IEEE80211_MODE_11G) {
		/* update ERP Information Element */
		*sc->erp = ic->ic_bss->ni_erp;
		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_PREWRITE);
	}

#if defined(RT2560_DEBUG) && NBPFILTER > 0
	if (ic->ic_rawbpf != NULL)
		bpf_mtap(ic->ic_rawbpf, data->m, BPF_DIRECTION_OUT);
#endif

	DPRINTFN(15, ("beacon expired\n"));
}
#endif

void
rt2560_wakeup_expire(struct rt2560_softc *sc)
{
	DPRINTFN(15, ("wakeup expired\n"));
}

int
rt2560_intr(void *arg)
{
	struct rt2560_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t r;

	r = RAL_READ(sc, RT2560_CSR7);
	if (__predict_false(r == 0xffffffff))
		return 0;	/* device likely went away */
	if (r == 0)
		return 0;	/* not for us */

	/* disable interrupts */
	RAL_WRITE(sc, RT2560_CSR8, 0xffffffff);

	/* acknowledge interrupts */
	RAL_WRITE(sc, RT2560_CSR7, r);

	/* don't re-enable interrupts if we're shutting down */
	if (!(ifp->if_flags & IFF_RUNNING))
		return 0;

#ifndef IEEE80211_STA_ONLY
	if (r & RT2560_BEACON_EXPIRE)
		rt2560_beacon_expire(sc);
#endif

	if (r & RT2560_WAKEUP_EXPIRE)
		rt2560_wakeup_expire(sc);

	if (r & RT2560_ENCRYPTION_DONE)
		rt2560_encryption_intr(sc);

	if (r & RT2560_TX_DONE)
		rt2560_tx_intr(sc);

	if (r & RT2560_PRIO_DONE)
		rt2560_prio_intr(sc);

	if (r & RT2560_DECRYPTION_DONE)
		rt2560_decryption_intr(sc);

	if (r & RT2560_RX_DONE)
		rt2560_rx_intr(sc);

	/* re-enable interrupts */
	RAL_WRITE(sc, RT2560_CSR8, RT2560_INTR_MASK);

	return 1;
}

/* quickly determine if a given rate is CCK or OFDM */
#define RAL_RATE_IS_OFDM(rate) ((rate) >= 12 && (rate) != 22)

#define RAL_ACK_SIZE	14	/* 10 + 4(FCS) */
#define RAL_CTS_SIZE	14	/* 10 + 4(FCS) */

#define RAL_SIFS		10	/* us */

#define RT2560_RXTX_TURNAROUND	10	/* us */

/*
 * This function is only used by the Rx radiotap code. It returns the rate at
 * which a given frame was received.
 */
#if NBPFILTER > 0
uint8_t
rt2560_rxrate(const struct rt2560_rx_desc *desc)
{
	if (letoh32(desc->flags) & RT2560_RX_OFDM) {
		/* reverse function of rt2560_plcp_signal */
		switch (desc->rate) {
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
rt2560_ack_rate(struct ieee80211com *ic, int rate)
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
rt2560_txtime(int len, int rate, uint32_t flags)
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
rt2560_plcp_signal(int rate)
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
rt2560_setup_tx_desc(struct rt2560_softc *sc, struct rt2560_tx_desc *desc,
    uint32_t flags, int len, int rate, int encrypt, bus_addr_t physaddr)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t plcp_length;
	int remainder;

	desc->flags = htole32(flags);
	desc->flags |= htole32(len << 16);
	desc->flags |= encrypt ? htole32(RT2560_TX_CIPHER_BUSY) :
	    htole32(RT2560_TX_BUSY | RT2560_TX_VALID);

	desc->physaddr = htole32(physaddr);
	desc->wme = htole16(
	    RT2560_AIFSN(2) |
	    RT2560_LOGCWMIN(3) |
	    RT2560_LOGCWMAX(8));

	/* setup PLCP fields */
	desc->plcp_signal  = rt2560_plcp_signal(rate);
	desc->plcp_service = 4;

	len += IEEE80211_CRC_LEN;
	if (RAL_RATE_IS_OFDM(rate)) {
		desc->flags |= htole32(RT2560_TX_OFDM);

		plcp_length = len & 0xfff;
		desc->plcp_length_hi = plcp_length >> 6;
		desc->plcp_length_lo = plcp_length & 0x3f;
	} else {
		plcp_length = (16 * len + rate - 1) / rate;
		if (rate == 22) {
			remainder = (16 * len) % 22;
			if (remainder != 0 && remainder < 7)
				desc->plcp_service |= RT2560_PLCP_LENGEXT;
		}
		desc->plcp_length_hi = plcp_length >> 8;
		desc->plcp_length_lo = plcp_length & 0xff;

		if (rate != 2 && (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			desc->plcp_signal |= 0x08;
	}
}

#ifndef IEEE80211_STA_ONLY
int
rt2560_tx_bcn(struct rt2560_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	int rate = 2, error;

	desc = &sc->bcnq.desc[sc->bcnq.cur];
	data = &sc->bcnq.data[sc->bcnq.cur];

	error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m0,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		m_freem(m0);
		return error;
	}

	data->m = m0;
	data->ni = ni;

	rt2560_setup_tx_desc(sc, desc, RT2560_TX_IFS_NEWBACKOFF |
	    RT2560_TX_TIMESTAMP, m0->m_pkthdr.len, rate, 0,
	    data->map->dm_segs->ds_addr);

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->bcnq.map,
	    sc->bcnq.cur * RT2560_TX_DESC_SIZE, RT2560_TX_DESC_SIZE,
	    BUS_DMASYNC_PREWRITE);

	/*
	 * Store pointer to ERP Information Element so that we can update it
	 * dynamically when the slot time changes.
	 * XXX: this is ugly since it depends on how net80211 builds beacon
	 * frames but ieee80211_beacon_alloc() don't store offsets for us.
	 */
	if (ic->ic_curmode == IEEE80211_MODE_11G) {
		sc->erp =
		    mtod(m0, uint8_t *) +
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

int
rt2560_tx_mgt(struct rt2560_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	struct ieee80211_frame *wh;
	uint16_t dur;
	uint32_t flags = 0;
	int rate = 2, error;

	desc = &sc->prioq.desc[sc->prioq.cur];
	data = &sc->prioq.data[sc->prioq.cur];

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
		struct rt2560_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);
		tap->wt_antenna = sc->tx_ant;

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len, m0,
		    BPF_DIRECTION_OUT);
	}
#endif

	data->m = m0;
	data->ni = ni;

	wh = mtod(m0, struct ieee80211_frame *);

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2560_TX_NEED_ACK;

		dur = rt2560_txtime(RAL_ACK_SIZE, rate, ic->ic_flags) +
		    RAL_SIFS;
		*(uint16_t *)wh->i_dur = htole16(dur);

#ifndef IEEE80211_STA_ONLY
		/* tell hardware to set timestamp for probe responses */
		if ((wh->i_fc[0] &
		    (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
		    (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP))
			flags |= RT2560_TX_TIMESTAMP;
#endif
	}

	rt2560_setup_tx_desc(sc, desc, flags, m0->m_pkthdr.len, rate, 0,
	    data->map->dm_segs->ds_addr);

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->prioq.map,
	    sc->prioq.cur * RT2560_TX_DESC_SIZE, RT2560_TX_DESC_SIZE,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(10, ("sending mgt frame len=%u idx=%u rate=%u\n",
	    m0->m_pkthdr.len, sc->prioq.cur, rate));

	/* kick prio */
	sc->prioq.queued++;
	sc->prioq.cur = (sc->prioq.cur + 1) % RT2560_PRIO_RING_COUNT;
	RAL_WRITE(sc, RT2560_TXCSR0, RT2560_KICK_PRIO);

	return 0;
}

int
rt2560_tx_data(struct rt2560_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2560_tx_ring *txq = &sc->txq;
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	struct mbuf *m1;
	uint16_t dur;
	uint32_t flags = 0;
	int pktlen, rate, needcts = 0, needrts = 0, error;

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
	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
	     IEEE80211_FC0_TYPE_MGT)) {
		/* mgmt/multicast frames are sent at the lowest avail. rate */
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
		flags |= RT2560_TX_IFS_SIFS;

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

		protrate = 2;	/* XXX */
		ackrate  = rt2560_ack_rate(ic, rate);

		dur = rt2560_txtime(pktlen, rate, ic->ic_flags) +
		      rt2560_txtime(RAL_ACK_SIZE, ackrate, ic->ic_flags) +
		      2 * RAL_SIFS;
		if (needrts) {
			dur += rt2560_txtime(RAL_CTS_SIZE, rt2560_ack_rate(ic,
			    protrate), ic->ic_flags) + RAL_SIFS;
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

		desc = &txq->desc[txq->cur_encrypt];
		data = &txq->data[txq->cur_encrypt];

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

		rt2560_setup_tx_desc(sc, desc,
		    (needrts ? RT2560_TX_NEED_ACK : 0) | RT2560_TX_MORE_FRAG,
		    mprot->m_pkthdr.len, protrate, 1,
		    data->map->dm_segs->ds_addr);

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_PREWRITE);
		bus_dmamap_sync(sc->sc_dmat, txq->map,
		    txq->cur_encrypt * RT2560_TX_DESC_SIZE,
		    RT2560_TX_DESC_SIZE, BUS_DMASYNC_PREWRITE);

		txq->queued++;
		if (++txq->cur_encrypt >= txq->count)
			txq->cur_encrypt = 0;

		flags |= RT2560_TX_LONG_RETRY | RT2560_TX_IFS_SIFS;
	}

	data = &txq->data[txq->cur_encrypt];
	desc = &txq->desc[txq->cur_encrypt];

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
		struct rt2560_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);
		tap->wt_antenna = sc->tx_ant;

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len, m0,
		    BPF_DIRECTION_OUT);
	}
#endif

	data->m = m0;
	data->ni = ni;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2560_TX_NEED_ACK;

		dur = rt2560_txtime(RAL_ACK_SIZE, rt2560_ack_rate(ic, rate),
		    ic->ic_flags) + RAL_SIFS;
		*(uint16_t *)wh->i_dur = htole16(dur);
	}

	rt2560_setup_tx_desc(sc, desc, flags, m0->m_pkthdr.len, rate, 1,
	    data->map->dm_segs->ds_addr);

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, txq->map,
	    txq->cur_encrypt * RT2560_TX_DESC_SIZE, RT2560_TX_DESC_SIZE,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(10, ("sending frame len=%u idx=%u rate=%u\n",
	    m0->m_pkthdr.len, txq->cur_encrypt, rate));

	/* kick encrypt */
	txq->queued++;
	if (++txq->cur_encrypt >= txq->count)
		txq->cur_encrypt = 0;
	RAL_WRITE(sc, RT2560_SECCSR1, RT2560_KICK_ENCRYPT);

	return 0;
}

void
rt2560_start(struct ifnet *ifp)
{
	struct rt2560_softc *sc = ifp->if_softc;
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
			if (sc->prioq.queued >= RT2560_PRIO_RING_COUNT) {
				ifq_set_oactive(&ifp->if_snd);
				sc->sc_flags |= RT2560_PRIO_OACTIVE;
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
			if (rt2560_tx_mgt(sc, m0, ni) != 0)
				break;

		} else {
			/* Because RTS/CTS requires an extra frame we need
			 * space for 2 frames on the regular Tx queue. */
			if (sc->txq.queued >= RT2560_TX_RING_COUNT - 1) {
				ifq_set_oactive(&ifp->if_snd);
				sc->sc_flags |= RT2560_DATA_OACTIVE;
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
				bpf_mtap(ic->ic_rawbpf, m0, BPF_DIRECTION_OUT);
#endif
			if (rt2560_tx_data(sc, m0, ni) != 0) {
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
rt2560_watchdog(struct ifnet *ifp)
{
	struct rt2560_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			rt2560_init(ifp);
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
rt2560_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct rt2560_softc *sc = ifp->if_softc;
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
				rt2560_update_promisc(sc);
			else
				rt2560_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				rt2560_stop(ifp, 1);
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
				rt2560_set_chan(sc, ic->ic_ibss_chan);
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			rt2560_init(ifp);
		error = 0;
	}

	splx(s);

	return error;
}

void
rt2560_bbp_write(struct rt2560_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2560_BBPCSR) & RT2560_BBP_BUSY))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not write to BBP\n", sc->sc_dev.dv_xname);
		return;
	}

	tmp = RT2560_BBP_WRITE | RT2560_BBP_BUSY | reg << 8 | val;
	RAL_WRITE(sc, RT2560_BBPCSR, tmp);

	DPRINTFN(15, ("BBP R%u <- 0x%02x\n", reg, val));
}

uint8_t
rt2560_bbp_read(struct rt2560_softc *sc, uint8_t reg)
{
	uint32_t val;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2560_BBPCSR) & RT2560_BBP_BUSY))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not read from BBP\n", sc->sc_dev.dv_xname);
		return 0;
	}

	val = RT2560_BBP_BUSY | reg << 8;
	RAL_WRITE(sc, RT2560_BBPCSR, val);

	for (ntries = 0; ntries < 100; ntries++) {
		val = RAL_READ(sc, RT2560_BBPCSR);
		if (!(val & RT2560_BBP_BUSY))
			return val & 0xff;
		DELAY(1);
	}

	printf("%s: could not read from BBP\n", sc->sc_dev.dv_xname);
	return 0;
}

void
rt2560_rf_write(struct rt2560_softc *sc, uint8_t reg, uint32_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2560_RFCSR) & RT2560_RF_BUSY))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not write to RF\n", sc->sc_dev.dv_xname);
		return;
	}

	tmp = RT2560_RF_BUSY | RT2560_RF_20BIT | (val & 0xfffff) << 2 |
	    (reg & 0x3);
	RAL_WRITE(sc, RT2560_RFCSR, tmp);

	/* remember last written value in sc */
	sc->rf_regs[reg] = val;

	DPRINTFN(15, ("RF R[%u] <- 0x%05x\n", reg & 0x3, val & 0xfffff));
}

void
rt2560_set_chan(struct rt2560_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t power, tmp;
	u_int chan;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return;

	power = min(sc->txpow[chan - 1], 31);

	DPRINTFN(2, ("setting channel to %u, txpower to %u\n", chan, power));

	switch (sc->rf_rev) {
	case RT2560_RF_2522:
		rt2560_rf_write(sc, RT2560_RF1, 0x00814);
		rt2560_rf_write(sc, RT2560_RF2, rt2560_rf2522_r2[chan - 1]);
		rt2560_rf_write(sc, RT2560_RF3, power << 7 | 0x00040);
		break;

	case RT2560_RF_2523:
		rt2560_rf_write(sc, RT2560_RF1, 0x08804);
		rt2560_rf_write(sc, RT2560_RF2, rt2560_rf2523_r2[chan - 1]);
		rt2560_rf_write(sc, RT2560_RF3, power << 7 | 0x38044);
		rt2560_rf_write(sc, RT2560_RF4,
		    (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RT2560_RF_2524:
		rt2560_rf_write(sc, RT2560_RF1, 0x0c808);
		rt2560_rf_write(sc, RT2560_RF2, rt2560_rf2524_r2[chan - 1]);
		rt2560_rf_write(sc, RT2560_RF3, power << 7 | 0x00040);
		rt2560_rf_write(sc, RT2560_RF4,
		    (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RT2560_RF_2525:
		rt2560_rf_write(sc, RT2560_RF1, 0x08808);
		rt2560_rf_write(sc, RT2560_RF2, rt2560_rf2525_hi_r2[chan - 1]);
		rt2560_rf_write(sc, RT2560_RF3, power << 7 | 0x18044);
		rt2560_rf_write(sc, RT2560_RF4,
		    (chan == 14) ? 0x00280 : 0x00286);

		rt2560_rf_write(sc, RT2560_RF1, 0x08808);
		rt2560_rf_write(sc, RT2560_RF2, rt2560_rf2525_r2[chan - 1]);
		rt2560_rf_write(sc, RT2560_RF3, power << 7 | 0x18044);
		rt2560_rf_write(sc, RT2560_RF4,
		    (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RT2560_RF_2525E:
		rt2560_rf_write(sc, RT2560_RF1, 0x08808);
		rt2560_rf_write(sc, RT2560_RF2, rt2560_rf2525e_r2[chan - 1]);
		rt2560_rf_write(sc, RT2560_RF3, power << 7 | 0x18044);
		rt2560_rf_write(sc, RT2560_RF4,
		    (chan == 14) ? 0x00286 : 0x00282);
		break;

	case RT2560_RF_2526:
		rt2560_rf_write(sc, RT2560_RF2, rt2560_rf2526_hi_r2[chan - 1]);
		rt2560_rf_write(sc, RT2560_RF4,
		   (chan & 1) ? 0x00386 : 0x00381);
		rt2560_rf_write(sc, RT2560_RF1, 0x08804);

		rt2560_rf_write(sc, RT2560_RF2, rt2560_rf2526_r2[chan - 1]);
		rt2560_rf_write(sc, RT2560_RF3, power << 7 | 0x18044);
		rt2560_rf_write(sc, RT2560_RF4,
		    (chan & 1) ? 0x00386 : 0x00381);
		break;
	}

	if (ic->ic_opmode != IEEE80211_M_MONITOR &&
	    ic->ic_state != IEEE80211_S_SCAN) {
		/* set Japan filter bit for channel 14 */
		tmp = rt2560_bbp_read(sc, 70);

		tmp &= ~RT2560_JAPAN_FILTER;
		if (chan == 14)
			tmp |= RT2560_JAPAN_FILTER;

		rt2560_bbp_write(sc, 70, tmp);

		DELAY(1000); /* RF needs a 1ms delay here */
		rt2560_disable_rf_tune(sc);

		/* clear CRC errors */
		RAL_READ(sc, RT2560_CNT0);
	}
}

/*
 * Disable RF auto-tuning.
 */
void
rt2560_disable_rf_tune(struct rt2560_softc *sc)
{
	uint32_t tmp;

	if (sc->rf_rev != RT2560_RF_2523) {
		tmp = sc->rf_regs[RT2560_RF1] & ~RT2560_RF1_AUTOTUNE;
		rt2560_rf_write(sc, RT2560_RF1, tmp);
	}

	tmp = sc->rf_regs[RT2560_RF3] & ~RT2560_RF3_AUTOTUNE;
	rt2560_rf_write(sc, RT2560_RF3, tmp);

	DPRINTFN(2, ("disabling RF autotune\n"));
}

/*
 * Refer to IEEE Std 802.11-1999 pp. 123 for more information on TSF
 * synchronization.
 */
void
rt2560_enable_tsf_sync(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t logcwmin, preload;
	uint32_t tmp;

	/* first, disable TSF synchronization */
	RAL_WRITE(sc, RT2560_CSR14, 0);

	tmp = 16 * ic->ic_bss->ni_intval;
	RAL_WRITE(sc, RT2560_CSR12, tmp);

	RAL_WRITE(sc, RT2560_CSR13, 0);

	logcwmin = 5;
	preload = (ic->ic_opmode == IEEE80211_M_STA) ? 384 : 1024;
	tmp = logcwmin << 16 | preload;
	RAL_WRITE(sc, RT2560_BCNOCSR, tmp);

	/* finally, enable TSF synchronization */
	tmp = RT2560_ENABLE_TSF | RT2560_ENABLE_TBCN;
	if (ic->ic_opmode == IEEE80211_M_STA)
		tmp |= RT2560_ENABLE_TSF_SYNC(1);
#ifndef IEEE80211_STA_ONLY
	else
		tmp |= RT2560_ENABLE_TSF_SYNC(2) |
		       RT2560_ENABLE_BEACON_GENERATOR;
#endif
	RAL_WRITE(sc, RT2560_CSR14, tmp);

	DPRINTF(("enabling TSF synchronization\n"));
}

void
rt2560_update_plcp(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* no short preamble for 1Mbps */
	RAL_WRITE(sc, RT2560_PLCP1MCSR, 0x00700400);

	if (!(ic->ic_flags & IEEE80211_F_SHPREAMBLE)) {
		/* values taken from the reference driver */
		RAL_WRITE(sc, RT2560_PLCP2MCSR,   0x00380401);
		RAL_WRITE(sc, RT2560_PLCP5p5MCSR, 0x00150402);
		RAL_WRITE(sc, RT2560_PLCP11MCSR,  0x000b8403);
	} else {
		/* same values as above or'ed 0x8 */
		RAL_WRITE(sc, RT2560_PLCP2MCSR,   0x00380409);
		RAL_WRITE(sc, RT2560_PLCP5p5MCSR, 0x0015040a);
		RAL_WRITE(sc, RT2560_PLCP11MCSR,  0x000b840b);
	}

	DPRINTF(("updating PLCP for %s preamble\n",
	    (ic->ic_flags & IEEE80211_F_SHPREAMBLE) ? "short" : "long"));
}

void
rt2560_updateslot(struct ieee80211com *ic)
{
	struct rt2560_softc *sc = ic->ic_if.if_softc;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
		/*
		 * In HostAP mode, we defer setting of new slot time until
		 * updated ERP Information Element has propagated to all
		 * associated STAs.
		 */
		sc->sc_flags |= RT2560_UPDATE_SLOT;
	} else
#endif
		rt2560_set_slottime(sc);
}

/*
 * IEEE 802.11a (and possibly 802.11g) use short slot time. Refer to
 * IEEE Std 802.11-1999 pp. 85 to know how these values are computed.
 */
void
rt2560_set_slottime(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t slottime;
	uint16_t sifs, pifs, difs, eifs;
	uint32_t tmp;

	slottime = (ic->ic_flags & IEEE80211_F_SHSLOT) ?
	    IEEE80211_DUR_DS_SHSLOT : IEEE80211_DUR_DS_SLOT;

	/* define the MAC slot boundaries */
	sifs = RAL_SIFS - RT2560_RXTX_TURNAROUND;
	pifs = sifs + slottime;
	difs = sifs + 2 * slottime;
	eifs = (ic->ic_curmode == IEEE80211_MODE_11B) ? 364 : 60;

	tmp = RAL_READ(sc, RT2560_CSR11);
	tmp = (tmp & ~0x1f00) | slottime << 8;
	RAL_WRITE(sc, RT2560_CSR11, tmp);

	tmp = pifs << 16 | sifs;
	RAL_WRITE(sc, RT2560_CSR18, tmp);

	tmp = eifs << 16 | difs;
	RAL_WRITE(sc, RT2560_CSR19, tmp);

	DPRINTF(("setting slottime to %uus\n", slottime));
}

void
rt2560_set_basicrates(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* update basic rate set */
	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		/* 11b basic rates: 1, 2Mbps */
		RAL_WRITE(sc, RT2560_ARSP_PLCP_1, 0x3);
	} else {
		/* 11b/g basic rates: 1, 2, 5.5, 11Mbps */
		RAL_WRITE(sc, RT2560_ARSP_PLCP_1, 0xf);
	}
}

void
rt2560_update_led(struct rt2560_softc *sc, int led1, int led2)
{
	uint32_t tmp;

	/* set ON period to 70ms and OFF period to 30ms */
	tmp = led1 << 16 | led2 << 17 | 70 << 8 | 30;
	RAL_WRITE(sc, RT2560_LEDCSR, tmp);
}

void
rt2560_set_bssid(struct rt2560_softc *sc, uint8_t *bssid)
{
	uint32_t tmp;

	tmp = bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24;
	RAL_WRITE(sc, RT2560_CSR5, tmp);

	tmp = bssid[4] | bssid[5] << 8;
	RAL_WRITE(sc, RT2560_CSR6, tmp);

	DPRINTF(("setting BSSID to %s\n", ether_sprintf(bssid)));
}

void
rt2560_set_macaddr(struct rt2560_softc *sc, uint8_t *addr)
{
	uint32_t tmp;

	tmp = addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24;
	RAL_WRITE(sc, RT2560_CSR3, tmp);

	tmp = addr[4] | addr[5] << 8;
	RAL_WRITE(sc, RT2560_CSR4, tmp);

	DPRINTF(("setting MAC address to %s\n", ether_sprintf(addr)));
}

void
rt2560_get_macaddr(struct rt2560_softc *sc, uint8_t *addr)
{
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2560_CSR3);
	addr[0] = tmp & 0xff;
	addr[1] = (tmp >>  8) & 0xff;
	addr[2] = (tmp >> 16) & 0xff;
	addr[3] = (tmp >> 24);

	tmp = RAL_READ(sc, RT2560_CSR4);
	addr[4] = tmp & 0xff;
	addr[5] = (tmp >> 8) & 0xff;
}

void
rt2560_update_promisc(struct rt2560_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2560_RXCSR0);

	tmp &= ~RT2560_DROP_NOT_TO_ME;
	if (!(ifp->if_flags & IFF_PROMISC))
		tmp |= RT2560_DROP_NOT_TO_ME;

	RAL_WRITE(sc, RT2560_RXCSR0, tmp);

	DPRINTF(("%s promiscuous mode\n", (ifp->if_flags & IFF_PROMISC) ?
	    "entering" : "leaving"));
}

void
rt2560_set_txantenna(struct rt2560_softc *sc, int antenna)
{
	uint32_t tmp;
	uint8_t tx;

	tx = rt2560_bbp_read(sc, RT2560_BBP_TX) & ~RT2560_BBP_ANTMASK;
	if (antenna == 1)
		tx |= RT2560_BBP_ANTA;
	else if (antenna == 2)
		tx |= RT2560_BBP_ANTB;
	else
		tx |= RT2560_BBP_DIVERSITY;

	/* need to force I/Q flip for RF 2525e, 2526 and 5222 */
	if (sc->rf_rev == RT2560_RF_2525E || sc->rf_rev == RT2560_RF_2526 ||
	    sc->rf_rev == RT2560_RF_5222)
		tx |= RT2560_BBP_FLIPIQ;

	rt2560_bbp_write(sc, RT2560_BBP_TX, tx);

	/* update values for CCK and OFDM in BBPCSR1 */
	tmp = RAL_READ(sc, RT2560_BBPCSR1) & ~0x00070007;
	tmp |= (tx & 0x7) << 16 | (tx & 0x7);
	RAL_WRITE(sc, RT2560_BBPCSR1, tmp);
}

void
rt2560_set_rxantenna(struct rt2560_softc *sc, int antenna)
{
	uint8_t rx;

	rx = rt2560_bbp_read(sc, RT2560_BBP_RX) & ~RT2560_BBP_ANTMASK;
	if (antenna == 1)
		rx |= RT2560_BBP_ANTA;
	else if (antenna == 2)
		rx |= RT2560_BBP_ANTB;
	else
		rx |= RT2560_BBP_DIVERSITY;

	/* need to force no I/Q flip for RF 2525e and 2526 */
	if (sc->rf_rev == RT2560_RF_2525E || sc->rf_rev == RT2560_RF_2526)
		rx &= ~RT2560_BBP_FLIPIQ;

	rt2560_bbp_write(sc, RT2560_BBP_RX, rx);
}

const char *
rt2560_get_rf(int rev)
{
	switch (rev) {
	case RT2560_RF_2522:	return "RT2522";
	case RT2560_RF_2523:	return "RT2523";
	case RT2560_RF_2524:	return "RT2524";
	case RT2560_RF_2525:	return "RT2525";
	case RT2560_RF_2525E:	return "RT2525e";
	case RT2560_RF_2526:	return "RT2526";
	case RT2560_RF_5222:	return "RT5222";
	default:		return "unknown";
	}
}

void
rt2560_read_eeprom(struct rt2560_softc *sc)
{
	uint16_t val;
	int i;

	val = rt2560_eeprom_read(sc, RT2560_EEPROM_CONFIG0);
	sc->rf_rev =   (val >> 11) & 0x1f;
	sc->hw_radio = (val >> 10) & 0x1;
	sc->led_mode = (val >> 6)  & 0x7;
	sc->rx_ant =   (val >> 4)  & 0x3;
	sc->tx_ant =   (val >> 2)  & 0x3;
	sc->nb_ant =   val & 0x3;

	/* read default values for BBP registers */
	for (i = 0; i < 16; i++) {
		val = rt2560_eeprom_read(sc, RT2560_EEPROM_BBP_BASE + i);
		sc->bbp_prom[i].reg = val >> 8;
		sc->bbp_prom[i].val = val & 0xff;
	}

	/* read Tx power for all b/g channels */
	for (i = 0; i < 14 / 2; i++) {
		val = rt2560_eeprom_read(sc, RT2560_EEPROM_TXPOWER + i);
		sc->txpow[i * 2] = val >> 8;
		sc->txpow[i * 2 + 1] = val & 0xff;
	}
}

int
rt2560_bbp_init(struct rt2560_softc *sc)
{
	int i, ntries;

	/* wait for BBP to be ready */
	for (ntries = 0; ntries < 100; ntries++) {
		if (rt2560_bbp_read(sc, RT2560_BBP_VERSION) != 0)
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for BBP\n", sc->sc_dev.dv_xname);
		return EIO;
	}

	/* initialize BBP registers to default values */
	for (i = 0; i < nitems(rt2560_def_bbp); i++) {
		rt2560_bbp_write(sc, rt2560_def_bbp[i].reg,
		    rt2560_def_bbp[i].val);
	}
#if 0
	/* initialize BBP registers to values stored in EEPROM */
	for (i = 0; i < 16; i++) {
		if (sc->bbp_prom[i].reg == 0xff)
			continue;
		rt2560_bbp_write(sc, sc->bbp_prom[i].reg, sc->bbp_prom[i].val);
	}
#endif

	return 0;
}

int
rt2560_init(struct ifnet *ifp)
{
	struct rt2560_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	int i;

	/* for CardBus, power on the socket */
	if (!(sc->sc_flags & RT2560_ENABLED)) {
		if (sc->sc_enable != NULL && (*sc->sc_enable)(sc) != 0) {
			printf("%s: could not enable device\n",
			    sc->sc_dev.dv_xname);
			return EIO;
		}
		sc->sc_flags |= RT2560_ENABLED;
	}

	rt2560_stop(ifp, 0);

	/* setup tx rings */
	tmp = RT2560_PRIO_RING_COUNT << 24 |
	      RT2560_ATIM_RING_COUNT << 16 |
	      RT2560_TX_RING_COUNT   <<  8 |
	      RT2560_TX_DESC_SIZE;

	/* rings _must_ be initialized in this _exact_ order! */
	RAL_WRITE(sc, RT2560_TXCSR2, tmp);
	RAL_WRITE(sc, RT2560_TXCSR3, sc->txq.physaddr);
	RAL_WRITE(sc, RT2560_TXCSR5, sc->prioq.physaddr);
	RAL_WRITE(sc, RT2560_TXCSR4, sc->atimq.physaddr);
	RAL_WRITE(sc, RT2560_TXCSR6, sc->bcnq.physaddr);

	/* setup rx ring */
	tmp = RT2560_RX_RING_COUNT << 8 | RT2560_RX_DESC_SIZE;

	RAL_WRITE(sc, RT2560_RXCSR1, tmp);
	RAL_WRITE(sc, RT2560_RXCSR2, sc->rxq.physaddr);

	/* initialize MAC registers to default values */
	for (i = 0; i < nitems(rt2560_def_mac); i++)
		RAL_WRITE(sc, rt2560_def_mac[i].reg, rt2560_def_mac[i].val);

	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	rt2560_set_macaddr(sc, ic->ic_myaddr);

	/* set basic rate set (will be updated later) */
	RAL_WRITE(sc, RT2560_ARSP_PLCP_1, 0x153);

	rt2560_set_slottime(sc);
	rt2560_update_plcp(sc);
	rt2560_update_led(sc, 0, 0);

	RAL_WRITE(sc, RT2560_CSR1, RT2560_RESET_ASIC);
	RAL_WRITE(sc, RT2560_CSR1, RT2560_HOST_READY);

	if (rt2560_bbp_init(sc) != 0) {
		rt2560_stop(ifp, 1);
		return EIO;
	}

	rt2560_set_txantenna(sc, 1);
	rt2560_set_rxantenna(sc, 1);

	/* set default BSS channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	rt2560_set_chan(sc, ic->ic_bss->ni_chan);

	/* kick Rx */
	tmp = RT2560_DROP_PHY_ERROR | RT2560_DROP_CRC_ERROR;
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RT2560_DROP_CTL | RT2560_DROP_VERSION_ERROR;
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode != IEEE80211_M_HOSTAP)
#endif
			tmp |= RT2560_DROP_TODS;
		if (!(ifp->if_flags & IFF_PROMISC))
			tmp |= RT2560_DROP_NOT_TO_ME;
	}
	RAL_WRITE(sc, RT2560_RXCSR0, tmp);

	/* clear old FCS and Rx FIFO errors */
	RAL_READ(sc, RT2560_CNT0);
	RAL_READ(sc, RT2560_CNT4);

	/* clear any pending interrupts */
	RAL_WRITE(sc, RT2560_CSR7, 0xffffffff);

	/* enable interrupts */
	RAL_WRITE(sc, RT2560_CSR8, RT2560_INTR_MASK);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return 0;
}

void
rt2560_stop(struct ifnet *ifp, int disable)
{
	struct rt2560_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	sc->sc_tx_timer = 0;
	sc->sc_flags &= ~(RT2560_PRIO_OACTIVE|RT2560_DATA_OACTIVE);
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);	/* free all nodes */

	/* abort Tx */
	RAL_WRITE(sc, RT2560_TXCSR0, RT2560_ABORT_TX);

	/* disable Rx */
	RAL_WRITE(sc, RT2560_RXCSR0, RT2560_DISABLE_RX);

	/* reset ASIC (and thus, BBP) */
	RAL_WRITE(sc, RT2560_CSR1, RT2560_RESET_ASIC);
	RAL_WRITE(sc, RT2560_CSR1, 0);

	/* disable interrupts */
	RAL_WRITE(sc, RT2560_CSR8, 0xffffffff);

	/* clear any pending interrupt */
	RAL_WRITE(sc, RT2560_CSR7, 0xffffffff);

	/* reset Tx and Rx rings */
	rt2560_reset_tx_ring(sc, &sc->txq);
	rt2560_reset_tx_ring(sc, &sc->atimq);
	rt2560_reset_tx_ring(sc, &sc->prioq);
	rt2560_reset_tx_ring(sc, &sc->bcnq);
	rt2560_reset_rx_ring(sc, &sc->rxq);

	/* for CardBus, power down the socket */
	if (disable && sc->sc_disable != NULL) {
		if (sc->sc_flags & RT2560_ENABLED) {
			(*sc->sc_disable)(sc);
			sc->sc_flags &= ~RT2560_ENABLED;
		}
	}
}

struct cfdriver ral_cd = {
	NULL, "ral", DV_IFNET
};

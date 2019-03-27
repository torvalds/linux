/*	$FreeBSD$	*/

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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * Ralink Technology RT2560 chipset driver
 * http://www.ralinktech.com/
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_ratectl.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#include <dev/ral/rt2560reg.h>
#include <dev/ral/rt2560var.h>

#define RT2560_RSSI(sc, rssi)					\
	((rssi) > (RT2560_NOISE_FLOOR + (sc)->rssi_corr) ?	\
	 ((rssi) - RT2560_NOISE_FLOOR - (sc)->rssi_corr) : 0)

#define RAL_DEBUG
#ifdef RAL_DEBUG
#define DPRINTF(sc, fmt, ...) do {				\
	if (sc->sc_debug > 0)					\
		printf(fmt, __VA_ARGS__);			\
} while (0)
#define DPRINTFN(sc, n, fmt, ...) do {				\
	if (sc->sc_debug >= (n))				\
		printf(fmt, __VA_ARGS__);			\
} while (0)
#else
#define DPRINTF(sc, fmt, ...)
#define DPRINTFN(sc, n, fmt, ...)
#endif

static struct ieee80211vap *rt2560_vap_create(struct ieee80211com *,
			    const char [IFNAMSIZ], int, enum ieee80211_opmode,
			    int, const uint8_t [IEEE80211_ADDR_LEN],
			    const uint8_t [IEEE80211_ADDR_LEN]);
static void		rt2560_vap_delete(struct ieee80211vap *);
static void		rt2560_dma_map_addr(void *, bus_dma_segment_t *, int,
			    int);
static int		rt2560_alloc_tx_ring(struct rt2560_softc *,
			    struct rt2560_tx_ring *, int);
static void		rt2560_reset_tx_ring(struct rt2560_softc *,
			    struct rt2560_tx_ring *);
static void		rt2560_free_tx_ring(struct rt2560_softc *,
			    struct rt2560_tx_ring *);
static int		rt2560_alloc_rx_ring(struct rt2560_softc *,
			    struct rt2560_rx_ring *, int);
static void		rt2560_reset_rx_ring(struct rt2560_softc *,
			    struct rt2560_rx_ring *);
static void		rt2560_free_rx_ring(struct rt2560_softc *,
			    struct rt2560_rx_ring *);
static int		rt2560_newstate(struct ieee80211vap *,
			    enum ieee80211_state, int);
static uint16_t		rt2560_eeprom_read(struct rt2560_softc *, uint8_t);
static void		rt2560_encryption_intr(struct rt2560_softc *);
static void		rt2560_tx_intr(struct rt2560_softc *);
static void		rt2560_prio_intr(struct rt2560_softc *);
static void		rt2560_decryption_intr(struct rt2560_softc *);
static void		rt2560_rx_intr(struct rt2560_softc *);
static void		rt2560_beacon_update(struct ieee80211vap *, int item);
static void		rt2560_beacon_expire(struct rt2560_softc *);
static void		rt2560_wakeup_expire(struct rt2560_softc *);
static void		rt2560_scan_start(struct ieee80211com *);
static void		rt2560_scan_end(struct ieee80211com *);
static void		rt2560_getradiocaps(struct ieee80211com *, int, int *,
			    struct ieee80211_channel[]);
static void		rt2560_set_channel(struct ieee80211com *);
static void		rt2560_setup_tx_desc(struct rt2560_softc *,
			    struct rt2560_tx_desc *, uint32_t, int, int, int,
			    bus_addr_t);
static int		rt2560_tx_bcn(struct rt2560_softc *, struct mbuf *,
			    struct ieee80211_node *);
static int		rt2560_tx_mgt(struct rt2560_softc *, struct mbuf *,
			    struct ieee80211_node *);
static int		rt2560_tx_data(struct rt2560_softc *, struct mbuf *,
			    struct ieee80211_node *);
static int		rt2560_transmit(struct ieee80211com *, struct mbuf *);
static void		rt2560_start(struct rt2560_softc *);
static void		rt2560_watchdog(void *);
static void		rt2560_parent(struct ieee80211com *);
static void		rt2560_bbp_write(struct rt2560_softc *, uint8_t,
			    uint8_t);
static uint8_t		rt2560_bbp_read(struct rt2560_softc *, uint8_t);
static void		rt2560_rf_write(struct rt2560_softc *, uint8_t,
			    uint32_t);
static void		rt2560_set_chan(struct rt2560_softc *,
			    struct ieee80211_channel *);
#if 0
static void		rt2560_disable_rf_tune(struct rt2560_softc *);
#endif
static void		rt2560_enable_tsf_sync(struct rt2560_softc *);
static void		rt2560_enable_tsf(struct rt2560_softc *);
static void		rt2560_update_plcp(struct rt2560_softc *);
static void		rt2560_update_slot(struct ieee80211com *);
static void		rt2560_set_basicrates(struct rt2560_softc *,
			    const struct ieee80211_rateset *);
static void		rt2560_update_led(struct rt2560_softc *, int, int);
static void		rt2560_set_bssid(struct rt2560_softc *, const uint8_t *);
static void		rt2560_set_macaddr(struct rt2560_softc *,
			    const uint8_t *);
static void		rt2560_get_macaddr(struct rt2560_softc *, uint8_t *);
static void		rt2560_update_promisc(struct ieee80211com *);
static const char	*rt2560_get_rf(int);
static void		rt2560_read_config(struct rt2560_softc *);
static int		rt2560_bbp_init(struct rt2560_softc *);
static void		rt2560_set_txantenna(struct rt2560_softc *, int);
static void		rt2560_set_rxantenna(struct rt2560_softc *, int);
static void		rt2560_init_locked(struct rt2560_softc *);
static void		rt2560_init(void *);
static void		rt2560_stop_locked(struct rt2560_softc *);
static int		rt2560_raw_xmit(struct ieee80211_node *, struct mbuf *,
				const struct ieee80211_bpf_params *);

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

static const uint8_t rt2560_chan_5ghz[] =
	{ 36, 40, 44, 48, 52, 56, 60, 64,
	  100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
	  149, 153, 157, 161 };

static const struct {
	uint8_t		chan;
	uint32_t	r1, r2, r4;
} rt2560_rf5222[] = {
	RT2560_RF5222
};

int
rt2560_attach(device_t dev, int id)
{
	struct rt2560_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

	sc->sc_dev = dev;

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF | MTX_RECURSE);

	callout_init_mtx(&sc->watchdog_ch, &sc->sc_mtx, 0);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	/* retrieve RT2560 rev. no */
	sc->asic_rev = RAL_READ(sc, RT2560_CSR0);

	/* retrieve RF rev. no and various other things from EEPROM */
	rt2560_read_config(sc);

	device_printf(dev, "MAC/BBP RT2560 (rev 0x%02x), RF %s\n",
	    sc->asic_rev, rt2560_get_rf(sc->rf_rev));

	/*
	 * Allocate Tx and Rx rings.
	 */
	error = rt2560_alloc_tx_ring(sc, &sc->txq, RT2560_TX_RING_COUNT);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate Tx ring\n");
		goto fail1;
	}

	error = rt2560_alloc_tx_ring(sc, &sc->atimq, RT2560_ATIM_RING_COUNT);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate ATIM ring\n");
		goto fail2;
	}

	error = rt2560_alloc_tx_ring(sc, &sc->prioq, RT2560_PRIO_RING_COUNT);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate Prio ring\n");
		goto fail3;
	}

	error = rt2560_alloc_tx_ring(sc, &sc->bcnq, RT2560_BEACON_RING_COUNT);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate Beacon ring\n");
		goto fail4;
	}

	error = rt2560_alloc_rx_ring(sc, &sc->rxq, RT2560_RX_RING_COUNT);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate Rx ring\n");
		goto fail5;
	}

	/* retrieve MAC address */
	rt2560_get_macaddr(sc, ic->ic_macaddr);

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(dev);
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_phytype = IEEE80211_T_OFDM; /* not only, but not used */

	/* set device capabilities */
	ic->ic_caps =
		  IEEE80211_C_STA		/* station mode */
		| IEEE80211_C_IBSS		/* ibss, nee adhoc, mode */
		| IEEE80211_C_HOSTAP		/* hostap mode */
		| IEEE80211_C_MONITOR		/* monitor mode */
		| IEEE80211_C_AHDEMO		/* adhoc demo mode */
		| IEEE80211_C_WDS		/* 4-address traffic works */
		| IEEE80211_C_MBSS		/* mesh point link mode */
		| IEEE80211_C_SHPREAMBLE	/* short preamble supported */
		| IEEE80211_C_SHSLOT		/* short slot time supported */
		| IEEE80211_C_WPA		/* capable of WPA1+WPA2 */
		| IEEE80211_C_BGSCAN		/* capable of bg scanning */
#ifdef notyet
		| IEEE80211_C_TXFRAG		/* handle tx frags */
#endif
		;

	rt2560_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	ieee80211_ifattach(ic);
	ic->ic_raw_xmit = rt2560_raw_xmit;
	ic->ic_updateslot = rt2560_update_slot;
	ic->ic_update_promisc = rt2560_update_promisc;
	ic->ic_scan_start = rt2560_scan_start;
	ic->ic_scan_end = rt2560_scan_end;
	ic->ic_getradiocaps = rt2560_getradiocaps;
	ic->ic_set_channel = rt2560_set_channel;

	ic->ic_vap_create = rt2560_vap_create;
	ic->ic_vap_delete = rt2560_vap_delete;
	ic->ic_parent = rt2560_parent;
	ic->ic_transmit = rt2560_transmit;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_txtap.wt_ihdr, sizeof(sc->sc_txtap),
		RT2560_TX_RADIOTAP_PRESENT,
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
		RT2560_RX_RADIOTAP_PRESENT);

	/*
	 * Add a few sysctl knobs.
	 */
#ifdef RAL_DEBUG
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "debug", CTLFLAG_RW, &sc->sc_debug, 0, "debug msgs");
#endif
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "txantenna", CTLFLAG_RW, &sc->tx_ant, 0, "tx antenna (0=auto)");

	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "rxantenna", CTLFLAG_RW, &sc->rx_ant, 0, "rx antenna (0=auto)");

	if (bootverbose)
		ieee80211_announce(ic);

	return 0;

fail5:	rt2560_free_tx_ring(sc, &sc->bcnq);
fail4:	rt2560_free_tx_ring(sc, &sc->prioq);
fail3:	rt2560_free_tx_ring(sc, &sc->atimq);
fail2:	rt2560_free_tx_ring(sc, &sc->txq);
fail1:	mtx_destroy(&sc->sc_mtx);

	return ENXIO;
}

int
rt2560_detach(void *xsc)
{
	struct rt2560_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	
	rt2560_stop(sc);

	ieee80211_ifdetach(ic);
	mbufq_drain(&sc->sc_snd);

	rt2560_free_tx_ring(sc, &sc->txq);
	rt2560_free_tx_ring(sc, &sc->atimq);
	rt2560_free_tx_ring(sc, &sc->prioq);
	rt2560_free_tx_ring(sc, &sc->bcnq);
	rt2560_free_rx_ring(sc, &sc->rxq);

	mtx_destroy(&sc->sc_mtx);

	return 0;
}

static struct ieee80211vap *
rt2560_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct rt2560_softc *sc = ic->ic_softc;
	struct rt2560_vap *rvp;
	struct ieee80211vap *vap;

	switch (opmode) {
	case IEEE80211_M_STA:
	case IEEE80211_M_IBSS:
	case IEEE80211_M_AHDEMO:
	case IEEE80211_M_MONITOR:
	case IEEE80211_M_HOSTAP:
	case IEEE80211_M_MBSS:
		/* XXXRP: TBD */
		if (!TAILQ_EMPTY(&ic->ic_vaps)) {
			device_printf(sc->sc_dev, "only 1 vap supported\n");
			return NULL;
		}
		if (opmode == IEEE80211_M_STA)
			flags |= IEEE80211_CLONE_NOBEACONS;
		break;
	case IEEE80211_M_WDS:
		if (TAILQ_EMPTY(&ic->ic_vaps) ||
		    ic->ic_opmode != IEEE80211_M_HOSTAP) {
			device_printf(sc->sc_dev,
			    "wds only supported in ap mode\n");
			return NULL;
		}
		/*
		 * Silently remove any request for a unique
		 * bssid; WDS vap's always share the local
		 * mac address.
		 */
		flags &= ~IEEE80211_CLONE_BSSID;
		break;
	default:
		device_printf(sc->sc_dev, "unknown opmode %d\n", opmode);
		return NULL;
	}
	rvp = malloc(sizeof(struct rt2560_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &rvp->ral_vap;
	ieee80211_vap_setup(ic, vap, name, unit, opmode, flags, bssid);

	/* override state transition machine */
	rvp->ral_newstate = vap->iv_newstate;
	vap->iv_newstate = rt2560_newstate;
	vap->iv_update_beacon = rt2560_beacon_update;

	ieee80211_ratectl_init(vap);
	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change,
	    ieee80211_media_status, mac);
	if (TAILQ_FIRST(&ic->ic_vaps) == vap)
		ic->ic_opmode = opmode;
	return vap;
}

static void
rt2560_vap_delete(struct ieee80211vap *vap)
{
	struct rt2560_vap *rvp = RT2560_VAP(vap);

	ieee80211_ratectl_deinit(vap);
	ieee80211_vap_detach(vap);
	free(rvp, M_80211_VAP);
}

void
rt2560_resume(void *xsc)
{
	struct rt2560_softc *sc = xsc;

	if (sc->sc_ic.ic_nrunning > 0)
		rt2560_init(sc);
}

static void
rt2560_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	if (error != 0)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));

	*(bus_addr_t *)arg = segs[0].ds_addr;
}

static int
rt2560_alloc_tx_ring(struct rt2560_softc *sc, struct rt2560_tx_ring *ring,
    int count)
{
	int i, error;

	ring->count = count;
	ring->queued = 0;
	ring->cur = ring->next = 0;
	ring->cur_encrypt = ring->next_encrypt = 0;

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 4, 0, 
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    count * RT2560_TX_DESC_SIZE, 1, count * RT2560_TX_DESC_SIZE,
	    0, NULL, NULL, &ring->desc_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create desc DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(ring->desc_dmat, (void **)&ring->desc,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &ring->desc_map);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(ring->desc_dmat, ring->desc_map, ring->desc,
	    count * RT2560_TX_DESC_SIZE, rt2560_dma_map_addr, &ring->physaddr,
	    0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not load desc DMA map\n");
		goto fail;
	}

	ring->data = malloc(count * sizeof (struct rt2560_tx_data), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (ring->data == NULL) {
		device_printf(sc->sc_dev, "could not allocate soft data\n");
		error = ENOMEM;
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0, 
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    MCLBYTES, RT2560_MAX_SCATTER, MCLBYTES, 0, NULL, NULL,
	    &ring->data_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create data DMA tag\n");
		goto fail;
	}

	for (i = 0; i < count; i++) {
		error = bus_dmamap_create(ring->data_dmat, 0,
		    &ring->data[i].map);
		if (error != 0) {
			device_printf(sc->sc_dev, "could not create DMA map\n");
			goto fail;
		}
	}

	return 0;

fail:	rt2560_free_tx_ring(sc, ring);
	return error;
}

static void
rt2560_reset_tx_ring(struct rt2560_softc *sc, struct rt2560_tx_ring *ring)
{
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	int i;

	for (i = 0; i < ring->count; i++) {
		desc = &ring->desc[i];
		data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(ring->data_dmat, data->map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ring->data_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}

		if (data->ni != NULL) {
			ieee80211_free_node(data->ni);
			data->ni = NULL;
		}

		desc->flags = 0;
	}

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map, BUS_DMASYNC_PREWRITE);

	ring->queued = 0;
	ring->cur = ring->next = 0;
	ring->cur_encrypt = ring->next_encrypt = 0;
}

static void
rt2560_free_tx_ring(struct rt2560_softc *sc, struct rt2560_tx_ring *ring)
{
	struct rt2560_tx_data *data;
	int i;

	if (ring->desc != NULL) {
		bus_dmamap_sync(ring->desc_dmat, ring->desc_map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ring->desc_dmat, ring->desc_map);
		bus_dmamem_free(ring->desc_dmat, ring->desc, ring->desc_map);
	}

	if (ring->desc_dmat != NULL)
		bus_dma_tag_destroy(ring->desc_dmat);

	if (ring->data != NULL) {
		for (i = 0; i < ring->count; i++) {
			data = &ring->data[i];

			if (data->m != NULL) {
				bus_dmamap_sync(ring->data_dmat, data->map,
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(ring->data_dmat, data->map);
				m_freem(data->m);
			}

			if (data->ni != NULL)
				ieee80211_free_node(data->ni);

			if (data->map != NULL)
				bus_dmamap_destroy(ring->data_dmat, data->map);
		}

		free(ring->data, M_DEVBUF);
	}

	if (ring->data_dmat != NULL)
		bus_dma_tag_destroy(ring->data_dmat);
}

static int
rt2560_alloc_rx_ring(struct rt2560_softc *sc, struct rt2560_rx_ring *ring,
    int count)
{
	struct rt2560_rx_desc *desc;
	struct rt2560_rx_data *data;
	bus_addr_t physaddr;
	int i, error;

	ring->count = count;
	ring->cur = ring->next = 0;
	ring->cur_decrypt = 0;

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 4, 0, 
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    count * RT2560_RX_DESC_SIZE, 1, count * RT2560_RX_DESC_SIZE,
	    0, NULL, NULL, &ring->desc_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create desc DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(ring->desc_dmat, (void **)&ring->desc,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &ring->desc_map);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(ring->desc_dmat, ring->desc_map, ring->desc,
	    count * RT2560_RX_DESC_SIZE, rt2560_dma_map_addr, &ring->physaddr,
	    0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not load desc DMA map\n");
		goto fail;
	}

	ring->data = malloc(count * sizeof (struct rt2560_rx_data), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (ring->data == NULL) {
		device_printf(sc->sc_dev, "could not allocate soft data\n");
		error = ENOMEM;
		goto fail;
	}

	/*
	 * Pre-allocate Rx buffers and populate Rx ring.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0, 
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES,
	    1, MCLBYTES, 0, NULL, NULL, &ring->data_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create data DMA tag\n");
		goto fail;
	}

	for (i = 0; i < count; i++) {
		desc = &sc->rxq.desc[i];
		data = &sc->rxq.data[i];

		error = bus_dmamap_create(ring->data_dmat, 0, &data->map);
		if (error != 0) {
			device_printf(sc->sc_dev, "could not create DMA map\n");
			goto fail;
		}

		data->m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (data->m == NULL) {
			device_printf(sc->sc_dev,
			    "could not allocate rx mbuf\n");
			error = ENOMEM;
			goto fail;
		}

		error = bus_dmamap_load(ring->data_dmat, data->map,
		    mtod(data->m, void *), MCLBYTES, rt2560_dma_map_addr,
		    &physaddr, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not load rx buf DMA map");
			goto fail;
		}

		desc->flags = htole32(RT2560_RX_BUSY);
		desc->physaddr = htole32(physaddr);
	}

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map, BUS_DMASYNC_PREWRITE);

	return 0;

fail:	rt2560_free_rx_ring(sc, ring);
	return error;
}

static void
rt2560_reset_rx_ring(struct rt2560_softc *sc, struct rt2560_rx_ring *ring)
{
	int i;

	for (i = 0; i < ring->count; i++) {
		ring->desc[i].flags = htole32(RT2560_RX_BUSY);
		ring->data[i].drop = 0;
	}

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map, BUS_DMASYNC_PREWRITE);

	ring->cur = ring->next = 0;
	ring->cur_decrypt = 0;
}

static void
rt2560_free_rx_ring(struct rt2560_softc *sc, struct rt2560_rx_ring *ring)
{
	struct rt2560_rx_data *data;
	int i;

	if (ring->desc != NULL) {
		bus_dmamap_sync(ring->desc_dmat, ring->desc_map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ring->desc_dmat, ring->desc_map);
		bus_dmamem_free(ring->desc_dmat, ring->desc, ring->desc_map);
	}

	if (ring->desc_dmat != NULL)
		bus_dma_tag_destroy(ring->desc_dmat);

	if (ring->data != NULL) {
		for (i = 0; i < ring->count; i++) {
			data = &ring->data[i];

			if (data->m != NULL) {
				bus_dmamap_sync(ring->data_dmat, data->map,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(ring->data_dmat, data->map);
				m_freem(data->m);
			}

			if (data->map != NULL)
				bus_dmamap_destroy(ring->data_dmat, data->map);
		}

		free(ring->data, M_DEVBUF);
	}

	if (ring->data_dmat != NULL)
		bus_dma_tag_destroy(ring->data_dmat);
}

static int
rt2560_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct rt2560_vap *rvp = RT2560_VAP(vap);
	struct rt2560_softc *sc = vap->iv_ic->ic_softc;
	int error;

	if (nstate == IEEE80211_S_INIT && vap->iv_state == IEEE80211_S_RUN) {
		/* abort TSF synchronization */
		RAL_WRITE(sc, RT2560_CSR14, 0);

		/* turn association led off */
		rt2560_update_led(sc, 0, 0);
	}

	error = rvp->ral_newstate(vap, nstate, arg);

	if (error == 0 && nstate == IEEE80211_S_RUN) {
		struct ieee80211_node *ni = vap->iv_bss;
		struct mbuf *m;

		if (vap->iv_opmode != IEEE80211_M_MONITOR) {
			rt2560_update_plcp(sc);
			rt2560_set_basicrates(sc, &ni->ni_rates);
			rt2560_set_bssid(sc, ni->ni_bssid);
		}

		if (vap->iv_opmode == IEEE80211_M_HOSTAP ||
		    vap->iv_opmode == IEEE80211_M_IBSS ||
		    vap->iv_opmode == IEEE80211_M_MBSS) {
			m = ieee80211_beacon_alloc(ni);
			if (m == NULL) {
				device_printf(sc->sc_dev,
				    "could not allocate beacon\n");
				return ENOBUFS;
			}
			ieee80211_ref_node(ni);
			error = rt2560_tx_bcn(sc, m, ni);
			if (error != 0)
				return error;
		}

		/* turn association led on */
		rt2560_update_led(sc, 1, 0);

		if (vap->iv_opmode != IEEE80211_M_MONITOR)
			rt2560_enable_tsf_sync(sc);
		else
			rt2560_enable_tsf(sc);
	}
	return error;
}

/*
 * Read 16 bits at address 'addr' from the serial EEPROM (either 93C46 or
 * 93C66).
 */
static uint16_t
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
static void
rt2560_encryption_intr(struct rt2560_softc *sc)
{
	struct rt2560_tx_desc *desc;
	int hw;

	/* retrieve last descriptor index processed by cipher engine */
	hw = RAL_READ(sc, RT2560_SECCSR1) - sc->txq.physaddr;
	hw /= RT2560_TX_DESC_SIZE;

	bus_dmamap_sync(sc->txq.desc_dmat, sc->txq.desc_map,
	    BUS_DMASYNC_POSTREAD);

	while (sc->txq.next_encrypt != hw) {
		if (sc->txq.next_encrypt == sc->txq.cur_encrypt) {
			printf("hw encrypt %d, cur_encrypt %d\n", hw,
			    sc->txq.cur_encrypt);
			break;
		}

		desc = &sc->txq.desc[sc->txq.next_encrypt];

		if ((le32toh(desc->flags) & RT2560_TX_BUSY) ||
		    (le32toh(desc->flags) & RT2560_TX_CIPHER_BUSY))
			break;

		/* for TKIP, swap eiv field to fix a bug in ASIC */
		if ((le32toh(desc->flags) & RT2560_TX_CIPHER_MASK) ==
		    RT2560_TX_CIPHER_TKIP)
			desc->eiv = bswap32(desc->eiv);

		/* mark the frame ready for transmission */
		desc->flags |= htole32(RT2560_TX_VALID);
		desc->flags |= htole32(RT2560_TX_BUSY);

		DPRINTFN(sc, 15, "encryption done idx=%u\n",
		    sc->txq.next_encrypt);

		sc->txq.next_encrypt =
		    (sc->txq.next_encrypt + 1) % RT2560_TX_RING_COUNT;
	}

	bus_dmamap_sync(sc->txq.desc_dmat, sc->txq.desc_map,
	    BUS_DMASYNC_PREWRITE);

	/* kick Tx */
	RAL_WRITE(sc, RT2560_TXCSR0, RT2560_KICK_TX);
}

static void
rt2560_tx_intr(struct rt2560_softc *sc)
{
	struct ieee80211_ratectl_tx_status *txs = &sc->sc_txs;
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	struct mbuf *m;
	struct ieee80211_node *ni;
	uint32_t flags;
	int status;

	bus_dmamap_sync(sc->txq.desc_dmat, sc->txq.desc_map,
	    BUS_DMASYNC_POSTREAD);

	txs->flags = IEEE80211_RATECTL_STATUS_LONG_RETRY;
	for (;;) {
		desc = &sc->txq.desc[sc->txq.next];
		data = &sc->txq.data[sc->txq.next];

		flags = le32toh(desc->flags);
		if ((flags & RT2560_TX_BUSY) ||
		    (flags & RT2560_TX_CIPHER_BUSY) ||
		    !(flags & RT2560_TX_VALID))
			break;

		m = data->m;
		ni = data->ni;

		switch (flags & RT2560_TX_RESULT_MASK) {
		case RT2560_TX_SUCCESS:
			txs->status = IEEE80211_RATECTL_TX_SUCCESS;
			txs->long_retries = 0;

			DPRINTFN(sc, 10, "%s\n", "data frame sent successfully");
			if (data->rix != IEEE80211_FIXED_RATE_NONE)
				ieee80211_ratectl_tx_complete(ni, txs);
			status = 0;
			break;

		case RT2560_TX_SUCCESS_RETRY:
			txs->status = IEEE80211_RATECTL_TX_SUCCESS;
			txs->long_retries = RT2560_TX_RETRYCNT(flags);

			DPRINTFN(sc, 9, "data frame sent after %u retries\n",
			    txs->long_retries);
			if (data->rix != IEEE80211_FIXED_RATE_NONE)
				ieee80211_ratectl_tx_complete(ni, txs);
			status = 0;
			break;

		case RT2560_TX_FAIL_RETRY:
			txs->status = IEEE80211_RATECTL_TX_FAIL_LONG;
			txs->long_retries = RT2560_TX_RETRYCNT(flags);

			DPRINTFN(sc, 9, "data frame failed after %d retries\n",
			    txs->long_retries);
			if (data->rix != IEEE80211_FIXED_RATE_NONE)
				ieee80211_ratectl_tx_complete(ni, txs);
			status = 1;
			break;

		case RT2560_TX_FAIL_INVALID:
		case RT2560_TX_FAIL_OTHER:
		default:
			device_printf(sc->sc_dev, "sending data frame failed "
			    "0x%08x\n", flags);
			status = 1;
		}

		bus_dmamap_sync(sc->txq.data_dmat, data->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->txq.data_dmat, data->map);

		ieee80211_tx_complete(ni, m, status);
		data->ni = NULL;
		data->m = NULL;

		/* descriptor is no longer valid */
		desc->flags &= ~htole32(RT2560_TX_VALID);

		DPRINTFN(sc, 15, "tx done idx=%u\n", sc->txq.next);

		sc->txq.queued--;
		sc->txq.next = (sc->txq.next + 1) % RT2560_TX_RING_COUNT;
	}

	bus_dmamap_sync(sc->txq.desc_dmat, sc->txq.desc_map,
	    BUS_DMASYNC_PREWRITE);

	if (sc->prioq.queued == 0 && sc->txq.queued == 0)
		sc->sc_tx_timer = 0;

	if (sc->txq.queued < RT2560_TX_RING_COUNT - 1)
		rt2560_start(sc);
}

static void
rt2560_prio_intr(struct rt2560_softc *sc)
{
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	struct ieee80211_node *ni;
	struct mbuf *m;
	int flags;

	bus_dmamap_sync(sc->prioq.desc_dmat, sc->prioq.desc_map,
	    BUS_DMASYNC_POSTREAD);

	for (;;) {
		desc = &sc->prioq.desc[sc->prioq.next];
		data = &sc->prioq.data[sc->prioq.next];

		flags = le32toh(desc->flags);
		if ((flags & RT2560_TX_BUSY) || (flags & RT2560_TX_VALID) == 0)
			break;

		switch (flags & RT2560_TX_RESULT_MASK) {
		case RT2560_TX_SUCCESS:
			DPRINTFN(sc, 10, "%s\n", "mgt frame sent successfully");
			break;

		case RT2560_TX_SUCCESS_RETRY:
			DPRINTFN(sc, 9, "mgt frame sent after %u retries\n",
			    (flags >> 5) & 0x7);
			break;

		case RT2560_TX_FAIL_RETRY:
			DPRINTFN(sc, 9, "%s\n",
			    "sending mgt frame failed (too much retries)");
			break;

		case RT2560_TX_FAIL_INVALID:
		case RT2560_TX_FAIL_OTHER:
		default:
			device_printf(sc->sc_dev, "sending mgt frame failed "
			    "0x%08x\n", flags);
			break;
		}

		bus_dmamap_sync(sc->prioq.data_dmat, data->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->prioq.data_dmat, data->map);

		m = data->m;
		data->m = NULL;
		ni = data->ni;
		data->ni = NULL;

		/* descriptor is no longer valid */
		desc->flags &= ~htole32(RT2560_TX_VALID);

		DPRINTFN(sc, 15, "prio done idx=%u\n", sc->prioq.next);

		sc->prioq.queued--;
		sc->prioq.next = (sc->prioq.next + 1) % RT2560_PRIO_RING_COUNT;

		if (m->m_flags & M_TXCB)
			ieee80211_process_callback(ni, m,
				(flags & RT2560_TX_RESULT_MASK) &~
				(RT2560_TX_SUCCESS | RT2560_TX_SUCCESS_RETRY));
		m_freem(m);
		ieee80211_free_node(ni);
	}

	bus_dmamap_sync(sc->prioq.desc_dmat, sc->prioq.desc_map,
	    BUS_DMASYNC_PREWRITE);

	if (sc->prioq.queued == 0 && sc->txq.queued == 0)
		sc->sc_tx_timer = 0;

	if (sc->prioq.queued < RT2560_PRIO_RING_COUNT)
		rt2560_start(sc);
}

/*
 * Some frames were processed by the hardware cipher engine and are ready for
 * handoff to the IEEE802.11 layer.
 */
static void
rt2560_decryption_intr(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2560_rx_desc *desc;
	struct rt2560_rx_data *data;
	bus_addr_t physaddr;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct mbuf *mnew, *m;
	int hw, error;
	int8_t rssi, nf;

	/* retrieve last descriptor index processed by cipher engine */
	hw = RAL_READ(sc, RT2560_SECCSR0) - sc->rxq.physaddr;
	hw /= RT2560_RX_DESC_SIZE;

	bus_dmamap_sync(sc->rxq.desc_dmat, sc->rxq.desc_map,
	    BUS_DMASYNC_POSTREAD);

	for (; sc->rxq.cur_decrypt != hw;) {
		desc = &sc->rxq.desc[sc->rxq.cur_decrypt];
		data = &sc->rxq.data[sc->rxq.cur_decrypt];

		if ((le32toh(desc->flags) & RT2560_RX_BUSY) ||
		    (le32toh(desc->flags) & RT2560_RX_CIPHER_BUSY))
			break;

		if (data->drop) {
			counter_u64_add(ic->ic_ierrors, 1);
			goto skip;
		}

		if ((le32toh(desc->flags) & RT2560_RX_CIPHER_MASK) != 0 &&
		    (le32toh(desc->flags) & RT2560_RX_ICV_ERROR)) {
			counter_u64_add(ic->ic_ierrors, 1);
			goto skip;
		}

		/*
		 * Try to allocate a new mbuf for this ring element and load it
		 * before processing the current mbuf. If the ring element
		 * cannot be loaded, drop the received packet and reuse the old
		 * mbuf. In the unlikely case that the old mbuf can't be
		 * reloaded either, explicitly panic.
		 */
		mnew = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (mnew == NULL) {
			counter_u64_add(ic->ic_ierrors, 1);
			goto skip;
		}

		bus_dmamap_sync(sc->rxq.data_dmat, data->map,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->rxq.data_dmat, data->map);

		error = bus_dmamap_load(sc->rxq.data_dmat, data->map,
		    mtod(mnew, void *), MCLBYTES, rt2560_dma_map_addr,
		    &physaddr, 0);
		if (error != 0) {
			m_freem(mnew);

			/* try to reload the old mbuf */
			error = bus_dmamap_load(sc->rxq.data_dmat, data->map,
			    mtod(data->m, void *), MCLBYTES,
			    rt2560_dma_map_addr, &physaddr, 0);
			if (error != 0) {
				/* very unlikely that it will fail... */
				panic("%s: could not load old rx mbuf",
				    device_get_name(sc->sc_dev));
			}
			counter_u64_add(ic->ic_ierrors, 1);
			goto skip;
		}

		/*
	 	 * New mbuf successfully loaded, update Rx ring and continue
		 * processing.
		 */
		m = data->m;
		data->m = mnew;
		desc->physaddr = htole32(physaddr);

		/* finalize mbuf */
		m->m_pkthdr.len = m->m_len =
		    (le32toh(desc->flags) >> 16) & 0xfff;

		rssi = RT2560_RSSI(sc, desc->rssi);
		nf = RT2560_NOISE_FLOOR;
		if (ieee80211_radiotap_active(ic)) {
			struct rt2560_rx_radiotap_header *tap = &sc->sc_rxtap;
			uint32_t tsf_lo, tsf_hi;

			/* get timestamp (low and high 32 bits) */
			tsf_hi = RAL_READ(sc, RT2560_CSR17);
			tsf_lo = RAL_READ(sc, RT2560_CSR16);

			tap->wr_tsf =
			    htole64(((uint64_t)tsf_hi << 32) | tsf_lo);
			tap->wr_flags = 0;
			tap->wr_rate = ieee80211_plcp2rate(desc->rate,
			    (desc->flags & htole32(RT2560_RX_OFDM)) ?
				IEEE80211_T_OFDM : IEEE80211_T_CCK);
			tap->wr_antenna = sc->rx_ant;
			tap->wr_antsignal = nf + rssi;
			tap->wr_antnoise = nf;
		}

		sc->sc_flags |= RT2560_F_INPUT_RUNNING;
		RAL_UNLOCK(sc);
		wh = mtod(m, struct ieee80211_frame *);
		ni = ieee80211_find_rxnode(ic,
		    (struct ieee80211_frame_min *)wh);
		if (ni != NULL) {
			(void) ieee80211_input(ni, m, rssi, nf);
			ieee80211_free_node(ni);
		} else
			(void) ieee80211_input_all(ic, m, rssi, nf);

		RAL_LOCK(sc);
		sc->sc_flags &= ~RT2560_F_INPUT_RUNNING;
skip:		desc->flags = htole32(RT2560_RX_BUSY);

		DPRINTFN(sc, 15, "decryption done idx=%u\n", sc->rxq.cur_decrypt);

		sc->rxq.cur_decrypt =
		    (sc->rxq.cur_decrypt + 1) % RT2560_RX_RING_COUNT;
	}

	bus_dmamap_sync(sc->rxq.desc_dmat, sc->rxq.desc_map,
	    BUS_DMASYNC_PREWRITE);
}

/*
 * Some frames were received. Pass them to the hardware cipher engine before
 * sending them to the 802.11 layer.
 */
static void
rt2560_rx_intr(struct rt2560_softc *sc)
{
	struct rt2560_rx_desc *desc;
	struct rt2560_rx_data *data;

	bus_dmamap_sync(sc->rxq.desc_dmat, sc->rxq.desc_map,
	    BUS_DMASYNC_POSTREAD);

	for (;;) {
		desc = &sc->rxq.desc[sc->rxq.cur];
		data = &sc->rxq.data[sc->rxq.cur];

		if ((le32toh(desc->flags) & RT2560_RX_BUSY) ||
		    (le32toh(desc->flags) & RT2560_RX_CIPHER_BUSY))
			break;

		data->drop = 0;

		if ((le32toh(desc->flags) & RT2560_RX_PHY_ERROR) ||
		    (le32toh(desc->flags) & RT2560_RX_CRC_ERROR)) {
			/*
			 * This should not happen since we did not request
			 * to receive those frames when we filled RXCSR0.
			 */
			DPRINTFN(sc, 5, "PHY or CRC error flags 0x%08x\n",
			    le32toh(desc->flags));
			data->drop = 1;
		}

		if (((le32toh(desc->flags) >> 16) & 0xfff) > MCLBYTES) {
			DPRINTFN(sc, 5, "%s\n", "bad length");
			data->drop = 1;
		}

		/* mark the frame for decryption */
		desc->flags |= htole32(RT2560_RX_CIPHER_BUSY);

		DPRINTFN(sc, 15, "rx done idx=%u\n", sc->rxq.cur);

		sc->rxq.cur = (sc->rxq.cur + 1) % RT2560_RX_RING_COUNT;
	}

	bus_dmamap_sync(sc->rxq.desc_dmat, sc->rxq.desc_map,
	    BUS_DMASYNC_PREWRITE);

	/* kick decrypt */
	RAL_WRITE(sc, RT2560_SECCSR0, RT2560_KICK_DECRYPT);
}

static void
rt2560_beacon_update(struct ieee80211vap *vap, int item)
{
	struct ieee80211_beacon_offsets *bo = &vap->iv_bcn_off;

	setbit(bo->bo_flags, item);
}

/*
 * This function is called periodically in IBSS mode when a new beacon must be
 * sent out.
 */
static void
rt2560_beacon_expire(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2560_tx_data *data;

	if (ic->ic_opmode != IEEE80211_M_IBSS &&
	    ic->ic_opmode != IEEE80211_M_HOSTAP &&
	    ic->ic_opmode != IEEE80211_M_MBSS)
		return;	

	data = &sc->bcnq.data[sc->bcnq.next];
	/*
	 * Don't send beacon if bsschan isn't set
	 */
	if (data->ni == NULL)
	        return;

	bus_dmamap_sync(sc->bcnq.data_dmat, data->map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->bcnq.data_dmat, data->map);

	/* XXX 1 =>'s mcast frames which means all PS sta's will wakeup! */
	ieee80211_beacon_update(data->ni, data->m, 1);

	rt2560_tx_bcn(sc, data->m, data->ni);

	DPRINTFN(sc, 15, "%s", "beacon expired\n");

	sc->bcnq.next = (sc->bcnq.next + 1) % RT2560_BEACON_RING_COUNT;
}

/* ARGSUSED */
static void
rt2560_wakeup_expire(struct rt2560_softc *sc)
{
	DPRINTFN(sc, 2, "%s", "wakeup expired\n");
}

void
rt2560_intr(void *arg)
{
	struct rt2560_softc *sc = arg;
	uint32_t r;

	RAL_LOCK(sc);

	/* disable interrupts */
	RAL_WRITE(sc, RT2560_CSR8, 0xffffffff);

	/* don't re-enable interrupts if we're shutting down */
	if (!(sc->sc_flags & RT2560_F_RUNNING)) {
		RAL_UNLOCK(sc);
		return;
	}

	r = RAL_READ(sc, RT2560_CSR7);
	RAL_WRITE(sc, RT2560_CSR7, r);

	if (r & RT2560_BEACON_EXPIRE)
		rt2560_beacon_expire(sc);

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

	if (r & RT2560_RX_DONE) {
		rt2560_rx_intr(sc);
		rt2560_encryption_intr(sc);
	}

	/* re-enable interrupts */
	RAL_WRITE(sc, RT2560_CSR8, RT2560_INTR_MASK);

	RAL_UNLOCK(sc);
}

#define RAL_SIFS		10	/* us */

#define RT2560_TXRX_TURNAROUND	10	/* us */

static uint8_t
rt2560_plcp_signal(int rate)
{
	switch (rate) {
	/* OFDM rates (cf IEEE Std 802.11a-1999, pp. 14 Table 80) */
	case 12:	return 0xb;
	case 18:	return 0xf;
	case 24:	return 0xa;
	case 36:	return 0xe;
	case 48:	return 0x9;
	case 72:	return 0xd;
	case 96:	return 0x8;
	case 108:	return 0xc;

	/* CCK rates (NB: not IEEE std, device-specific) */
	case 2:		return 0x0;
	case 4:		return 0x1;
	case 11:	return 0x2;
	case 22:	return 0x3;
	}
	return 0xff;		/* XXX unsupported/unknown rate */
}

static void
rt2560_setup_tx_desc(struct rt2560_softc *sc, struct rt2560_tx_desc *desc,
    uint32_t flags, int len, int rate, int encrypt, bus_addr_t physaddr)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t plcp_length;
	int remainder;

	desc->flags = htole32(flags);
	desc->flags |= htole32(len << 16);

	desc->physaddr = htole32(physaddr);
	desc->wme = htole16(
	    RT2560_AIFSN(2) |
	    RT2560_LOGCWMIN(3) |
	    RT2560_LOGCWMAX(8));

	/* setup PLCP fields */
	desc->plcp_signal  = rt2560_plcp_signal(rate);
	desc->plcp_service = 4;

	len += IEEE80211_CRC_LEN;
	if (ieee80211_rate2phytype(ic->ic_rt, rate) == IEEE80211_T_OFDM) {
		desc->flags |= htole32(RT2560_TX_OFDM);

		plcp_length = len & 0xfff;
		desc->plcp_length_hi = plcp_length >> 6;
		desc->plcp_length_lo = plcp_length & 0x3f;
	} else {
		plcp_length = howmany(16 * len, rate);
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

	if (!encrypt)
		desc->flags |= htole32(RT2560_TX_VALID);
	desc->flags |= encrypt ? htole32(RT2560_TX_CIPHER_BUSY)
			       : htole32(RT2560_TX_BUSY);
}

static int
rt2560_tx_bcn(struct rt2560_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	bus_dma_segment_t segs[RT2560_MAX_SCATTER];
	int nsegs, rate, error;

	desc = &sc->bcnq.desc[sc->bcnq.cur];
	data = &sc->bcnq.data[sc->bcnq.cur];

	/* XXX maybe a separate beacon rate? */
	rate = vap->iv_txparms[ieee80211_chan2mode(ni->ni_chan)].mgmtrate;

	error = bus_dmamap_load_mbuf_sg(sc->bcnq.data_dmat, data->map, m0,
	    segs, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map mbuf (error %d)\n",
		    error);
		m_freem(m0);
		return error;
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct rt2560_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_antenna = sc->tx_ant;

		ieee80211_radiotap_tx(vap, m0);
	}

	data->m = m0;
	data->ni = ni;

	rt2560_setup_tx_desc(sc, desc, RT2560_TX_IFS_NEWBACKOFF |
	    RT2560_TX_TIMESTAMP, m0->m_pkthdr.len, rate, 0, segs->ds_addr);

	DPRINTFN(sc, 10, "sending beacon frame len=%u idx=%u rate=%u\n",
	    m0->m_pkthdr.len, sc->bcnq.cur, rate);

	bus_dmamap_sync(sc->bcnq.data_dmat, data->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->bcnq.desc_dmat, sc->bcnq.desc_map,
	    BUS_DMASYNC_PREWRITE);

	sc->bcnq.cur = (sc->bcnq.cur + 1) % RT2560_BEACON_RING_COUNT;

	return 0;
}

static int
rt2560_tx_mgt(struct rt2560_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	bus_dma_segment_t segs[RT2560_MAX_SCATTER];
	uint16_t dur;
	uint32_t flags = 0;
	int nsegs, rate, error;

	desc = &sc->prioq.desc[sc->prioq.cur];
	data = &sc->prioq.data[sc->prioq.cur];

	rate = ni->ni_txparms->mgmtrate;

	wh = mtod(m0, struct ieee80211_frame *);

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			m_freem(m0);
			return ENOBUFS;
		}
	}

	error = bus_dmamap_load_mbuf_sg(sc->prioq.data_dmat, data->map, m0,
	    segs, &nsegs, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map mbuf (error %d)\n",
		    error);
		m_freem(m0);
		return error;
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct rt2560_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_antenna = sc->tx_ant;

		ieee80211_radiotap_tx(vap, m0);
	}

	data->m = m0;
	data->ni = ni;
	/* management frames are not taken into account for amrr */
	data->rix = IEEE80211_FIXED_RATE_NONE;

	wh = mtod(m0, struct ieee80211_frame *);

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2560_TX_ACK;

		dur = ieee80211_ack_duration(ic->ic_rt,
		    rate, ic->ic_flags & IEEE80211_F_SHPREAMBLE);
		*(uint16_t *)wh->i_dur = htole16(dur);

		/* tell hardware to add timestamp for probe responses */
		if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_MGT &&
		    (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
		    IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			flags |= RT2560_TX_TIMESTAMP;
	}

	rt2560_setup_tx_desc(sc, desc, flags, m0->m_pkthdr.len, rate, 0,
	    segs->ds_addr);

	bus_dmamap_sync(sc->prioq.data_dmat, data->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->prioq.desc_dmat, sc->prioq.desc_map,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(sc, 10, "sending mgt frame len=%u idx=%u rate=%u\n",
	    m0->m_pkthdr.len, sc->prioq.cur, rate);

	/* kick prio */
	sc->prioq.queued++;
	sc->prioq.cur = (sc->prioq.cur + 1) % RT2560_PRIO_RING_COUNT;
	RAL_WRITE(sc, RT2560_TXCSR0, RT2560_KICK_PRIO);

	return 0;
}

static int
rt2560_sendprot(struct rt2560_softc *sc,
    const struct mbuf *m, struct ieee80211_node *ni, int prot, int rate)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	struct mbuf *mprot;
	int protrate, flags, error;
	bus_dma_segment_t segs[RT2560_MAX_SCATTER];
	int nsegs;

	mprot = ieee80211_alloc_prot(ni, m, rate, prot);
	if (mprot == NULL) {
		if_inc_counter(ni->ni_vap->iv_ifp, IFCOUNTER_OERRORS, 1);
		device_printf(sc->sc_dev,
		    "could not allocate mbuf for protection mode %d\n", prot);
		return ENOBUFS;
	}

	desc = &sc->txq.desc[sc->txq.cur_encrypt];
	data = &sc->txq.data[sc->txq.cur_encrypt];

	error = bus_dmamap_load_mbuf_sg(sc->txq.data_dmat, data->map,
	    mprot, segs, &nsegs, 0);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not map mbuf (error %d)\n", error);
		m_freem(mprot);
		return error;
	}

	data->m = mprot;
	data->ni = ieee80211_ref_node(ni);
	/* ctl frames are not taken into account for amrr */
	data->rix = IEEE80211_FIXED_RATE_NONE;

	protrate = ieee80211_ctl_rate(ic->ic_rt, rate);
	flags = RT2560_TX_MORE_FRAG;
	if (prot == IEEE80211_PROT_RTSCTS)
		flags |= RT2560_TX_ACK;

	rt2560_setup_tx_desc(sc, desc, flags, mprot->m_pkthdr.len, protrate, 1,
	    segs->ds_addr);

	bus_dmamap_sync(sc->txq.data_dmat, data->map,
	    BUS_DMASYNC_PREWRITE);

	sc->txq.queued++;
	sc->txq.cur_encrypt = (sc->txq.cur_encrypt + 1) % RT2560_TX_RING_COUNT;

	return 0;
}

static int
rt2560_tx_raw(struct rt2560_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni, const struct ieee80211_bpf_params *params)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	bus_dma_segment_t segs[RT2560_MAX_SCATTER];
	uint32_t flags;
	int nsegs, rate, error;

	desc = &sc->prioq.desc[sc->prioq.cur];
	data = &sc->prioq.data[sc->prioq.cur];

	rate = params->ibp_rate0;
	if (!ieee80211_isratevalid(ic->ic_rt, rate)) {
		/* XXX fall back to mcast/mgmt rate? */
		m_freem(m0);
		return EINVAL;
	}

	flags = 0;
	if ((params->ibp_flags & IEEE80211_BPF_NOACK) == 0)
		flags |= RT2560_TX_ACK;
	if (params->ibp_flags & (IEEE80211_BPF_RTS|IEEE80211_BPF_CTS)) {
		error = rt2560_sendprot(sc, m0, ni,
		    params->ibp_flags & IEEE80211_BPF_RTS ?
			 IEEE80211_PROT_RTSCTS : IEEE80211_PROT_CTSONLY,
		    rate);
		if (error) {
			m_freem(m0);
			return error;
		}
		flags |= RT2560_TX_LONG_RETRY | RT2560_TX_IFS_SIFS;
	}

	error = bus_dmamap_load_mbuf_sg(sc->prioq.data_dmat, data->map, m0,
	    segs, &nsegs, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map mbuf (error %d)\n",
		    error);
		m_freem(m0);
		return error;
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct rt2560_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_antenna = sc->tx_ant;

		ieee80211_radiotap_tx(ni->ni_vap, m0);
	}

	data->m = m0;
	data->ni = ni;

	/* XXX need to setup descriptor ourself */
	rt2560_setup_tx_desc(sc, desc, flags, m0->m_pkthdr.len,
	    rate, (params->ibp_flags & IEEE80211_BPF_CRYPTO) != 0,
	    segs->ds_addr);

	bus_dmamap_sync(sc->prioq.data_dmat, data->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->prioq.desc_dmat, sc->prioq.desc_map,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(sc, 10, "sending raw frame len=%u idx=%u rate=%u\n",
	    m0->m_pkthdr.len, sc->prioq.cur, rate);

	/* kick prio */
	sc->prioq.queued++;
	sc->prioq.cur = (sc->prioq.cur + 1) % RT2560_PRIO_RING_COUNT;
	RAL_WRITE(sc, RT2560_TXCSR0, RT2560_KICK_PRIO);

	return 0;
}

static int
rt2560_tx_data(struct rt2560_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct rt2560_tx_desc *desc;
	struct rt2560_tx_data *data;
	struct ieee80211_frame *wh;
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct ieee80211_key *k;
	struct mbuf *mnew;
	bus_dma_segment_t segs[RT2560_MAX_SCATTER];
	uint16_t dur;
	uint32_t flags;
	int nsegs, rate, error;

	wh = mtod(m0, struct ieee80211_frame *);

	if (m0->m_flags & M_EAPOL) {
		rate = tp->mgmtrate;
	} else if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		rate = tp->mcastrate;
	} else if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE) {
		rate = tp->ucastrate;
	} else {
		(void) ieee80211_ratectl_rate(ni, NULL, 0);
		rate = ni->ni_txrate;
	}

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			m_freem(m0);
			return ENOBUFS;
		}

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	flags = 0;
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		int prot = IEEE80211_PROT_NONE;
		if (m0->m_pkthdr.len + IEEE80211_CRC_LEN > vap->iv_rtsthreshold)
			prot = IEEE80211_PROT_RTSCTS;
		else if ((ic->ic_flags & IEEE80211_F_USEPROT) &&
		    ieee80211_rate2phytype(ic->ic_rt, rate) == IEEE80211_T_OFDM)
			prot = ic->ic_protmode;
		if (prot != IEEE80211_PROT_NONE) {
			error = rt2560_sendprot(sc, m0, ni, prot, rate);
			if (error) {
				m_freem(m0);
				return error;
			}
			flags |= RT2560_TX_LONG_RETRY | RT2560_TX_IFS_SIFS;
		}
	}

	data = &sc->txq.data[sc->txq.cur_encrypt];
	desc = &sc->txq.desc[sc->txq.cur_encrypt];

	error = bus_dmamap_load_mbuf_sg(sc->txq.data_dmat, data->map, m0,
	    segs, &nsegs, 0);
	if (error != 0 && error != EFBIG) {
		device_printf(sc->sc_dev, "could not map mbuf (error %d)\n",
		    error);
		m_freem(m0);
		return error;
	}
	if (error != 0) {
		mnew = m_defrag(m0, M_NOWAIT);
		if (mnew == NULL) {
			device_printf(sc->sc_dev,
			    "could not defragment mbuf\n");
			m_freem(m0);
			return ENOBUFS;
		}
		m0 = mnew;

		error = bus_dmamap_load_mbuf_sg(sc->txq.data_dmat, data->map,
		    m0, segs, &nsegs, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not map mbuf (error %d)\n", error);
			m_freem(m0);
			return error;
		}

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct rt2560_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_antenna = sc->tx_ant;

		ieee80211_radiotap_tx(vap, m0);
	}

	data->m = m0;
	data->ni = ni;

	/* remember link conditions for rate adaptation algorithm */
	if (tp->ucastrate == IEEE80211_FIXED_RATE_NONE) {
		data->rix = ni->ni_txrate;
		/* XXX probably need last rssi value and not avg */
		data->rssi = ic->ic_node_getrssi(ni);
	} else
		data->rix = IEEE80211_FIXED_RATE_NONE;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2560_TX_ACK;

		dur = ieee80211_ack_duration(ic->ic_rt,
		    rate, ic->ic_flags & IEEE80211_F_SHPREAMBLE);
		*(uint16_t *)wh->i_dur = htole16(dur);
	}

	rt2560_setup_tx_desc(sc, desc, flags, m0->m_pkthdr.len, rate, 1,
	    segs->ds_addr);

	bus_dmamap_sync(sc->txq.data_dmat, data->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->txq.desc_dmat, sc->txq.desc_map,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(sc, 10, "sending data frame len=%u idx=%u rate=%u\n",
	    m0->m_pkthdr.len, sc->txq.cur_encrypt, rate);

	/* kick encrypt */
	sc->txq.queued++;
	sc->txq.cur_encrypt = (sc->txq.cur_encrypt + 1) % RT2560_TX_RING_COUNT;
	RAL_WRITE(sc, RT2560_SECCSR1, RT2560_KICK_ENCRYPT);

	return 0;
}

static int
rt2560_transmit(struct ieee80211com *ic, struct mbuf *m)   
{
	struct rt2560_softc *sc = ic->ic_softc;
	int error;

	RAL_LOCK(sc);
	if ((sc->sc_flags & RT2560_F_RUNNING) == 0) {
		RAL_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		RAL_UNLOCK(sc);
		return (error);
	}
	rt2560_start(sc);
	RAL_UNLOCK(sc);

	return (0);
}

static void
rt2560_start(struct rt2560_softc *sc)
{
	struct ieee80211_node *ni;
	struct mbuf *m;

	RAL_LOCK_ASSERT(sc);

	while (sc->txq.queued < RT2560_TX_RING_COUNT - 1 &&
	    (m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *) m->m_pkthdr.rcvif;
		if (rt2560_tx_data(sc, m, ni) != 0) {
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			ieee80211_free_node(ni);
			break;
		}
		sc->sc_tx_timer = 5;
	}
}

static void
rt2560_watchdog(void *arg)
{
	struct rt2560_softc *sc = arg;

	RAL_LOCK_ASSERT(sc);

	KASSERT(sc->sc_flags & RT2560_F_RUNNING, ("not running"));

	if (sc->sc_invalid)		/* card ejected */
		return;

	rt2560_encryption_intr(sc);
	rt2560_tx_intr(sc);

	if (sc->sc_tx_timer > 0 && --sc->sc_tx_timer == 0) {
		device_printf(sc->sc_dev, "device timeout\n");
		rt2560_init_locked(sc);
		counter_u64_add(sc->sc_ic.ic_oerrors, 1);
		/* NB: callout is reset in rt2560_init() */
		return;
	}
	callout_reset(&sc->watchdog_ch, hz, rt2560_watchdog, sc);
}

static void
rt2560_parent(struct ieee80211com *ic)
{
	struct rt2560_softc *sc = ic->ic_softc;
	int startall = 0;

	RAL_LOCK(sc);
	if (ic->ic_nrunning > 0) {
		if ((sc->sc_flags & RT2560_F_RUNNING) == 0) {
			rt2560_init_locked(sc);
			startall = 1;
		} else
			rt2560_update_promisc(ic);
	} else if (sc->sc_flags & RT2560_F_RUNNING)
		rt2560_stop_locked(sc);
	RAL_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

static void
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
		device_printf(sc->sc_dev, "could not write to BBP\n");
		return;
	}

	tmp = RT2560_BBP_WRITE | RT2560_BBP_BUSY | reg << 8 | val;
	RAL_WRITE(sc, RT2560_BBPCSR, tmp);

	DPRINTFN(sc, 15, "BBP R%u <- 0x%02x\n", reg, val);
}

static uint8_t
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
		device_printf(sc->sc_dev, "could not read from BBP\n");
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

	device_printf(sc->sc_dev, "could not read from BBP\n");
	return 0;
}

static void
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
		device_printf(sc->sc_dev, "could not write to RF\n");
		return;
	}

	tmp = RT2560_RF_BUSY | RT2560_RF_20BIT | (val & 0xfffff) << 2 |
	    (reg & 0x3);
	RAL_WRITE(sc, RT2560_RFCSR, tmp);

	/* remember last written value in sc */
	sc->rf_regs[reg] = val;

	DPRINTFN(sc, 15, "RF R[%u] <- 0x%05x\n", reg & 0x3, val & 0xfffff);
}

static void
rt2560_set_chan(struct rt2560_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t power, tmp;
	u_int i, chan;

	chan = ieee80211_chan2ieee(ic, c);
	KASSERT(chan != 0 && chan != IEEE80211_CHAN_ANY, ("chan 0x%x", chan));

	if (IEEE80211_IS_CHAN_2GHZ(c))
		power = min(sc->txpow[chan - 1], 31);
	else
		power = 31;

	/* adjust txpower using ifconfig settings */
	power -= (100 - ic->ic_txpowlimit) / 8;

	DPRINTFN(sc, 2, "setting channel to %u, txpower to %u\n", chan, power);

	switch (sc->rf_rev) {
	case RT2560_RF_2522:
		rt2560_rf_write(sc, RAL_RF1, 0x00814);
		rt2560_rf_write(sc, RAL_RF2, rt2560_rf2522_r2[chan - 1]);
		rt2560_rf_write(sc, RAL_RF3, power << 7 | 0x00040);
		break;

	case RT2560_RF_2523:
		rt2560_rf_write(sc, RAL_RF1, 0x08804);
		rt2560_rf_write(sc, RAL_RF2, rt2560_rf2523_r2[chan - 1]);
		rt2560_rf_write(sc, RAL_RF3, power << 7 | 0x38044);
		rt2560_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RT2560_RF_2524:
		rt2560_rf_write(sc, RAL_RF1, 0x0c808);
		rt2560_rf_write(sc, RAL_RF2, rt2560_rf2524_r2[chan - 1]);
		rt2560_rf_write(sc, RAL_RF3, power << 7 | 0x00040);
		rt2560_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RT2560_RF_2525:
		rt2560_rf_write(sc, RAL_RF1, 0x08808);
		rt2560_rf_write(sc, RAL_RF2, rt2560_rf2525_hi_r2[chan - 1]);
		rt2560_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		rt2560_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);

		rt2560_rf_write(sc, RAL_RF1, 0x08808);
		rt2560_rf_write(sc, RAL_RF2, rt2560_rf2525_r2[chan - 1]);
		rt2560_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		rt2560_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RT2560_RF_2525E:
		rt2560_rf_write(sc, RAL_RF1, 0x08808);
		rt2560_rf_write(sc, RAL_RF2, rt2560_rf2525e_r2[chan - 1]);
		rt2560_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		rt2560_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00286 : 0x00282);
		break;

	case RT2560_RF_2526:
		rt2560_rf_write(sc, RAL_RF2, rt2560_rf2526_hi_r2[chan - 1]);
		rt2560_rf_write(sc, RAL_RF4, (chan & 1) ? 0x00386 : 0x00381);
		rt2560_rf_write(sc, RAL_RF1, 0x08804);

		rt2560_rf_write(sc, RAL_RF2, rt2560_rf2526_r2[chan - 1]);
		rt2560_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		rt2560_rf_write(sc, RAL_RF4, (chan & 1) ? 0x00386 : 0x00381);
		break;

	/* dual-band RF */
	case RT2560_RF_5222:
		for (i = 0; rt2560_rf5222[i].chan != chan; i++);

		rt2560_rf_write(sc, RAL_RF1, rt2560_rf5222[i].r1);
		rt2560_rf_write(sc, RAL_RF2, rt2560_rf5222[i].r2);
		rt2560_rf_write(sc, RAL_RF3, power << 7 | 0x00040);
		rt2560_rf_write(sc, RAL_RF4, rt2560_rf5222[i].r4);
		break;
	default: 
 	        printf("unknown ral rev=%d\n", sc->rf_rev);
	}

	/* XXX */
	if ((ic->ic_flags & IEEE80211_F_SCAN) == 0) {
		/* set Japan filter bit for channel 14 */
		tmp = rt2560_bbp_read(sc, 70);

		tmp &= ~RT2560_JAPAN_FILTER;
		if (chan == 14)
			tmp |= RT2560_JAPAN_FILTER;

		rt2560_bbp_write(sc, 70, tmp);

		/* clear CRC errors */
		RAL_READ(sc, RT2560_CNT0);
	}
}

static void
rt2560_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct rt2560_softc *sc = ic->ic_softc;
	uint8_t bands[IEEE80211_MODE_BYTES];

	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	ieee80211_add_channels_default_2ghz(chans, maxchans, nchans, bands, 0);

	if (sc->rf_rev == RT2560_RF_5222) {
		setbit(bands, IEEE80211_MODE_11A);
		ieee80211_add_channel_list_5ghz(chans, maxchans, nchans,
		    rt2560_chan_5ghz, nitems(rt2560_chan_5ghz), bands, 0);
	}
}

static void
rt2560_set_channel(struct ieee80211com *ic)
{
	struct rt2560_softc *sc = ic->ic_softc;

	RAL_LOCK(sc);
	rt2560_set_chan(sc, ic->ic_curchan);
	RAL_UNLOCK(sc);

}

#if 0
/*
 * Disable RF auto-tuning.
 */
static void
rt2560_disable_rf_tune(struct rt2560_softc *sc)
{
	uint32_t tmp;

	if (sc->rf_rev != RT2560_RF_2523) {
		tmp = sc->rf_regs[RAL_RF1] & ~RAL_RF1_AUTOTUNE;
		rt2560_rf_write(sc, RAL_RF1, tmp);
	}

	tmp = sc->rf_regs[RAL_RF3] & ~RAL_RF3_AUTOTUNE;
	rt2560_rf_write(sc, RAL_RF3, tmp);

	DPRINTFN(sc, 2, "%s", "disabling RF autotune\n");
}
#endif

/*
 * Refer to IEEE Std 802.11-1999 pp. 123 for more information on TSF
 * synchronization.
 */
static void
rt2560_enable_tsf_sync(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint16_t logcwmin, preload;
	uint32_t tmp;

	/* first, disable TSF synchronization */
	RAL_WRITE(sc, RT2560_CSR14, 0);

	tmp = 16 * vap->iv_bss->ni_intval;
	RAL_WRITE(sc, RT2560_CSR12, tmp);

	RAL_WRITE(sc, RT2560_CSR13, 0);

	logcwmin = 5;
	preload = (vap->iv_opmode == IEEE80211_M_STA) ? 384 : 1024;
	tmp = logcwmin << 16 | preload;
	RAL_WRITE(sc, RT2560_BCNOCSR, tmp);

	/* finally, enable TSF synchronization */
	tmp = RT2560_ENABLE_TSF | RT2560_ENABLE_TBCN;
	if (ic->ic_opmode == IEEE80211_M_STA)
		tmp |= RT2560_ENABLE_TSF_SYNC(1);
	else
		tmp |= RT2560_ENABLE_TSF_SYNC(2) |
		       RT2560_ENABLE_BEACON_GENERATOR;
	RAL_WRITE(sc, RT2560_CSR14, tmp);

	DPRINTF(sc, "%s", "enabling TSF synchronization\n");
}

static void
rt2560_enable_tsf(struct rt2560_softc *sc)
{
	RAL_WRITE(sc, RT2560_CSR14, 0);
	RAL_WRITE(sc, RT2560_CSR14,
	    RT2560_ENABLE_TSF_SYNC(2) | RT2560_ENABLE_TSF);
}

static void
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

	DPRINTF(sc, "updating PLCP for %s preamble\n",
	    (ic->ic_flags & IEEE80211_F_SHPREAMBLE) ? "short" : "long");
}

/*
 * This function can be called by ieee80211_set_shortslottime(). Refer to
 * IEEE Std 802.11-1999 pp. 85 to know how these values are computed.
 */
static void
rt2560_update_slot(struct ieee80211com *ic)
{
	struct rt2560_softc *sc = ic->ic_softc;
	uint8_t slottime;
	uint16_t tx_sifs, tx_pifs, tx_difs, eifs;
	uint32_t tmp;

#ifndef FORCE_SLOTTIME
	slottime = IEEE80211_GET_SLOTTIME(ic);
#else
	/*
	 * Setting slot time according to "short slot time" capability
	 * in beacon/probe_resp seems to cause problem to acknowledge
	 * certain AP's data frames transimitted at CCK/DS rates: the
	 * problematic AP keeps retransmitting data frames, probably
	 * because MAC level acks are not received by hardware.
	 * So we cheat a little bit here by claiming we are capable of
	 * "short slot time" but setting hardware slot time to the normal
	 * slot time.  ral(4) does not seem to have trouble to receive
	 * frames transmitted using short slot time even if hardware
	 * slot time is set to normal slot time.  If we didn't use this
	 * trick, we would have to claim that short slot time is not
	 * supported; this would give relative poor RX performance
	 * (-1Mb~-2Mb lower) and the _whole_ BSS would stop using short
	 * slot time.
	 */
	slottime = IEEE80211_DUR_SLOT;
#endif

	/* update the MAC slot boundaries */
	tx_sifs = RAL_SIFS - RT2560_TXRX_TURNAROUND;
	tx_pifs = tx_sifs + slottime;
	tx_difs = IEEE80211_DUR_DIFS(tx_sifs, slottime);
	eifs = (ic->ic_curmode == IEEE80211_MODE_11B) ? 364 : 60;

	tmp = RAL_READ(sc, RT2560_CSR11);
	tmp = (tmp & ~0x1f00) | slottime << 8;
	RAL_WRITE(sc, RT2560_CSR11, tmp);

	tmp = tx_pifs << 16 | tx_sifs;
	RAL_WRITE(sc, RT2560_CSR18, tmp);

	tmp = eifs << 16 | tx_difs;
	RAL_WRITE(sc, RT2560_CSR19, tmp);

	DPRINTF(sc, "setting slottime to %uus\n", slottime);
}

static void
rt2560_set_basicrates(struct rt2560_softc *sc,
    const struct ieee80211_rateset *rs)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t mask = 0;
	uint8_t rate;
	int i;

	for (i = 0; i < rs->rs_nrates; i++) {
		rate = rs->rs_rates[i];

		if (!(rate & IEEE80211_RATE_BASIC))
			continue;

		mask |= 1 << ieee80211_legacy_rate_lookup(ic->ic_rt,
		    IEEE80211_RV(rate));
	}

	RAL_WRITE(sc, RT2560_ARSP_PLCP_1, mask);

	DPRINTF(sc, "Setting basic rate mask to 0x%x\n", mask);
}

static void
rt2560_update_led(struct rt2560_softc *sc, int led1, int led2)
{
	uint32_t tmp;

	/* set ON period to 70ms and OFF period to 30ms */
	tmp = led1 << 16 | led2 << 17 | 70 << 8 | 30;
	RAL_WRITE(sc, RT2560_LEDCSR, tmp);
}

static void
rt2560_set_bssid(struct rt2560_softc *sc, const uint8_t *bssid)
{
	uint32_t tmp;

	tmp = bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24;
	RAL_WRITE(sc, RT2560_CSR5, tmp);

	tmp = bssid[4] | bssid[5] << 8;
	RAL_WRITE(sc, RT2560_CSR6, tmp);

	DPRINTF(sc, "setting BSSID to %6D\n", bssid, ":");
}

static void
rt2560_set_macaddr(struct rt2560_softc *sc, const uint8_t *addr)
{
	uint32_t tmp;

	tmp = addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24;
	RAL_WRITE(sc, RT2560_CSR3, tmp);

	tmp = addr[4] | addr[5] << 8;
	RAL_WRITE(sc, RT2560_CSR4, tmp);

	DPRINTF(sc, "setting MAC address to %6D\n", addr, ":");
}

static void
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

static void
rt2560_update_promisc(struct ieee80211com *ic)
{
	struct rt2560_softc *sc = ic->ic_softc;
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2560_RXCSR0);

	tmp &= ~RT2560_DROP_NOT_TO_ME;
	if (ic->ic_promisc == 0)
		tmp |= RT2560_DROP_NOT_TO_ME;

	RAL_WRITE(sc, RT2560_RXCSR0, tmp);

	DPRINTF(sc, "%s promiscuous mode\n",
	    (ic->ic_promisc > 0) ?  "entering" : "leaving");
}

static const char *
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

static void
rt2560_read_config(struct rt2560_softc *sc)
{
	uint16_t val;
	int i;

	val = rt2560_eeprom_read(sc, RT2560_EEPROM_CONFIG0);
	sc->rf_rev =   (val >> 11) & 0x7;
	sc->hw_radio = (val >> 10) & 0x1;
	sc->led_mode = (val >> 6)  & 0x7;
	sc->rx_ant =   (val >> 4)  & 0x3;
	sc->tx_ant =   (val >> 2)  & 0x3;
	sc->nb_ant =   val & 0x3;

	/* read default values for BBP registers */
	for (i = 0; i < 16; i++) {
		val = rt2560_eeprom_read(sc, RT2560_EEPROM_BBP_BASE + i);
		if (val == 0 || val == 0xffff)
			continue;

		sc->bbp_prom[i].reg = val >> 8;
		sc->bbp_prom[i].val = val & 0xff;
	}

	/* read Tx power for all b/g channels */
	for (i = 0; i < 14 / 2; i++) {
		val = rt2560_eeprom_read(sc, RT2560_EEPROM_TXPOWER + i);
		sc->txpow[i * 2] = val & 0xff;
		sc->txpow[i * 2 + 1] = val >> 8;
	}
	for (i = 0; i < 14; ++i) {
		if (sc->txpow[i] > 31)
			sc->txpow[i] = 24;
	}

	val = rt2560_eeprom_read(sc, RT2560_EEPROM_CALIBRATE);
	if ((val & 0xff) == 0xff)
		sc->rssi_corr = RT2560_DEFAULT_RSSI_CORR;
	else
		sc->rssi_corr = val & 0xff;
	DPRINTF(sc, "rssi correction %d, calibrate 0x%02x\n",
		 sc->rssi_corr, val);
}


static void
rt2560_scan_start(struct ieee80211com *ic)
{
	struct rt2560_softc *sc = ic->ic_softc;

	/* abort TSF synchronization */
	RAL_WRITE(sc, RT2560_CSR14, 0);
	rt2560_set_bssid(sc, ieee80211broadcastaddr);
}

static void
rt2560_scan_end(struct ieee80211com *ic)
{
	struct rt2560_softc *sc = ic->ic_softc;
	struct ieee80211vap *vap = ic->ic_scan->ss_vap;

	rt2560_enable_tsf_sync(sc);
	/* XXX keep local copy */
	rt2560_set_bssid(sc, vap->iv_bss->ni_bssid);
}

static int
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
		device_printf(sc->sc_dev, "timeout waiting for BBP\n");
		return EIO;
	}

	/* initialize BBP registers to default values */
	for (i = 0; i < nitems(rt2560_def_bbp); i++) {
		rt2560_bbp_write(sc, rt2560_def_bbp[i].reg,
		    rt2560_def_bbp[i].val);
	}

	/* initialize BBP registers to values stored in EEPROM */
	for (i = 0; i < 16; i++) {
		if (sc->bbp_prom[i].reg == 0 && sc->bbp_prom[i].val == 0)
			break;
		rt2560_bbp_write(sc, sc->bbp_prom[i].reg, sc->bbp_prom[i].val);
	}
	rt2560_bbp_write(sc, 17, 0x48);	/* XXX restore bbp17 */

	return 0;
}

static void
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

static void
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

static void
rt2560_init_locked(struct rt2560_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t tmp;
	int i;

	RAL_LOCK_ASSERT(sc);

	rt2560_stop_locked(sc);

	/* setup tx rings */
	tmp = RT2560_PRIO_RING_COUNT << 24 |
	      RT2560_ATIM_RING_COUNT << 16 |
	      RT2560_TX_RING_COUNT   <<  8 |
	      RT2560_TX_DESC_SIZE;

	/* rings must be initialized in this exact order */
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

	rt2560_set_macaddr(sc, vap ? vap->iv_myaddr : ic->ic_macaddr);

	/* set basic rate set (will be updated later) */
	RAL_WRITE(sc, RT2560_ARSP_PLCP_1, 0x153);

	rt2560_update_slot(ic);
	rt2560_update_plcp(sc);
	rt2560_update_led(sc, 0, 0);

	RAL_WRITE(sc, RT2560_CSR1, RT2560_RESET_ASIC);
	RAL_WRITE(sc, RT2560_CSR1, RT2560_HOST_READY);

	if (rt2560_bbp_init(sc) != 0) {
		rt2560_stop_locked(sc);
		return;
	}

	rt2560_set_txantenna(sc, sc->tx_ant);
	rt2560_set_rxantenna(sc, sc->rx_ant);

	/* set default BSS channel */
	rt2560_set_chan(sc, ic->ic_curchan);

	/* kick Rx */
	tmp = RT2560_DROP_PHY_ERROR | RT2560_DROP_CRC_ERROR;
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RT2560_DROP_CTL | RT2560_DROP_VERSION_ERROR;
		if (ic->ic_opmode != IEEE80211_M_HOSTAP &&
		    ic->ic_opmode != IEEE80211_M_MBSS)
			tmp |= RT2560_DROP_TODS;
		if (ic->ic_promisc == 0)
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

	sc->sc_flags |= RT2560_F_RUNNING;

	callout_reset(&sc->watchdog_ch, hz, rt2560_watchdog, sc);
}

static void
rt2560_init(void *priv)
{
	struct rt2560_softc *sc = priv;
	struct ieee80211com *ic = &sc->sc_ic;

	RAL_LOCK(sc);
	rt2560_init_locked(sc);
	RAL_UNLOCK(sc);

	if (sc->sc_flags & RT2560_F_RUNNING)
		ieee80211_start_all(ic);		/* start all vap's */
}

static void
rt2560_stop_locked(struct rt2560_softc *sc)
{
	volatile int *flags = &sc->sc_flags;

	RAL_LOCK_ASSERT(sc);

	while (*flags & RT2560_F_INPUT_RUNNING)
		msleep(sc, &sc->sc_mtx, 0, "ralrunning", hz/10);

	callout_stop(&sc->watchdog_ch);
	sc->sc_tx_timer = 0;

	if (sc->sc_flags & RT2560_F_RUNNING) {
		sc->sc_flags &= ~RT2560_F_RUNNING;

		/* abort Tx */
		RAL_WRITE(sc, RT2560_TXCSR0, RT2560_ABORT_TX);
		
		/* disable Rx */
		RAL_WRITE(sc, RT2560_RXCSR0, RT2560_DISABLE_RX);

		/* reset ASIC (imply reset BBP) */
		RAL_WRITE(sc, RT2560_CSR1, RT2560_RESET_ASIC);
		RAL_WRITE(sc, RT2560_CSR1, 0);

		/* disable interrupts */
		RAL_WRITE(sc, RT2560_CSR8, 0xffffffff);
		
		/* reset Tx and Rx rings */
		rt2560_reset_tx_ring(sc, &sc->txq);
		rt2560_reset_tx_ring(sc, &sc->atimq);
		rt2560_reset_tx_ring(sc, &sc->prioq);
		rt2560_reset_tx_ring(sc, &sc->bcnq);
		rt2560_reset_rx_ring(sc, &sc->rxq);
	}
}

void
rt2560_stop(void *arg)
{
	struct rt2560_softc *sc = arg;

	RAL_LOCK(sc);
	rt2560_stop_locked(sc);
	RAL_UNLOCK(sc);
}

static int
rt2560_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
	const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct rt2560_softc *sc = ic->ic_softc;

	RAL_LOCK(sc);

	/* prevent management frames from being sent if we're not ready */
	if (!(sc->sc_flags & RT2560_F_RUNNING)) {
		RAL_UNLOCK(sc);
		m_freem(m);
		return ENETDOWN;
	}
	if (sc->prioq.queued >= RT2560_PRIO_RING_COUNT) {
		RAL_UNLOCK(sc);
		m_freem(m);
		return ENOBUFS;		/* XXX */
	}

	if (params == NULL) {
		/*
		 * Legacy path; interpret frame contents to decide
		 * precisely how to send the frame.
		 */
		if (rt2560_tx_mgt(sc, m, ni) != 0)
			goto bad;
	} else {
		/*
		 * Caller supplied explicit parameters to use in
		 * sending the frame.
		 */
		if (rt2560_tx_raw(sc, m, ni, params))
			goto bad;
	}
	sc->sc_tx_timer = 5;

	RAL_UNLOCK(sc);

	return 0;
bad:
	RAL_UNLOCK(sc);
	return EIO;		/* XXX */
}

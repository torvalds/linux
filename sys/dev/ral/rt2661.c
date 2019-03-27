/*	$FreeBSD$	*/

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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * Ralink Technology RT2561, RT2561S and RT2661 chipset driver
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
#include <sys/firmware.h>

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

#include <dev/ral/rt2661reg.h>
#include <dev/ral/rt2661var.h>

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

static struct ieee80211vap *rt2661_vap_create(struct ieee80211com *,
			    const char [IFNAMSIZ], int, enum ieee80211_opmode,
			    int, const uint8_t [IEEE80211_ADDR_LEN],
			    const uint8_t [IEEE80211_ADDR_LEN]);
static void		rt2661_vap_delete(struct ieee80211vap *);
static void		rt2661_dma_map_addr(void *, bus_dma_segment_t *, int,
			    int);
static int		rt2661_alloc_tx_ring(struct rt2661_softc *,
			    struct rt2661_tx_ring *, int);
static void		rt2661_reset_tx_ring(struct rt2661_softc *,
			    struct rt2661_tx_ring *);
static void		rt2661_free_tx_ring(struct rt2661_softc *,
			    struct rt2661_tx_ring *);
static int		rt2661_alloc_rx_ring(struct rt2661_softc *,
			    struct rt2661_rx_ring *, int);
static void		rt2661_reset_rx_ring(struct rt2661_softc *,
			    struct rt2661_rx_ring *);
static void		rt2661_free_rx_ring(struct rt2661_softc *,
			    struct rt2661_rx_ring *);
static int		rt2661_newstate(struct ieee80211vap *,
			    enum ieee80211_state, int);
static uint16_t		rt2661_eeprom_read(struct rt2661_softc *, uint8_t);
static void		rt2661_rx_intr(struct rt2661_softc *);
static void		rt2661_tx_intr(struct rt2661_softc *);
static void		rt2661_tx_dma_intr(struct rt2661_softc *,
			    struct rt2661_tx_ring *);
static void		rt2661_mcu_beacon_expire(struct rt2661_softc *);
static void		rt2661_mcu_wakeup(struct rt2661_softc *);
static void		rt2661_mcu_cmd_intr(struct rt2661_softc *);
static void		rt2661_scan_start(struct ieee80211com *);
static void		rt2661_scan_end(struct ieee80211com *);
static void		rt2661_getradiocaps(struct ieee80211com *, int, int *,
			    struct ieee80211_channel[]);
static void		rt2661_set_channel(struct ieee80211com *);
static void		rt2661_setup_tx_desc(struct rt2661_softc *,
			    struct rt2661_tx_desc *, uint32_t, uint16_t, int,
			    int, const bus_dma_segment_t *, int, int);
static int		rt2661_tx_data(struct rt2661_softc *, struct mbuf *,
			    struct ieee80211_node *, int);
static int		rt2661_tx_mgt(struct rt2661_softc *, struct mbuf *,
			    struct ieee80211_node *);
static int		rt2661_transmit(struct ieee80211com *, struct mbuf *);
static void		rt2661_start(struct rt2661_softc *);
static int		rt2661_raw_xmit(struct ieee80211_node *, struct mbuf *,
			    const struct ieee80211_bpf_params *);
static void		rt2661_watchdog(void *);
static void		rt2661_parent(struct ieee80211com *);
static void		rt2661_bbp_write(struct rt2661_softc *, uint8_t,
			    uint8_t);
static uint8_t		rt2661_bbp_read(struct rt2661_softc *, uint8_t);
static void		rt2661_rf_write(struct rt2661_softc *, uint8_t,
			    uint32_t);
static int		rt2661_tx_cmd(struct rt2661_softc *, uint8_t,
			    uint16_t);
static void		rt2661_select_antenna(struct rt2661_softc *);
static void		rt2661_enable_mrr(struct rt2661_softc *);
static void		rt2661_set_txpreamble(struct rt2661_softc *);
static void		rt2661_set_basicrates(struct rt2661_softc *,
			    const struct ieee80211_rateset *);
static void		rt2661_select_band(struct rt2661_softc *,
			    struct ieee80211_channel *);
static void		rt2661_set_chan(struct rt2661_softc *,
			    struct ieee80211_channel *);
static void		rt2661_set_bssid(struct rt2661_softc *,
			    const uint8_t *);
static void		rt2661_set_macaddr(struct rt2661_softc *,
			   const uint8_t *);
static void		rt2661_update_promisc(struct ieee80211com *);
static int		rt2661_wme_update(struct ieee80211com *) __unused;
static void		rt2661_update_slot(struct ieee80211com *);
static const char	*rt2661_get_rf(int);
static void		rt2661_read_eeprom(struct rt2661_softc *,
			    uint8_t macaddr[IEEE80211_ADDR_LEN]);
static int		rt2661_bbp_init(struct rt2661_softc *);
static void		rt2661_init_locked(struct rt2661_softc *);
static void		rt2661_init(void *);
static void             rt2661_stop_locked(struct rt2661_softc *);
static void		rt2661_stop(void *);
static int		rt2661_load_microcode(struct rt2661_softc *);
#ifdef notyet
static void		rt2661_rx_tune(struct rt2661_softc *);
static void		rt2661_radar_start(struct rt2661_softc *);
static int		rt2661_radar_stop(struct rt2661_softc *);
#endif
static int		rt2661_prepare_beacon(struct rt2661_softc *,
			    struct ieee80211vap *);
static void		rt2661_enable_tsf_sync(struct rt2661_softc *);
static void		rt2661_enable_tsf(struct rt2661_softc *);
static int		rt2661_get_rssi(struct rt2661_softc *, uint8_t);

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

static const uint8_t rt2661_chan_5ghz[] =
	{ 36, 40, 44, 48, 52, 56, 60, 64,
	  100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
	  149, 153, 157, 161, 165 };

int
rt2661_attach(device_t dev, int id)
{
	struct rt2661_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t val;
	int error, ac, ntries;

	sc->sc_id = id;
	sc->sc_dev = dev;

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF | MTX_RECURSE);

	callout_init_mtx(&sc->watchdog_ch, &sc->sc_mtx, 0);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	/* wait for NIC to initialize */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((val = RAL_READ(sc, RT2661_MAC_CSR0)) != 0)
			break;
		DELAY(1000);
	}
	if (ntries == 1000) {
		device_printf(sc->sc_dev,
		    "timeout waiting for NIC to initialize\n");
		error = EIO;
		goto fail1;
	}

	/* retrieve RF rev. no and various other things from EEPROM */
	rt2661_read_eeprom(sc, ic->ic_macaddr);

	device_printf(dev, "MAC/BBP RT%X, RF %s\n", val,
	    rt2661_get_rf(sc->rf_rev));

	/*
	 * Allocate Tx and Rx rings.
	 */
	for (ac = 0; ac < 4; ac++) {
		error = rt2661_alloc_tx_ring(sc, &sc->txq[ac],
		    RT2661_TX_RING_COUNT);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not allocate Tx ring %d\n", ac);
			goto fail2;
		}
	}

	error = rt2661_alloc_tx_ring(sc, &sc->mgtq, RT2661_MGT_RING_COUNT);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate Mgt ring\n");
		goto fail2;
	}

	error = rt2661_alloc_rx_ring(sc, &sc->rxq, RT2661_RX_RING_COUNT);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate Rx ring\n");
		goto fail3;
	}

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
		| IEEE80211_C_WME		/* 802.11e */
#endif
		;

	rt2661_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	ieee80211_ifattach(ic);
#if 0
	ic->ic_wme.wme_update = rt2661_wme_update;
#endif
	ic->ic_scan_start = rt2661_scan_start;
	ic->ic_scan_end = rt2661_scan_end;
	ic->ic_getradiocaps = rt2661_getradiocaps;
	ic->ic_set_channel = rt2661_set_channel;
	ic->ic_updateslot = rt2661_update_slot;
	ic->ic_update_promisc = rt2661_update_promisc;
	ic->ic_raw_xmit = rt2661_raw_xmit;
	ic->ic_transmit = rt2661_transmit;
	ic->ic_parent = rt2661_parent;
	ic->ic_vap_create = rt2661_vap_create;
	ic->ic_vap_delete = rt2661_vap_delete;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_txtap.wt_ihdr, sizeof(sc->sc_txtap),
		RT2661_TX_RADIOTAP_PRESENT,
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
		RT2661_RX_RADIOTAP_PRESENT);

#ifdef RAL_DEBUG
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "debug", CTLFLAG_RW, &sc->sc_debug, 0, "debug msgs");
#endif
	if (bootverbose)
		ieee80211_announce(ic);

	return 0;

fail3:	rt2661_free_tx_ring(sc, &sc->mgtq);
fail2:	while (--ac >= 0)
		rt2661_free_tx_ring(sc, &sc->txq[ac]);
fail1:	mtx_destroy(&sc->sc_mtx);
	return error;
}

int
rt2661_detach(void *xsc)
{
	struct rt2661_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	
	RAL_LOCK(sc);
	rt2661_stop_locked(sc);
	RAL_UNLOCK(sc);

	ieee80211_ifdetach(ic);
	mbufq_drain(&sc->sc_snd);

	rt2661_free_tx_ring(sc, &sc->txq[0]);
	rt2661_free_tx_ring(sc, &sc->txq[1]);
	rt2661_free_tx_ring(sc, &sc->txq[2]);
	rt2661_free_tx_ring(sc, &sc->txq[3]);
	rt2661_free_tx_ring(sc, &sc->mgtq);
	rt2661_free_rx_ring(sc, &sc->rxq);

	mtx_destroy(&sc->sc_mtx);

	return 0;
}

static struct ieee80211vap *
rt2661_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct rt2661_softc *sc = ic->ic_softc;
	struct rt2661_vap *rvp;
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
	rvp = malloc(sizeof(struct rt2661_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &rvp->ral_vap;
	ieee80211_vap_setup(ic, vap, name, unit, opmode, flags, bssid);

	/* override state transition machine */
	rvp->ral_newstate = vap->iv_newstate;
	vap->iv_newstate = rt2661_newstate;
#if 0
	vap->iv_update_beacon = rt2661_beacon_update;
#endif

	ieee80211_ratectl_init(vap);
	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change,
	    ieee80211_media_status, mac);
	if (TAILQ_FIRST(&ic->ic_vaps) == vap)
		ic->ic_opmode = opmode;
	return vap;
}

static void
rt2661_vap_delete(struct ieee80211vap *vap)
{
	struct rt2661_vap *rvp = RT2661_VAP(vap);

	ieee80211_ratectl_deinit(vap);
	ieee80211_vap_detach(vap);
	free(rvp, M_80211_VAP);
}

void
rt2661_shutdown(void *xsc)
{
	struct rt2661_softc *sc = xsc;

	rt2661_stop(sc);
}

void
rt2661_suspend(void *xsc)
{
	struct rt2661_softc *sc = xsc;

	rt2661_stop(sc);
}

void
rt2661_resume(void *xsc)
{
	struct rt2661_softc *sc = xsc;

	if (sc->sc_ic.ic_nrunning > 0)
		rt2661_init(sc);
}

static void
rt2661_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	if (error != 0)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));

	*(bus_addr_t *)arg = segs[0].ds_addr;
}

static int
rt2661_alloc_tx_ring(struct rt2661_softc *sc, struct rt2661_tx_ring *ring,
    int count)
{
	int i, error;

	ring->count = count;
	ring->queued = 0;
	ring->cur = ring->next = ring->stat = 0;

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 4, 0, 
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    count * RT2661_TX_DESC_SIZE, 1, count * RT2661_TX_DESC_SIZE,
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
	    count * RT2661_TX_DESC_SIZE, rt2661_dma_map_addr, &ring->physaddr,
	    0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not load desc DMA map\n");
		goto fail;
	}

	ring->data = malloc(count * sizeof (struct rt2661_tx_data), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (ring->data == NULL) {
		device_printf(sc->sc_dev, "could not allocate soft data\n");
		error = ENOMEM;
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0, 
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES,
	    RT2661_MAX_SCATTER, MCLBYTES, 0, NULL, NULL, &ring->data_dmat);
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

fail:	rt2661_free_tx_ring(sc, ring);
	return error;
}

static void
rt2661_reset_tx_ring(struct rt2661_softc *sc, struct rt2661_tx_ring *ring)
{
	struct rt2661_tx_desc *desc;
	struct rt2661_tx_data *data;
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
	ring->cur = ring->next = ring->stat = 0;
}

static void
rt2661_free_tx_ring(struct rt2661_softc *sc, struct rt2661_tx_ring *ring)
{
	struct rt2661_tx_data *data;
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
rt2661_alloc_rx_ring(struct rt2661_softc *sc, struct rt2661_rx_ring *ring,
    int count)
{
	struct rt2661_rx_desc *desc;
	struct rt2661_rx_data *data;
	bus_addr_t physaddr;
	int i, error;

	ring->count = count;
	ring->cur = ring->next = 0;

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 4, 0, 
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    count * RT2661_RX_DESC_SIZE, 1, count * RT2661_RX_DESC_SIZE,
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
	    count * RT2661_RX_DESC_SIZE, rt2661_dma_map_addr, &ring->physaddr,
	    0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not load desc DMA map\n");
		goto fail;
	}

	ring->data = malloc(count * sizeof (struct rt2661_rx_data), M_DEVBUF,
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
		    mtod(data->m, void *), MCLBYTES, rt2661_dma_map_addr,
		    &physaddr, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not load rx buf DMA map");
			goto fail;
		}

		desc->flags = htole32(RT2661_RX_BUSY);
		desc->physaddr = htole32(physaddr);
	}

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map, BUS_DMASYNC_PREWRITE);

	return 0;

fail:	rt2661_free_rx_ring(sc, ring);
	return error;
}

static void
rt2661_reset_rx_ring(struct rt2661_softc *sc, struct rt2661_rx_ring *ring)
{
	int i;

	for (i = 0; i < ring->count; i++)
		ring->desc[i].flags = htole32(RT2661_RX_BUSY);

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map, BUS_DMASYNC_PREWRITE);

	ring->cur = ring->next = 0;
}

static void
rt2661_free_rx_ring(struct rt2661_softc *sc, struct rt2661_rx_ring *ring)
{
	struct rt2661_rx_data *data;
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
rt2661_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct rt2661_vap *rvp = RT2661_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct rt2661_softc *sc = ic->ic_softc;
	int error;

	if (nstate == IEEE80211_S_INIT && vap->iv_state == IEEE80211_S_RUN) {
		uint32_t tmp;

		/* abort TSF synchronization */
		tmp = RAL_READ(sc, RT2661_TXRX_CSR9);
		RAL_WRITE(sc, RT2661_TXRX_CSR9, tmp & ~0x00ffffff);
	}

	error = rvp->ral_newstate(vap, nstate, arg);

	if (error == 0 && nstate == IEEE80211_S_RUN) {
		struct ieee80211_node *ni = vap->iv_bss;

		if (vap->iv_opmode != IEEE80211_M_MONITOR) {
			rt2661_enable_mrr(sc);
			rt2661_set_txpreamble(sc);
			rt2661_set_basicrates(sc, &ni->ni_rates);
			rt2661_set_bssid(sc, ni->ni_bssid);
		}

		if (vap->iv_opmode == IEEE80211_M_HOSTAP ||
		    vap->iv_opmode == IEEE80211_M_IBSS ||
		    vap->iv_opmode == IEEE80211_M_MBSS) {
			error = rt2661_prepare_beacon(sc, vap);
			if (error != 0)
				return error;
		}
		if (vap->iv_opmode != IEEE80211_M_MONITOR)
			rt2661_enable_tsf_sync(sc);
		else
			rt2661_enable_tsf(sc);
	}
	return error;
}

/*
 * Read 16 bits at address 'addr' from the serial EEPROM (either 93C46 or
 * 93C66).
 */
static uint16_t
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

static void
rt2661_tx_intr(struct rt2661_softc *sc)
{
	struct ieee80211_ratectl_tx_status *txs = &sc->sc_txs;
	struct rt2661_tx_ring *txq;
	struct rt2661_tx_data *data;
	uint32_t val;
	int error, qid;

	txs->flags = IEEE80211_RATECTL_TX_FAIL_LONG;
	for (;;) {
		struct ieee80211_node *ni;
		struct mbuf *m;

		val = RAL_READ(sc, RT2661_STA_CSR4);
		if (!(val & RT2661_TX_STAT_VALID))
			break;

		/* retrieve the queue in which this frame was sent */
		qid = RT2661_TX_QID(val);
		txq = (qid <= 3) ? &sc->txq[qid] : &sc->mgtq;

		/* retrieve rate control algorithm context */
		data = &txq->data[txq->stat];
		m = data->m;
		data->m = NULL;
		ni = data->ni;
		data->ni = NULL;

		/* if no frame has been sent, ignore */
		if (ni == NULL)
			continue;

		switch (RT2661_TX_RESULT(val)) {
		case RT2661_TX_SUCCESS:
			txs->status = IEEE80211_RATECTL_TX_SUCCESS;
			txs->long_retries = RT2661_TX_RETRYCNT(val);

			DPRINTFN(sc, 10, "data frame sent successfully after "
			    "%d retries\n", txs->long_retries);
			if (data->rix != IEEE80211_FIXED_RATE_NONE)
				ieee80211_ratectl_tx_complete(ni, txs);
			error = 0;
			break;

		case RT2661_TX_RETRY_FAIL:
			txs->status = IEEE80211_RATECTL_TX_FAIL_LONG;
			txs->long_retries = RT2661_TX_RETRYCNT(val);

			DPRINTFN(sc, 9, "%s\n",
			    "sending data frame failed (too much retries)");
			if (data->rix != IEEE80211_FIXED_RATE_NONE)
				ieee80211_ratectl_tx_complete(ni, txs);
			error = 1;
			break;

		default:
			/* other failure */
			device_printf(sc->sc_dev,
			    "sending data frame failed 0x%08x\n", val);
			error = 1;
		}

		DPRINTFN(sc, 15, "tx done q=%d idx=%u\n", qid, txq->stat);

		txq->queued--;
		if (++txq->stat >= txq->count)	/* faster than % count */
			txq->stat = 0;

		ieee80211_tx_complete(ni, m, error);
	}

	sc->sc_tx_timer = 0;

	rt2661_start(sc);
}

static void
rt2661_tx_dma_intr(struct rt2661_softc *sc, struct rt2661_tx_ring *txq)
{
	struct rt2661_tx_desc *desc;
	struct rt2661_tx_data *data;

	bus_dmamap_sync(txq->desc_dmat, txq->desc_map, BUS_DMASYNC_POSTREAD);

	for (;;) {
		desc = &txq->desc[txq->next];
		data = &txq->data[txq->next];

		if ((le32toh(desc->flags) & RT2661_TX_BUSY) ||
		    !(le32toh(desc->flags) & RT2661_TX_VALID))
			break;

		bus_dmamap_sync(txq->data_dmat, data->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(txq->data_dmat, data->map);

		/* descriptor is no longer valid */
		desc->flags &= ~htole32(RT2661_TX_VALID);

		DPRINTFN(sc, 15, "tx dma done q=%p idx=%u\n", txq, txq->next);

		if (++txq->next >= txq->count)	/* faster than % count */
			txq->next = 0;
	}

	bus_dmamap_sync(txq->desc_dmat, txq->desc_map, BUS_DMASYNC_PREWRITE);
}

static void
rt2661_rx_intr(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2661_rx_desc *desc;
	struct rt2661_rx_data *data;
	bus_addr_t physaddr;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct mbuf *mnew, *m;
	int error;

	bus_dmamap_sync(sc->rxq.desc_dmat, sc->rxq.desc_map,
	    BUS_DMASYNC_POSTREAD);

	for (;;) {
		int8_t rssi, nf;

		desc = &sc->rxq.desc[sc->rxq.cur];
		data = &sc->rxq.data[sc->rxq.cur];

		if (le32toh(desc->flags) & RT2661_RX_BUSY)
			break;

		if ((le32toh(desc->flags) & RT2661_RX_PHY_ERROR) ||
		    (le32toh(desc->flags) & RT2661_RX_CRC_ERROR)) {
			/*
			 * This should not happen since we did not request
			 * to receive those frames when we filled TXRX_CSR0.
			 */
			DPRINTFN(sc, 5, "PHY or CRC error flags 0x%08x\n",
			    le32toh(desc->flags));
			counter_u64_add(ic->ic_ierrors, 1);
			goto skip;
		}

		if ((le32toh(desc->flags) & RT2661_RX_CIPHER_MASK) != 0) {
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
		    mtod(mnew, void *), MCLBYTES, rt2661_dma_map_addr,
		    &physaddr, 0);
		if (error != 0) {
			m_freem(mnew);

			/* try to reload the old mbuf */
			error = bus_dmamap_load(sc->rxq.data_dmat, data->map,
			    mtod(data->m, void *), MCLBYTES,
			    rt2661_dma_map_addr, &physaddr, 0);
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

		rssi = rt2661_get_rssi(sc, desc->rssi);
		/* Error happened during RSSI conversion. */
		if (rssi < 0)
			rssi = -30;	/* XXX ignored by net80211 */
		nf = RT2661_NOISE_FLOOR;

		if (ieee80211_radiotap_active(ic)) {
			struct rt2661_rx_radiotap_header *tap = &sc->sc_rxtap;
			uint32_t tsf_lo, tsf_hi;

			/* get timestamp (low and high 32 bits) */
			tsf_hi = RAL_READ(sc, RT2661_TXRX_CSR13);
			tsf_lo = RAL_READ(sc, RT2661_TXRX_CSR12);

			tap->wr_tsf =
			    htole64(((uint64_t)tsf_hi << 32) | tsf_lo);
			tap->wr_flags = 0;
			tap->wr_rate = ieee80211_plcp2rate(desc->rate,
			    (desc->flags & htole32(RT2661_RX_OFDM)) ?
				IEEE80211_T_OFDM : IEEE80211_T_CCK);
			tap->wr_antsignal = nf + rssi;
			tap->wr_antnoise = nf;
		}
		sc->sc_flags |= RAL_INPUT_RUNNING;
		RAL_UNLOCK(sc);
		wh = mtod(m, struct ieee80211_frame *);

		/* send the frame to the 802.11 layer */
		ni = ieee80211_find_rxnode(ic,
		    (struct ieee80211_frame_min *)wh);
		if (ni != NULL) {
			(void) ieee80211_input(ni, m, rssi, nf);
			ieee80211_free_node(ni);
		} else
			(void) ieee80211_input_all(ic, m, rssi, nf);

		RAL_LOCK(sc);
		sc->sc_flags &= ~RAL_INPUT_RUNNING;

skip:		desc->flags |= htole32(RT2661_RX_BUSY);

		DPRINTFN(sc, 15, "rx intr idx=%u\n", sc->rxq.cur);

		sc->rxq.cur = (sc->rxq.cur + 1) % RT2661_RX_RING_COUNT;
	}

	bus_dmamap_sync(sc->rxq.desc_dmat, sc->rxq.desc_map,
	    BUS_DMASYNC_PREWRITE);
}

/* ARGSUSED */
static void
rt2661_mcu_beacon_expire(struct rt2661_softc *sc)
{
	/* do nothing */
}

static void
rt2661_mcu_wakeup(struct rt2661_softc *sc)
{
	RAL_WRITE(sc, RT2661_MAC_CSR11, 5 << 16);

	RAL_WRITE(sc, RT2661_SOFT_RESET_CSR, 0x7);
	RAL_WRITE(sc, RT2661_IO_CNTL_CSR, 0x18);
	RAL_WRITE(sc, RT2661_PCI_USEC_CSR, 0x20);

	/* send wakeup command to MCU */
	rt2661_tx_cmd(sc, RT2661_MCU_CMD_WAKEUP, 0);
}

static void
rt2661_mcu_cmd_intr(struct rt2661_softc *sc)
{
	RAL_READ(sc, RT2661_M2H_CMD_DONE_CSR);
	RAL_WRITE(sc, RT2661_M2H_CMD_DONE_CSR, 0xffffffff);
}

void
rt2661_intr(void *arg)
{
	struct rt2661_softc *sc = arg;
	uint32_t r1, r2;

	RAL_LOCK(sc);

	/* disable MAC and MCU interrupts */
	RAL_WRITE(sc, RT2661_INT_MASK_CSR, 0xffffff7f);
	RAL_WRITE(sc, RT2661_MCU_INT_MASK_CSR, 0xffffffff);

	/* don't re-enable interrupts if we're shutting down */
	if (!(sc->sc_flags & RAL_RUNNING)) {
		RAL_UNLOCK(sc);
		return;
	}

	r1 = RAL_READ(sc, RT2661_INT_SOURCE_CSR);
	RAL_WRITE(sc, RT2661_INT_SOURCE_CSR, r1);

	r2 = RAL_READ(sc, RT2661_MCU_INT_SOURCE_CSR);
	RAL_WRITE(sc, RT2661_MCU_INT_SOURCE_CSR, r2);

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

	if (r2 & RT2661_MCU_BEACON_EXPIRE)
		rt2661_mcu_beacon_expire(sc);

	if (r2 & RT2661_MCU_WAKEUP)
		rt2661_mcu_wakeup(sc);

	/* re-enable MAC and MCU interrupts */
	RAL_WRITE(sc, RT2661_INT_MASK_CSR, 0x0000ff10);
	RAL_WRITE(sc, RT2661_MCU_INT_MASK_CSR, 0);

	RAL_UNLOCK(sc);
}

static uint8_t
rt2661_plcp_signal(int rate)
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
rt2661_setup_tx_desc(struct rt2661_softc *sc, struct rt2661_tx_desc *desc,
    uint32_t flags, uint16_t xflags, int len, int rate,
    const bus_dma_segment_t *segs, int nsegs, int ac)
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
	 * Remember in which queue this frame was sent. This field is driver
	 * private data only. It will be made available by the NIC in STA_CSR4
	 * on Tx interrupts.
	 */
	desc->qid = ac;

	/* setup PLCP fields */
	desc->plcp_signal  = rt2661_plcp_signal(rate);
	desc->plcp_service = 4;

	len += IEEE80211_CRC_LEN;
	if (ieee80211_rate2phytype(ic->ic_rt, rate) == IEEE80211_T_OFDM) {
		desc->flags |= htole32(RT2661_TX_OFDM);

		plcp_length = len & 0xfff;
		desc->plcp_length_hi = plcp_length >> 6;
		desc->plcp_length_lo = plcp_length & 0x3f;
	} else {
		plcp_length = howmany(16 * len, rate);
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

static int
rt2661_tx_mgt(struct rt2661_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct rt2661_tx_desc *desc;
	struct rt2661_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	bus_dma_segment_t segs[RT2661_MAX_SCATTER];
	uint16_t dur;
	uint32_t flags = 0;	/* XXX HWSEQ */
	int nsegs, rate, error;

	desc = &sc->mgtq.desc[sc->mgtq.cur];
	data = &sc->mgtq.data[sc->mgtq.cur];

	rate = ni->ni_txparms->mgmtrate;

	wh = mtod(m0, struct ieee80211_frame *);

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			m_freem(m0);
			return ENOBUFS;
		}
	}

	error = bus_dmamap_load_mbuf_sg(sc->mgtq.data_dmat, data->map, m0,
	    segs, &nsegs, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map mbuf (error %d)\n",
		    error);
		m_freem(m0);
		return error;
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct rt2661_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;

		ieee80211_radiotap_tx(vap, m0);
	}

	data->m = m0;
	data->ni = ni;
	/* management frames are not taken into account for amrr */
	data->rix = IEEE80211_FIXED_RATE_NONE;

	wh = mtod(m0, struct ieee80211_frame *);

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2661_TX_NEED_ACK;

		dur = ieee80211_ack_duration(ic->ic_rt,
		    rate, ic->ic_flags & IEEE80211_F_SHPREAMBLE);
		*(uint16_t *)wh->i_dur = htole16(dur);

		/* tell hardware to add timestamp in probe responses */
		if ((wh->i_fc[0] &
		    (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
		    (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP))
			flags |= RT2661_TX_TIMESTAMP;
	}

	rt2661_setup_tx_desc(sc, desc, flags, 0 /* XXX HWSEQ */,
	    m0->m_pkthdr.len, rate, segs, nsegs, RT2661_QID_MGT);

	bus_dmamap_sync(sc->mgtq.data_dmat, data->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->mgtq.desc_dmat, sc->mgtq.desc_map,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(sc, 10, "sending mgt frame len=%u idx=%u rate=%u\n",
	    m0->m_pkthdr.len, sc->mgtq.cur, rate);

	/* kick mgt */
	sc->mgtq.queued++;
	sc->mgtq.cur = (sc->mgtq.cur + 1) % RT2661_MGT_RING_COUNT;
	RAL_WRITE(sc, RT2661_TX_CNTL_CSR, RT2661_KICK_MGT);

	return 0;
}

static int
rt2661_sendprot(struct rt2661_softc *sc, int ac,
    const struct mbuf *m, struct ieee80211_node *ni, int prot, int rate)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct rt2661_tx_ring *txq = &sc->txq[ac];
	struct rt2661_tx_desc *desc;
	struct rt2661_tx_data *data;
	struct mbuf *mprot;
	int protrate, flags, error;
	bus_dma_segment_t segs[RT2661_MAX_SCATTER];
	int nsegs;

	mprot = ieee80211_alloc_prot(ni, m, rate, prot);
	if (mprot == NULL) {
		if_inc_counter(ni->ni_vap->iv_ifp, IFCOUNTER_OERRORS, 1);
		device_printf(sc->sc_dev,
		    "could not allocate mbuf for protection mode %d\n", prot);
		return ENOBUFS;
	}

	data = &txq->data[txq->cur];
	desc = &txq->desc[txq->cur];

	error = bus_dmamap_load_mbuf_sg(txq->data_dmat, data->map, mprot, segs,
	    &nsegs, 0);
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
	flags = RT2661_TX_MORE_FRAG;
	if (prot == IEEE80211_PROT_RTSCTS)
		flags |= RT2661_TX_NEED_ACK;

	rt2661_setup_tx_desc(sc, desc, flags, 0, mprot->m_pkthdr.len,
	    protrate, segs, 1, ac);

	bus_dmamap_sync(txq->data_dmat, data->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(txq->desc_dmat, txq->desc_map, BUS_DMASYNC_PREWRITE);

	txq->queued++;
	txq->cur = (txq->cur + 1) % RT2661_TX_RING_COUNT;

	return 0;
}

static int
rt2661_tx_data(struct rt2661_softc *sc, struct mbuf *m0,
    struct ieee80211_node *ni, int ac)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2661_tx_ring *txq = &sc->txq[ac];
	struct rt2661_tx_desc *desc;
	struct rt2661_tx_data *data;
	struct ieee80211_frame *wh;
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct ieee80211_key *k;
	struct mbuf *mnew;
	bus_dma_segment_t segs[RT2661_MAX_SCATTER];
	uint16_t dur;
	uint32_t flags;
	int error, nsegs, rate, noack = 0;

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
	rate &= IEEE80211_RATE_VAL;

	if (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_QOS)
		noack = !! ieee80211_wme_vap_ac_is_noack(vap, ac);

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
			error = rt2661_sendprot(sc, ac, m0, ni, prot, rate);
			if (error) {
				m_freem(m0);
				return error;
			}
			flags |= RT2661_TX_LONG_RETRY | RT2661_TX_IFS;
		}
	}

	data = &txq->data[txq->cur];
	desc = &txq->desc[txq->cur];

	error = bus_dmamap_load_mbuf_sg(txq->data_dmat, data->map, m0, segs,
	    &nsegs, 0);
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

		error = bus_dmamap_load_mbuf_sg(txq->data_dmat, data->map, m0,
		    segs, &nsegs, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not map mbuf (error %d)\n", error);
			m_freem(m0);
			return error;
		}

		/* packet header have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct rt2661_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;

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

	if (!noack && !IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2661_TX_NEED_ACK;

		dur = ieee80211_ack_duration(ic->ic_rt,
		    rate, ic->ic_flags & IEEE80211_F_SHPREAMBLE);
		*(uint16_t *)wh->i_dur = htole16(dur);
	}

	rt2661_setup_tx_desc(sc, desc, flags, 0, m0->m_pkthdr.len, rate, segs,
	    nsegs, ac);

	bus_dmamap_sync(txq->data_dmat, data->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(txq->desc_dmat, txq->desc_map, BUS_DMASYNC_PREWRITE);

	DPRINTFN(sc, 10, "sending data frame len=%u idx=%u rate=%u\n",
	    m0->m_pkthdr.len, txq->cur, rate);

	/* kick Tx */
	txq->queued++;
	txq->cur = (txq->cur + 1) % RT2661_TX_RING_COUNT;
	RAL_WRITE(sc, RT2661_TX_CNTL_CSR, 1 << ac);

	return 0;
}

static int
rt2661_transmit(struct ieee80211com *ic, struct mbuf *m)   
{
	struct rt2661_softc *sc = ic->ic_softc;
	int error;

	RAL_LOCK(sc);
	if ((sc->sc_flags & RAL_RUNNING) == 0) {
		RAL_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		RAL_UNLOCK(sc);
		return (error);
	}
	rt2661_start(sc);
	RAL_UNLOCK(sc);

	return (0);
}

static void
rt2661_start(struct rt2661_softc *sc)
{
	struct mbuf *m;
	struct ieee80211_node *ni;
	int ac;

	RAL_LOCK_ASSERT(sc);

	/* prevent management frames from being sent if we're not ready */
	if (!(sc->sc_flags & RAL_RUNNING) || sc->sc_invalid)
		return;

	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ac = M_WME_GETAC(m);
		if (sc->txq[ac].queued >= RT2661_TX_RING_COUNT - 1) {
			/* there is no place left in this ring */
			mbufq_prepend(&sc->sc_snd, m);
			break;
		}
		ni = (struct ieee80211_node *) m->m_pkthdr.rcvif;
		if (rt2661_tx_data(sc, m, ni, ac) != 0) {
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			ieee80211_free_node(ni);
			break;
		}
		sc->sc_tx_timer = 5;
	}
}

static int
rt2661_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
	const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct rt2661_softc *sc = ic->ic_softc;

	RAL_LOCK(sc);

	/* prevent management frames from being sent if we're not ready */
	if (!(sc->sc_flags & RAL_RUNNING)) {
		RAL_UNLOCK(sc);
		m_freem(m);
		return ENETDOWN;
	}
	if (sc->mgtq.queued >= RT2661_MGT_RING_COUNT) {
		RAL_UNLOCK(sc);
		m_freem(m);
		return ENOBUFS;		/* XXX */
	}

	/*
	 * Legacy path; interpret frame contents to decide
	 * precisely how to send the frame.
	 * XXX raw path
	 */
	if (rt2661_tx_mgt(sc, m, ni) != 0)
		goto bad;
	sc->sc_tx_timer = 5;

	RAL_UNLOCK(sc);

	return 0;
bad:
	RAL_UNLOCK(sc);
	return EIO;		/* XXX */
}

static void
rt2661_watchdog(void *arg)
{
	struct rt2661_softc *sc = (struct rt2661_softc *)arg;

	RAL_LOCK_ASSERT(sc);

	KASSERT(sc->sc_flags & RAL_RUNNING, ("not running"));

	if (sc->sc_invalid)		/* card ejected */
		return;

	if (sc->sc_tx_timer > 0 && --sc->sc_tx_timer == 0) {
		device_printf(sc->sc_dev, "device timeout\n");
		rt2661_init_locked(sc);
		counter_u64_add(sc->sc_ic.ic_oerrors, 1);
		/* NB: callout is reset in rt2661_init() */
		return;
	}
	callout_reset(&sc->watchdog_ch, hz, rt2661_watchdog, sc);
}

static void
rt2661_parent(struct ieee80211com *ic)
{
	struct rt2661_softc *sc = ic->ic_softc;
	int startall = 0;

	RAL_LOCK(sc);
	if (ic->ic_nrunning > 0) {
		if ((sc->sc_flags & RAL_RUNNING) == 0) {
			rt2661_init_locked(sc);
			startall = 1;
		} else
			rt2661_update_promisc(ic);
	} else if (sc->sc_flags & RAL_RUNNING)
		rt2661_stop_locked(sc);
	RAL_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

static void
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
		device_printf(sc->sc_dev, "could not write to BBP\n");
		return;
	}

	tmp = RT2661_BBP_BUSY | (reg & 0x7f) << 8 | val;
	RAL_WRITE(sc, RT2661_PHY_CSR3, tmp);

	DPRINTFN(sc, 15, "BBP R%u <- 0x%02x\n", reg, val);
}

static uint8_t
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
		device_printf(sc->sc_dev, "could not read from BBP\n");
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

	device_printf(sc->sc_dev, "could not read from BBP\n");
	return 0;
}

static void
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
		device_printf(sc->sc_dev, "could not write to RF\n");
		return;
	}

	tmp = RT2661_RF_BUSY | RT2661_RF_21BIT | (val & 0x1fffff) << 2 |
	    (reg & 3);
	RAL_WRITE(sc, RT2661_PHY_CSR4, tmp);

	/* remember last written value in sc */
	sc->rf_regs[reg] = val;

	DPRINTFN(sc, 15, "RF R[%u] <- 0x%05x\n", reg & 3, val & 0x1fffff);
}

static int
rt2661_tx_cmd(struct rt2661_softc *sc, uint8_t cmd, uint16_t arg)
{
	if (RAL_READ(sc, RT2661_H2M_MAILBOX_CSR) & RT2661_H2M_BUSY)
		return EIO;	/* there is already a command pending */

	RAL_WRITE(sc, RT2661_H2M_MAILBOX_CSR,
	    RT2661_H2M_BUSY | RT2661_TOKEN_NO_INTR << 16 | arg);

	RAL_WRITE(sc, RT2661_HOST_CMD_CSR, RT2661_KICK_CMD | cmd);

	return 0;
}

static void
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
static void
rt2661_enable_mrr(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2661_TXRX_CSR4);

	tmp &= ~RT2661_MRR_CCK_FALLBACK;
	if (!IEEE80211_IS_CHAN_5GHZ(ic->ic_bsschan))
		tmp |= RT2661_MRR_CCK_FALLBACK;
	tmp |= RT2661_MRR_ENABLED;

	RAL_WRITE(sc, RT2661_TXRX_CSR4, tmp);
}

static void
rt2661_set_txpreamble(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2661_TXRX_CSR4);

	tmp &= ~RT2661_SHORT_PREAMBLE;
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= RT2661_SHORT_PREAMBLE;

	RAL_WRITE(sc, RT2661_TXRX_CSR4, tmp);
}

static void
rt2661_set_basicrates(struct rt2661_softc *sc,
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

	RAL_WRITE(sc, RT2661_TXRX_CSR5, mask);

	DPRINTF(sc, "Setting basic rate mask to 0x%x\n", mask);
}

/*
 * Reprogram MAC/BBP to switch to a new band.  Values taken from the reference
 * driver.
 */
static void
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
}

static void
rt2661_set_chan(struct rt2661_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct rfprog *rfprog;
	uint8_t bbp3, bbp94 = RT2661_BBPR94_DEFAULT;
	int8_t power;
	u_int i, chan;

	chan = ieee80211_chan2ieee(ic, c);
	KASSERT(chan != 0 && chan != IEEE80211_CHAN_ANY, ("chan 0x%x", chan));

	/* select the appropriate RF settings based on what EEPROM says */
	rfprog = (sc->rfprog == 0) ? rt2661_rf5225_1 : rt2661_rf5225_2;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rfprog[i].chan != chan; i++);

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

static void
rt2661_set_bssid(struct rt2661_softc *sc, const uint8_t *bssid)
{
	uint32_t tmp;

	tmp = bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24;
	RAL_WRITE(sc, RT2661_MAC_CSR4, tmp);

	tmp = bssid[4] | bssid[5] << 8 | RT2661_ONE_BSSID << 16;
	RAL_WRITE(sc, RT2661_MAC_CSR5, tmp);
}

static void
rt2661_set_macaddr(struct rt2661_softc *sc, const uint8_t *addr)
{
	uint32_t tmp;

	tmp = addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24;
	RAL_WRITE(sc, RT2661_MAC_CSR2, tmp);

	tmp = addr[4] | addr[5] << 8;
	RAL_WRITE(sc, RT2661_MAC_CSR3, tmp);
}

static void
rt2661_update_promisc(struct ieee80211com *ic)
{
	struct rt2661_softc *sc = ic->ic_softc;
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2661_TXRX_CSR0);

	tmp &= ~RT2661_DROP_NOT_TO_ME;
	if (ic->ic_promisc == 0)
		tmp |= RT2661_DROP_NOT_TO_ME;

	RAL_WRITE(sc, RT2661_TXRX_CSR0, tmp);

	DPRINTF(sc, "%s promiscuous mode\n",
	    (ic->ic_promisc > 0) ?  "entering" : "leaving");
}

/*
 * Update QoS (802.11e) settings for each h/w Tx ring.
 */
static int
rt2661_wme_update(struct ieee80211com *ic)
{
	struct rt2661_softc *sc = ic->ic_softc;
	struct chanAccParams chp;
	const struct wmeParams *wmep;

	ieee80211_wme_ic_getparams(ic, &chp);

	wmep = chp.cap_wmeParams;

	/* XXX: not sure about shifts. */
	/* XXX: the reference driver plays with AC_VI settings too. */

	/* update TxOp */
	RAL_WRITE(sc, RT2661_AC_TXOP_CSR0,
	    wmep[WME_AC_BE].wmep_txopLimit << 16 |
	    wmep[WME_AC_BK].wmep_txopLimit);
	RAL_WRITE(sc, RT2661_AC_TXOP_CSR1,
	    wmep[WME_AC_VI].wmep_txopLimit << 16 |
	    wmep[WME_AC_VO].wmep_txopLimit);

	/* update CWmin */
	RAL_WRITE(sc, RT2661_CWMIN_CSR,
	    wmep[WME_AC_BE].wmep_logcwmin << 12 |
	    wmep[WME_AC_BK].wmep_logcwmin <<  8 |
	    wmep[WME_AC_VI].wmep_logcwmin <<  4 |
	    wmep[WME_AC_VO].wmep_logcwmin);

	/* update CWmax */
	RAL_WRITE(sc, RT2661_CWMAX_CSR,
	    wmep[WME_AC_BE].wmep_logcwmax << 12 |
	    wmep[WME_AC_BK].wmep_logcwmax <<  8 |
	    wmep[WME_AC_VI].wmep_logcwmax <<  4 |
	    wmep[WME_AC_VO].wmep_logcwmax);

	/* update Aifsn */
	RAL_WRITE(sc, RT2661_AIFSN_CSR,
	    wmep[WME_AC_BE].wmep_aifsn << 12 |
	    wmep[WME_AC_BK].wmep_aifsn <<  8 |
	    wmep[WME_AC_VI].wmep_aifsn <<  4 |
	    wmep[WME_AC_VO].wmep_aifsn);

	return 0;
}

static void
rt2661_update_slot(struct ieee80211com *ic)
{
	struct rt2661_softc *sc = ic->ic_softc;
	uint8_t slottime;
	uint32_t tmp;

	slottime = IEEE80211_GET_SLOTTIME(ic);

	tmp = RAL_READ(sc, RT2661_MAC_CSR9);
	tmp = (tmp & ~0xff) | slottime;
	RAL_WRITE(sc, RT2661_MAC_CSR9, tmp);
}

static const char *
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

static void
rt2661_read_eeprom(struct rt2661_softc *sc, uint8_t macaddr[IEEE80211_ADDR_LEN])
{
	uint16_t val;
	int i;

	/* read MAC address */
	val = rt2661_eeprom_read(sc, RT2661_EEPROM_MAC01);
	macaddr[0] = val & 0xff;
	macaddr[1] = val >> 8;

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_MAC23);
	macaddr[2] = val & 0xff;
	macaddr[3] = val >> 8;

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_MAC45);
	macaddr[4] = val & 0xff;
	macaddr[5] = val >> 8;

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_ANTENNA);
	/* XXX: test if different from 0xffff? */
	sc->rf_rev   = (val >> 11) & 0x1f;
	sc->hw_radio = (val >> 10) & 0x1;
	sc->rx_ant   = (val >> 4)  & 0x3;
	sc->tx_ant   = (val >> 2)  & 0x3;
	sc->nb_ant   = val & 0x3;

	DPRINTF(sc, "RF revision=%d\n", sc->rf_rev);

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_CONFIG2);
	sc->ext_5ghz_lna = (val >> 6) & 0x1;
	sc->ext_2ghz_lna = (val >> 4) & 0x1;

	DPRINTF(sc, "External 2GHz LNA=%d\nExternal 5GHz LNA=%d\n",
	    sc->ext_2ghz_lna, sc->ext_5ghz_lna);

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_RSSI_2GHZ_OFFSET);
	if ((val & 0xff) != 0xff)
		sc->rssi_2ghz_corr = (int8_t)(val & 0xff);	/* signed */

	/* Only [-10, 10] is valid */
	if (sc->rssi_2ghz_corr < -10 || sc->rssi_2ghz_corr > 10)
		sc->rssi_2ghz_corr = 0;

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_RSSI_5GHZ_OFFSET);
	if ((val & 0xff) != 0xff)
		sc->rssi_5ghz_corr = (int8_t)(val & 0xff);	/* signed */

	/* Only [-10, 10] is valid */
	if (sc->rssi_5ghz_corr < -10 || sc->rssi_5ghz_corr > 10)
		sc->rssi_5ghz_corr = 0;

	/* adjust RSSI correction for external low-noise amplifier */
	if (sc->ext_2ghz_lna)
		sc->rssi_2ghz_corr -= 14;
	if (sc->ext_5ghz_lna)
		sc->rssi_5ghz_corr -= 14;

	DPRINTF(sc, "RSSI 2GHz corr=%d\nRSSI 5GHz corr=%d\n",
	    sc->rssi_2ghz_corr, sc->rssi_5ghz_corr);

	val = rt2661_eeprom_read(sc, RT2661_EEPROM_FREQ_OFFSET);
	if ((val >> 8) != 0xff)
		sc->rfprog = (val >> 8) & 0x3;
	if ((val & 0xff) != 0xff)
		sc->rffreq = val & 0xff;

	DPRINTF(sc, "RF prog=%d\nRF freq=%d\n", sc->rfprog, sc->rffreq);

	/* read Tx power for all a/b/g channels */
	for (i = 0; i < 19; i++) {
		val = rt2661_eeprom_read(sc, RT2661_EEPROM_TXPOWER + i);
		sc->txpow[i * 2] = (int8_t)(val >> 8);		/* signed */
		DPRINTF(sc, "Channel=%d Tx power=%d\n",
		    rt2661_rf5225_1[i * 2].chan, sc->txpow[i * 2]);
		sc->txpow[i * 2 + 1] = (int8_t)(val & 0xff);	/* signed */
		DPRINTF(sc, "Channel=%d Tx power=%d\n",
		    rt2661_rf5225_1[i * 2 + 1].chan, sc->txpow[i * 2 + 1]);
	}

	/* read vendor-specific BBP values */
	for (i = 0; i < 16; i++) {
		val = rt2661_eeprom_read(sc, RT2661_EEPROM_BBP_BASE + i);
		if (val == 0 || val == 0xffff)
			continue;	/* skip invalid entries */
		sc->bbp_prom[i].reg = val >> 8;
		sc->bbp_prom[i].val = val & 0xff;
		DPRINTF(sc, "BBP R%d=%02x\n", sc->bbp_prom[i].reg,
		    sc->bbp_prom[i].val);
	}
}

static int
rt2661_bbp_init(struct rt2661_softc *sc)
{
	int i, ntries;
	uint8_t val;

	/* wait for BBP to be ready */
	for (ntries = 0; ntries < 100; ntries++) {
		val = rt2661_bbp_read(sc, 0);
		if (val != 0 && val != 0xff)
			break;
		DELAY(100);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev, "timeout waiting for BBP\n");
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

static void
rt2661_init_locked(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t tmp, sta[3];
	int i, error, ntries;

	RAL_LOCK_ASSERT(sc);

	if ((sc->sc_flags & RAL_FW_LOADED) == 0) {
		error = rt2661_load_microcode(sc);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not load 8051 microcode, error %d\n",
			    __func__, error);
			return;
		}
		sc->sc_flags |= RAL_FW_LOADED;
	}

	rt2661_stop_locked(sc);

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

	rt2661_set_macaddr(sc, vap ? vap->iv_myaddr : ic->ic_macaddr);

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
		rt2661_stop_locked(sc);
		return;
	}

	if (rt2661_bbp_init(sc) != 0) {
		rt2661_stop_locked(sc);
		return;
	}

	/* select default channel */
	sc->sc_curchan = ic->ic_curchan;
	rt2661_select_band(sc, sc->sc_curchan);
	rt2661_select_antenna(sc);
	rt2661_set_chan(sc, sc->sc_curchan);

	/* update Rx filter */
	tmp = RAL_READ(sc, RT2661_TXRX_CSR0) & 0xffff;

	tmp |= RT2661_DROP_PHY_ERROR | RT2661_DROP_CRC_ERROR;
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RT2661_DROP_CTL | RT2661_DROP_VER_ERROR |
		       RT2661_DROP_ACKCTS;
		if (ic->ic_opmode != IEEE80211_M_HOSTAP &&
		    ic->ic_opmode != IEEE80211_M_MBSS)
			tmp |= RT2661_DROP_TODS;
		if (ic->ic_promisc == 0)
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

	sc->sc_flags |= RAL_RUNNING;

	callout_reset(&sc->watchdog_ch, hz, rt2661_watchdog, sc);
}

static void
rt2661_init(void *priv)
{
	struct rt2661_softc *sc = priv;
	struct ieee80211com *ic = &sc->sc_ic;

	RAL_LOCK(sc);
	rt2661_init_locked(sc);
	RAL_UNLOCK(sc);

	if (sc->sc_flags & RAL_RUNNING)
		ieee80211_start_all(ic);		/* start all vap's */
}

void
rt2661_stop_locked(struct rt2661_softc *sc)
{
	volatile int *flags = &sc->sc_flags;
	uint32_t tmp;

	while (*flags & RAL_INPUT_RUNNING)
		msleep(sc, &sc->sc_mtx, 0, "ralrunning", hz/10);

	callout_stop(&sc->watchdog_ch);
	sc->sc_tx_timer = 0;

	if (sc->sc_flags & RAL_RUNNING) {
		sc->sc_flags &= ~RAL_RUNNING;

		/* abort Tx (for all 5 Tx rings) */
		RAL_WRITE(sc, RT2661_TX_CNTL_CSR, 0x1f << 16);
		
		/* disable Rx (value remains after reset!) */
		tmp = RAL_READ(sc, RT2661_TXRX_CSR0);
		RAL_WRITE(sc, RT2661_TXRX_CSR0, tmp | RT2661_DISABLE_RX);
		
		/* reset ASIC */
		RAL_WRITE(sc, RT2661_MAC_CSR1, 3);
		RAL_WRITE(sc, RT2661_MAC_CSR1, 0);
		
		/* disable interrupts */
		RAL_WRITE(sc, RT2661_INT_MASK_CSR, 0xffffffff);
		RAL_WRITE(sc, RT2661_MCU_INT_MASK_CSR, 0xffffffff);
		
		/* clear any pending interrupt */
		RAL_WRITE(sc, RT2661_INT_SOURCE_CSR, 0xffffffff);
		RAL_WRITE(sc, RT2661_MCU_INT_SOURCE_CSR, 0xffffffff);
		
		/* reset Tx and Rx rings */
		rt2661_reset_tx_ring(sc, &sc->txq[0]);
		rt2661_reset_tx_ring(sc, &sc->txq[1]);
		rt2661_reset_tx_ring(sc, &sc->txq[2]);
		rt2661_reset_tx_ring(sc, &sc->txq[3]);
		rt2661_reset_tx_ring(sc, &sc->mgtq);
		rt2661_reset_rx_ring(sc, &sc->rxq);
	}
}

void
rt2661_stop(void *priv)
{
	struct rt2661_softc *sc = priv;

	RAL_LOCK(sc);
	rt2661_stop_locked(sc);
	RAL_UNLOCK(sc);
}

static int
rt2661_load_microcode(struct rt2661_softc *sc)
{
	const struct firmware *fp;
	const char *imagename;
	int ntries, error;

	RAL_LOCK_ASSERT(sc);

	switch (sc->sc_id) {
	case 0x0301: imagename = "rt2561sfw"; break;
	case 0x0302: imagename = "rt2561fw"; break;
	case 0x0401: imagename = "rt2661fw"; break;
	default:
		device_printf(sc->sc_dev, "%s: unexpected pci device id 0x%x, "
		    "don't know how to retrieve firmware\n",
		    __func__, sc->sc_id);
		return EINVAL;
	}
	RAL_UNLOCK(sc);
	fp = firmware_get(imagename);
	RAL_LOCK(sc);
	if (fp == NULL) {
		device_printf(sc->sc_dev,
		    "%s: unable to retrieve firmware image %s\n",
		    __func__, imagename);
		return EINVAL;
	}

	/*
	 * Load 8051 microcode into NIC.
	 */
	/* reset 8051 */
	RAL_WRITE(sc, RT2661_MCU_CNTL_CSR, RT2661_MCU_RESET);

	/* cancel any pending Host to MCU command */
	RAL_WRITE(sc, RT2661_H2M_MAILBOX_CSR, 0);
	RAL_WRITE(sc, RT2661_M2H_CMD_DONE_CSR, 0xffffffff);
	RAL_WRITE(sc, RT2661_HOST_CMD_CSR, 0);

	/* write 8051's microcode */
	RAL_WRITE(sc, RT2661_MCU_CNTL_CSR, RT2661_MCU_RESET | RT2661_MCU_SEL);
	RAL_WRITE_REGION_1(sc, RT2661_MCU_CODE_BASE, fp->data, fp->datasize);
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
		device_printf(sc->sc_dev,
		    "%s: timeout waiting for MCU to initialize\n", __func__);
		error = EIO;
	} else
		error = 0;

	firmware_put(fp, FIRMWARE_UNLOAD);
	return error;
}

#ifdef notyet
/*
 * Dynamically tune Rx sensitivity (BBP register 17) based on average RSSI and
 * false CCA count.  This function is called periodically (every seconds) when
 * in the RUN state.  Values taken from the reference driver.
 */
static void
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

	/* retrieve false CCA count since last call (clear on read) */
	cca = RAL_READ(sc, RT2661_STA_CSR1) & 0xffff;

	if (dbm >= -35) {
		bbp17 = 0x60;
	} else if (dbm >= -58) {
		bbp17 = hi;
	} else if (dbm >= -66) {
		bbp17 = lo + 0x10;
	} else if (dbm >= -74) {
		bbp17 = lo + 0x08;
	} else {
		/* RSSI < -74dBm, tune using false CCA count */

		bbp17 = sc->bbp17; /* current value */

		hi -= 2 * (-74 - dbm);
		if (hi < lo)
			hi = lo;

		if (bbp17 > hi) {
			bbp17 = hi;

		} else if (cca > 512) {
			if (++bbp17 > hi)
				bbp17 = hi;
		} else if (cca < 100) {
			if (--bbp17 < lo)
				bbp17 = lo;
		}
	}

	if (bbp17 != sc->bbp17) {
		rt2661_bbp_write(sc, 17, bbp17);
		sc->bbp17 = bbp17;
	}
}

/*
 * Enter/Leave radar detection mode.
 * This is for 802.11h additional regulatory domains.
 */
static void
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

static int
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

static int
rt2661_prepare_beacon(struct rt2661_softc *sc, struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct rt2661_tx_desc desc;
	struct mbuf *m0;
	int rate;

	if ((m0 = ieee80211_beacon_alloc(vap->iv_bss))== NULL) {
		device_printf(sc->sc_dev, "could not allocate beacon frame\n");
		return ENOBUFS;
	}

	/* send beacons at the lowest available rate */
	rate = IEEE80211_IS_CHAN_5GHZ(ic->ic_bsschan) ? 12 : 2;

	rt2661_setup_tx_desc(sc, &desc, RT2661_TX_TIMESTAMP, RT2661_TX_HWSEQ,
	    m0->m_pkthdr.len, rate, NULL, 0, RT2661_QID_MGT);

	/* copy the first 24 bytes of Tx descriptor into NIC memory */
	RAL_WRITE_REGION_1(sc, RT2661_HW_BEACON_BASE0, (uint8_t *)&desc, 24);

	/* copy beacon header and payload into NIC memory */
	RAL_WRITE_REGION_1(sc, RT2661_HW_BEACON_BASE0 + 24,
	    mtod(m0, uint8_t *), m0->m_pkthdr.len);

	m_freem(m0);

	return 0;
}

/*
 * Enable TSF synchronization and tell h/w to start sending beacons for IBSS
 * and HostAP operating modes.
 */
static void
rt2661_enable_tsf_sync(struct rt2661_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t tmp;

	if (vap->iv_opmode != IEEE80211_M_STA) {
		/*
		 * Change default 16ms TBTT adjustment to 8ms.
		 * Must be done before enabling beacon generation.
		 */
		RAL_WRITE(sc, RT2661_TXRX_CSR10, 1 << 12 | 8);
	}

	tmp = RAL_READ(sc, RT2661_TXRX_CSR9) & 0xff000000;

	/* set beacon interval (in 1/16ms unit) */
	tmp |= vap->iv_bss->ni_intval * 16;

	tmp |= RT2661_TSF_TICKING | RT2661_ENABLE_TBTT;
	if (vap->iv_opmode == IEEE80211_M_STA)
		tmp |= RT2661_TSF_MODE(1);
	else
		tmp |= RT2661_TSF_MODE(2) | RT2661_GENERATE_BEACON;

	RAL_WRITE(sc, RT2661_TXRX_CSR9, tmp);
}

static void
rt2661_enable_tsf(struct rt2661_softc *sc)
{
	RAL_WRITE(sc, RT2661_TXRX_CSR9, 
	      (RAL_READ(sc, RT2661_TXRX_CSR9) & 0xff000000)
	    | RT2661_TSF_TICKING | RT2661_TSF_MODE(2));
}

/*
 * Retrieve the "Received Signal Strength Indicator" from the raw values
 * contained in Rx descriptors.  The computation depends on which band the
 * frame was received.  Correction values taken from the reference driver.
 */
static int
rt2661_get_rssi(struct rt2661_softc *sc, uint8_t raw)
{
	int lna, agc, rssi;

	lna = (raw >> 5) & 0x3;
	agc = raw & 0x1f;

	if (lna == 0) {
		/*
		 * No mapping available.
		 *
		 * NB: Since RSSI is relative to noise floor, -1 is
		 *     adequate for caller to know error happened.
		 */
		return -1;
	}

	rssi = (2 * agc) - RT2661_NOISE_FLOOR;

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

static void
rt2661_scan_start(struct ieee80211com *ic)
{
	struct rt2661_softc *sc = ic->ic_softc;
	uint32_t tmp;

	/* abort TSF synchronization */
	tmp = RAL_READ(sc, RT2661_TXRX_CSR9);
	RAL_WRITE(sc, RT2661_TXRX_CSR9, tmp & ~0xffffff);
	rt2661_set_bssid(sc, ieee80211broadcastaddr);
}

static void
rt2661_scan_end(struct ieee80211com *ic)
{
	struct rt2661_softc *sc = ic->ic_softc;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	rt2661_enable_tsf_sync(sc);
	/* XXX keep local copy */
	rt2661_set_bssid(sc, vap->iv_bss->ni_bssid);
}

static void
rt2661_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct rt2661_softc *sc = ic->ic_softc;
	uint8_t bands[IEEE80211_MODE_BYTES];

	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	ieee80211_add_channels_default_2ghz(chans, maxchans, nchans, bands, 0);

	if (sc->rf_rev == RT2661_RF_5225 || sc->rf_rev == RT2661_RF_5325) {
		setbit(bands, IEEE80211_MODE_11A);
		ieee80211_add_channel_list_5ghz(chans, maxchans, nchans,
		    rt2661_chan_5ghz, nitems(rt2661_chan_5ghz), bands, 0);
	}
}

static void
rt2661_set_channel(struct ieee80211com *ic)
{
	struct rt2661_softc *sc = ic->ic_softc;

	RAL_LOCK(sc);
	rt2661_set_chan(sc, ic->ic_curchan);
	RAL_UNLOCK(sc);

}

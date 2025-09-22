/*	$OpenBSD: rt2860.c,v 1.104 2024/09/20 02:00:46 jsg Exp $	*/

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
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
 * Ralink Technology RT2860/RT3090/RT3290/RT3390/RT3562/RT5390/
 *     RT5392 chipset driver
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
#include <sys/queue.h>
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

#include <dev/ic/rt2860var.h>
#include <dev/ic/rt2860reg.h>

#include <dev/pci/pcidevs.h>

#ifdef RAL_DEBUG
#define DPRINTF(x)	do { if (rt2860_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (rt2860_debug >= (n)) printf x; } while (0)
int rt2860_debug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

void		rt2860_attachhook(struct device *);
int		rt2860_alloc_tx_ring(struct rt2860_softc *,
		    struct rt2860_tx_ring *);
void		rt2860_reset_tx_ring(struct rt2860_softc *,
		    struct rt2860_tx_ring *);
void		rt2860_free_tx_ring(struct rt2860_softc *,
		    struct rt2860_tx_ring *);
int		rt2860_alloc_tx_pool(struct rt2860_softc *);
void		rt2860_free_tx_pool(struct rt2860_softc *);
int		rt2860_alloc_rx_ring(struct rt2860_softc *,
		    struct rt2860_rx_ring *);
void		rt2860_reset_rx_ring(struct rt2860_softc *,
		    struct rt2860_rx_ring *);
void		rt2860_free_rx_ring(struct rt2860_softc *,
		    struct rt2860_rx_ring *);
struct		ieee80211_node *rt2860_node_alloc(struct ieee80211com *);
int		rt2860_media_change(struct ifnet *);
void		rt2860_iter_func(void *, struct ieee80211_node *);
void		rt2860_updatestats(struct rt2860_softc *);
void		rt2860_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);
void		rt2860_node_leave(struct ieee80211com *,
		    struct ieee80211_node *);
int		rt2860_ampdu_rx_start(struct ieee80211com *,
		    struct ieee80211_node *, uint8_t);
void		rt2860_ampdu_rx_stop(struct ieee80211com *,
		    struct ieee80211_node *, uint8_t);
int		rt2860_newstate(struct ieee80211com *, enum ieee80211_state,
		    int);
uint16_t	rt3090_efuse_read_2(struct rt2860_softc *, uint16_t);
uint16_t	rt3290_efuse_read_2(struct rt2860_softc *, uint16_t);
uint16_t	rt2860_eeprom_read_2(struct rt2860_softc *, uint16_t);
void		rt2860_intr_coherent(struct rt2860_softc *);
void		rt2860_drain_stats_fifo(struct rt2860_softc *);
void		rt2860_tx_intr(struct rt2860_softc *, int);
void		rt2860_rx_intr(struct rt2860_softc *);
void		rt2860_tbtt_intr(struct rt2860_softc *);
void		rt2860_gp_intr(struct rt2860_softc *);
int		rt2860_tx(struct rt2860_softc *, struct mbuf *,
		    struct ieee80211_node *);
void		rt2860_start(struct ifnet *);
void		rt2860_watchdog(struct ifnet *);
int		rt2860_ioctl(struct ifnet *, u_long, caddr_t);
void		rt2860_mcu_bbp_write(struct rt2860_softc *, uint8_t, uint8_t);
uint8_t		rt2860_mcu_bbp_read(struct rt2860_softc *, uint8_t);
void		rt2860_rf_write(struct rt2860_softc *, uint8_t, uint32_t);
uint8_t		rt3090_rf_read(struct rt2860_softc *, uint8_t);
void		rt3090_rf_write(struct rt2860_softc *, uint8_t, uint8_t);
int		rt2860_mcu_cmd(struct rt2860_softc *, uint8_t, uint16_t, int);
void		rt2860_enable_mrr(struct rt2860_softc *);
void		rt2860_set_txpreamble(struct rt2860_softc *);
void		rt2860_set_basicrates(struct rt2860_softc *);
void		rt2860_select_chan_group(struct rt2860_softc *, int);
void		rt2860_set_chan(struct rt2860_softc *, u_int);
void		rt3090_set_chan(struct rt2860_softc *, u_int);
void		rt5390_set_chan(struct rt2860_softc *, u_int);
int		rt3090_rf_init(struct rt2860_softc *);
void		rt5390_rf_init(struct rt2860_softc *);
void		rt3090_rf_wakeup(struct rt2860_softc *);
void		rt5390_rf_wakeup(struct rt2860_softc *);
int		rt3090_filter_calib(struct rt2860_softc *, uint8_t, uint8_t,
		    uint8_t *);
void		rt3090_rf_setup(struct rt2860_softc *);
void		rt2860_set_leds(struct rt2860_softc *, uint16_t);
void		rt2860_set_gp_timer(struct rt2860_softc *, int);
void		rt2860_set_bssid(struct rt2860_softc *, const uint8_t *);
void		rt2860_set_macaddr(struct rt2860_softc *, const uint8_t *);
void		rt2860_updateslot(struct ieee80211com *);
void		rt2860_updateprot(struct ieee80211com *);
void		rt2860_updateedca(struct ieee80211com *);
int		rt2860_set_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		rt2860_delete_key(struct ieee80211com *,
		    struct ieee80211_node *, struct ieee80211_key *);
#if NBPFILTER > 0
int8_t		rt2860_rssi2dbm(struct rt2860_softc *, uint8_t, uint8_t);
#endif
const char *	rt2860_get_rf(uint16_t);
int		rt2860_read_eeprom(struct rt2860_softc *);
int		rt2860_bbp_init(struct rt2860_softc *);
void		rt5390_bbp_init(struct rt2860_softc *);
int		rt3290_wlan_enable(struct rt2860_softc *);
int		rt2860_txrx_enable(struct rt2860_softc *);
int		rt2860_init(struct ifnet *);
void		rt2860_stop(struct ifnet *, int);
int		rt2860_load_microcode(struct rt2860_softc *);
void		rt2860_calib(struct rt2860_softc *);
void		rt3090_set_rx_antenna(struct rt2860_softc *, int);
void		rt2860_switch_chan(struct rt2860_softc *,
		    struct ieee80211_channel *);
#ifndef IEEE80211_STA_ONLY
int		rt2860_setup_beacon(struct rt2860_softc *);
#endif
void		rt2860_enable_tsf_sync(struct rt2860_softc *);

static const struct {
	uint32_t	reg;
	uint32_t	val;
} rt2860_def_mac[] = {
	RT2860_DEF_MAC
};

static const struct {
	uint8_t	reg;
	uint8_t	val;
} rt2860_def_bbp[] = {
	RT2860_DEF_BBP
},rt3290_def_bbp[] = {
	RT3290_DEF_BBP
},rt5390_def_bbp[] = {
	RT5390_DEF_BBP
};

static const struct rfprog {
	uint8_t		chan;
	uint32_t	r1, r2, r3, r4;
} rt2860_rf2850[] = {
	RT2860_RF2850
};

struct {
	uint8_t	n, r, k;
} rt3090_freqs[] = {
	RT3070_RF3052
};

static const struct {
	uint8_t	reg;
	uint8_t	val;
}  rt3090_def_rf[] = {
	RT3070_DEF_RF
}, rt3290_def_rf[] = {
	RT3290_DEF_RF
}, rt3572_def_rf[] = {
	RT3572_DEF_RF
}, rt5390_def_rf[] = {
	RT5390_DEF_RF
}, rt5392_def_rf[] = {
	RT5392_DEF_RF
};

int
rt2860_attach(void *xsc, int id)
{
	struct rt2860_softc *sc = xsc;
	struct ieee80211com *ic = &sc->sc_ic;
	int qid, ntries, error;
	uint32_t tmp, reg;

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 15;

	if (id == PCI_PRODUCT_RALINK_RT3290)
		reg = RT2860_PCI_CFG;
	else
		reg = RT2860_ASIC_VER_ID;

	/* wait for NIC to initialize */
	for (ntries = 0; ntries < 100; ntries++) {
		tmp = RAL_READ(sc, reg);
		if (tmp != 0 && tmp != 0xffffffff)
			break;
		DELAY(10);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for NIC to initialize\n",
		    sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}
	sc->mac_ver = tmp >> 16;
	sc->mac_rev = tmp & 0xffff;

	if (sc->mac_ver != 0x2860 &&
	    (id == PCI_PRODUCT_RALINK_RT2890 ||
	     id == PCI_PRODUCT_RALINK_RT2790 ||
	     id == PCI_PRODUCT_AWT_RT2890))
		sc->sc_flags |= RT2860_ADVANCED_PS;

	/* retrieve RF rev. no and various other things from EEPROM */
	rt2860_read_eeprom(sc);
	printf(", address %s\n", ether_sprintf(ic->ic_myaddr));
	printf("%s: MAC/BBP RT%X (rev 0x%04X), RF %s (MIMO %dT%dR)\n",
	    sc->sc_dev.dv_xname, sc->mac_ver, sc->mac_rev,
	    rt2860_get_rf(sc->rf_rev), sc->ntxchains, sc->nrxchains);

	/*
	 * Allocate Tx (4 EDCAs + HCCA + Mgt) and Rx rings.
	 */
	for (qid = 0; qid < 6; qid++) {
		if ((error = rt2860_alloc_tx_ring(sc, &sc->txq[qid])) != 0) {
			printf("%s: could not allocate Tx ring %d\n",
			    sc->sc_dev.dv_xname, qid);
			goto fail1;
		}
	}

	if ((error = rt2860_alloc_rx_ring(sc, &sc->rxq)) != 0) {
		printf("%s: could not allocate Rx ring\n",
		    sc->sc_dev.dv_xname);
		goto fail1;
	}

	if ((error = rt2860_alloc_tx_pool(sc)) != 0) {
		printf("%s: could not allocate Tx pool\n",
		    sc->sc_dev.dv_xname);
		goto fail2;
	}

	/* mgmt ring is broken on RT2860C, use EDCA AC VO ring instead */
	sc->mgtqid = (sc->mac_ver == 0x2860 && sc->mac_rev == 0x0100) ?
	    EDCA_AC_VO : 5;

	config_mountroot(xsc, rt2860_attachhook);

	return 0;

fail2:	rt2860_free_rx_ring(sc, &sc->rxq);
fail1:	while (--qid >= 0)
		rt2860_free_tx_ring(sc, &sc->txq[qid]);
	return error;
}

void
rt2860_attachhook(struct device *self)
{
	struct rt2860_softc *sc = (struct rt2860_softc *)self;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int i, error;

	if (sc->mac_ver == 0x3290) {
		error = loadfirmware("ral-rt3290", &sc->ucode, &sc->ucsize);
	} else {
		error = loadfirmware("ral-rt2860", &sc->ucode, &sc->ucsize);
	}
	if (error != 0) {
		printf("%s: error %d, could not read firmware file %s\n",
		    sc->sc_dev.dv_xname, error, "ral-rt2860");
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
	    IEEE80211_C_APPMGT |	/* HostAP power management */
#endif
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WEP |		/* s/w WEP */
	    IEEE80211_C_RSN;		/* WPA/RSN */

	if (sc->rf_rev == RT2860_RF_2750 || sc->rf_rev == RT2860_RF_2850) {
		/* set supported .11a rates */
		ic->ic_sup_rates[IEEE80211_MODE_11A] =
		    ieee80211_std_rateset_11a;

		/* set supported .11a channels */
		for (i = 14; i < nitems(rt2860_rf2850); i++) {
			uint8_t chan = rt2860_rf2850[i].chan;
			ic->ic_channels[chan].ic_freq =
			    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[chan].ic_flags = IEEE80211_CHAN_A;
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

	/* HW supports up to 255 STAs (0-254) in HostAP and IBSS modes */
	ic->ic_max_aid = min(IEEE80211_AID_MAX, RT2860_WCID_MAX);

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = rt2860_ioctl;
	ifp->if_start = rt2860_start;
	ifp->if_watchdog = rt2860_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_node_alloc = rt2860_node_alloc;
	ic->ic_newassoc = rt2860_newassoc;
#ifndef IEEE80211_STA_ONLY
	ic->ic_node_leave = rt2860_node_leave;
#endif
	ic->ic_ampdu_rx_start = rt2860_ampdu_rx_start;
	ic->ic_ampdu_rx_stop = rt2860_ampdu_rx_stop;
	ic->ic_updateslot = rt2860_updateslot;
	ic->ic_updateedca = rt2860_updateedca;
	ic->ic_set_key = rt2860_set_key;
	ic->ic_delete_key = rt2860_delete_key;
	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = rt2860_newstate;
	ieee80211_media_init(ifp, rt2860_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + 64);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(RT2860_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(RT2860_TX_RADIOTAP_PRESENT);
#endif
}

int
rt2860_detach(void *xsc)
{
	struct rt2860_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int qid;

	ieee80211_ifdetach(ifp);	/* free all nodes */
	if_detach(ifp);

	for (qid = 0; qid < 6; qid++)
		rt2860_free_tx_ring(sc, &sc->txq[qid]);
	rt2860_free_rx_ring(sc, &sc->rxq);
	rt2860_free_tx_pool(sc);

	if (sc->ucode != NULL)
		free(sc->ucode, M_DEVBUF, sc->ucsize);

	return 0;
}

void
rt2860_suspend(void *xsc)
{
	struct rt2860_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (ifp->if_flags & IFF_RUNNING)
		rt2860_stop(ifp, 1);
}

void
rt2860_wakeup(void *xsc)
{
	struct rt2860_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (ifp->if_flags & IFF_UP)
		rt2860_init(ifp);
}

int
rt2860_alloc_tx_ring(struct rt2860_softc *sc, struct rt2860_tx_ring *ring)
{
	int nsegs, size, error;

	size = RT2860_TX_RING_COUNT * sizeof (struct rt2860_txd);

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &ring->map);
	if (error != 0) {
		printf("%s: could not create DMA map\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	/* Tx rings must be 4-DWORD aligned */
	error = bus_dmamem_alloc(sc->sc_dmat, size, 16, 0, &ring->seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs, size,
	    (caddr_t *)&ring->txd, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map DMA memory\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, ring->txd, size, NULL,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load DMA map\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, size, BUS_DMASYNC_PREWRITE);

	ring->paddr = ring->map->dm_segs[0].ds_addr;

	return 0;

fail:	rt2860_free_tx_ring(sc, ring);
	return error;
}

void
rt2860_reset_tx_ring(struct rt2860_softc *sc, struct rt2860_tx_ring *ring)
{
	struct rt2860_tx_data *data;
	int i;

	for (i = 0; i < RT2860_TX_RING_COUNT; i++) {
		if ((data = ring->data[i]) == NULL)
			continue;	/* nothing mapped in this slot */

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, data->map);
		m_freem(data->m);
		data->m= NULL;
		data->ni = NULL;	/* node already freed */

		SLIST_INSERT_HEAD(&sc->data_pool, data, next);
		ring->data[i] = NULL;
	}

	ring->queued = 0;
	ring->cur = ring->next = 0;
}

void
rt2860_free_tx_ring(struct rt2860_softc *sc, struct rt2860_tx_ring *ring)
{
	struct rt2860_tx_data *data;
	int i;

	if (ring->txd != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)ring->txd,
		    RT2860_TX_RING_COUNT * sizeof (struct rt2860_txd));
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}
	if (ring->map != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ring->map);

	for (i = 0; i < RT2860_TX_RING_COUNT; i++) {
		if ((data = ring->data[i]) == NULL)
			continue;	/* nothing mapped in this slot */

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, data->map);
		m_freem(data->m);

		SLIST_INSERT_HEAD(&sc->data_pool, data, next);
	}
}

/*
 * Allocate a pool of TX Wireless Information blocks.
 */
int
rt2860_alloc_tx_pool(struct rt2860_softc *sc)
{
	caddr_t vaddr;
	bus_addr_t paddr;
	int i, nsegs, size, error;

	size = RT2860_TX_POOL_COUNT * RT2860_TXWI_DMASZ;

	/* init data_pool early in case of failure.. */
	SLIST_INIT(&sc->data_pool);

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &sc->txwi_map);
	if (error != 0) {
		printf("%s: could not create DMA map\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0,
	    &sc->txwi_seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->txwi_seg, nsegs, size,
	    &sc->txwi_vaddr, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map DMA memory\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, sc->txwi_map, sc->txwi_vaddr,
	    size, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load DMA map\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	bus_dmamap_sync(sc->sc_dmat, sc->txwi_map, 0, size,
	    BUS_DMASYNC_PREWRITE);

	vaddr = sc->txwi_vaddr;
	paddr = sc->txwi_map->dm_segs[0].ds_addr;
	for (i = 0; i < RT2860_TX_POOL_COUNT; i++) {
		struct rt2860_tx_data *data = &sc->data[i];

		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    RT2860_MAX_SCATTER, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &data->map); /* <0> */
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
		data->txwi = (struct rt2860_txwi *)vaddr;
		data->paddr = paddr;
		vaddr += RT2860_TXWI_DMASZ;
		paddr += RT2860_TXWI_DMASZ;

		SLIST_INSERT_HEAD(&sc->data_pool, data, next);
	}

	return 0;

fail:	rt2860_free_tx_pool(sc);
	return error;
}

void
rt2860_free_tx_pool(struct rt2860_softc *sc)
{
	if (sc->txwi_vaddr != NULL) {
		bus_dmamap_sync(sc->sc_dmat, sc->txwi_map, 0,
		    sc->txwi_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, sc->txwi_map);
		bus_dmamem_unmap(sc->sc_dmat, sc->txwi_vaddr,
		    RT2860_TX_POOL_COUNT * RT2860_TXWI_DMASZ);
		bus_dmamem_free(sc->sc_dmat, &sc->txwi_seg, 1);
	}
	if (sc->txwi_map != NULL)
		bus_dmamap_destroy(sc->sc_dmat, sc->txwi_map);

	while (!SLIST_EMPTY(&sc->data_pool)) {
		struct rt2860_tx_data *data;
		data = SLIST_FIRST(&sc->data_pool);
		bus_dmamap_destroy(sc->sc_dmat, data->map);
		SLIST_REMOVE_HEAD(&sc->data_pool, next);
	}
}

int
rt2860_alloc_rx_ring(struct rt2860_softc *sc, struct rt2860_rx_ring *ring)
{
	int i, nsegs, size, error;

	size = RT2860_RX_RING_COUNT * sizeof (struct rt2860_rxd);

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &ring->map);
	if (error != 0) {
		printf("%s: could not create DMA map\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	/* Rx ring must be 4-DWORD aligned */
	error = bus_dmamem_alloc(sc->sc_dmat, size, 16, 0, &ring->seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs, size,
	    (caddr_t *)&ring->rxd, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map DMA memory\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, ring->rxd, size, NULL,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load DMA map\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	ring->paddr = ring->map->dm_segs[0].ds_addr;

	for (i = 0; i < RT2860_RX_RING_COUNT; i++) {
		struct rt2860_rx_data *data = &ring->data[i];
		struct rt2860_rxd *rxd = &ring->rxd[i];

		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &data->map);
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		data->m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
		if (data->m == NULL) {
			printf("%s: could not allocate Rx mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOBUFS;
			goto fail;
		}

		error = bus_dmamap_load(sc->sc_dmat, data->map,
		    mtod(data->m, void *), MCLBYTES, NULL,
		    BUS_DMA_READ | BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: could not load DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		rxd->sdp0 = htole32(data->map->dm_segs[0].ds_addr);
		rxd->sdl0 = htole16(MCLBYTES);
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, size, BUS_DMASYNC_PREWRITE);

	return 0;

fail:	rt2860_free_rx_ring(sc, ring);
	return error;
}

void
rt2860_reset_rx_ring(struct rt2860_softc *sc, struct rt2860_rx_ring *ring)
{
	int i;

	for (i = 0; i < RT2860_RX_RING_COUNT; i++)
		ring->rxd[i].sdl0 &= ~htole16(RT2860_RX_DDONE);

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ring->cur = 0;
}

void
rt2860_free_rx_ring(struct rt2860_softc *sc, struct rt2860_rx_ring *ring)
{
	int i;

	if (ring->rxd != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)ring->rxd,
		    RT2860_RX_RING_COUNT * sizeof (struct rt2860_rxd));
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}
	if (ring->map != NULL)
		bus_dmamap_destroy(sc->sc_dmat, ring->map);

	for (i = 0; i < RT2860_RX_RING_COUNT; i++) {
		struct rt2860_rx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
		}
		if (data->map != NULL)
			bus_dmamap_destroy(sc->sc_dmat, data->map);
	}
}

struct ieee80211_node *
rt2860_node_alloc(struct ieee80211com *ic)
{
	return malloc(sizeof (struct rt2860_node), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
}

int
rt2860_media_change(struct ifnet *ifp)
{
	struct rt2860_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rate, ridx;
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if (ic->ic_fixed_rate != -1) {
		rate = ic->ic_sup_rates[ic->ic_curmode].
		    rs_rates[ic->ic_fixed_rate] & IEEE80211_RATE_VAL;
		for (ridx = 0; ridx <= RT2860_RIDX_MAX; ridx++)
			if (rt2860_rates[ridx].rate == rate)
				break;
		sc->fixed_ridx = ridx;
	}

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING)) {
		rt2860_stop(ifp, 0);
		rt2860_init(ifp);
	}
	return 0;
}

void
rt2860_iter_func(void *arg, struct ieee80211_node *ni)
{
	struct rt2860_softc *sc = arg;
	uint8_t wcid = ((struct rt2860_node *)ni)->wcid;

	ieee80211_amrr_choose(&sc->amrr, ni, &sc->amn[wcid]);
}

void
rt2860_updatestats(struct rt2860_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

#ifndef IEEE80211_STA_ONLY
	/*
	 * In IBSS or HostAP modes (when the hardware sends beacons), the
	 * MAC can run into a livelock and start sending CTS-to-self frames
	 * like crazy if protection is enabled.  Fortunately, we can detect
	 * when such a situation occurs and reset the MAC.
	 */
	if (ic->ic_curmode != IEEE80211_M_STA) {
		/* check if we're in a livelock situation.. */
		uint32_t tmp = RAL_READ(sc, RT2860_DEBUG);
		if ((tmp & (1 << 29)) && (tmp & (1 << 7 | 1 << 5))) {
			/* ..and reset MAC/BBP for a while.. */
			DPRINTF(("CTS-to-self livelock detected\n"));
			RAL_WRITE(sc, RT2860_MAC_SYS_CTRL, RT2860_MAC_SRST);
			RAL_BARRIER_WRITE(sc);
			DELAY(1);
			RAL_WRITE(sc, RT2860_MAC_SYS_CTRL,
			    RT2860_MAC_RX_EN | RT2860_MAC_TX_EN);
		}
	}
#endif
	if (ic->ic_opmode == IEEE80211_M_STA)
		rt2860_iter_func(sc, ic->ic_bss);
#ifndef IEEE80211_STA_ONLY
	else
		ieee80211_iterate_nodes(ic, rt2860_iter_func, sc);
#endif
}

void
rt2860_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct rt2860_softc *sc = ic->ic_softc;
	struct rt2860_node *rn = (void *)ni;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	uint8_t rate, wcid = 0;
	int ridx, i, j;

	if (isnew && ni->ni_associd != 0) {
		/* only interested in true associations */
		wcid = rn->wcid = IEEE80211_AID(ni->ni_associd);

		/* init WCID table entry */
		RAL_WRITE_REGION_1(sc, RT2860_WCID_ENTRY(wcid),
		    ni->ni_macaddr, IEEE80211_ADDR_LEN);
	}
	DPRINTF(("new assoc isnew=%d addr=%s WCID=%d\n",
	    isnew, ether_sprintf(ni->ni_macaddr), wcid));

	ieee80211_amrr_node_init(&sc->amrr, &sc->amn[wcid]);
	/* start at lowest available bit-rate, AMRR will raise */
	ni->ni_txrate = 0;

	for (i = 0; i < rs->rs_nrates; i++) {
		rate = rs->rs_rates[i] & IEEE80211_RATE_VAL;
		/* convert 802.11 rate to hardware rate index */
		for (ridx = 0; ridx < RT2860_RIDX_MAX; ridx++)
			if (rt2860_rates[ridx].rate == rate)
				break;
		rn->ridx[i] = ridx;
		/* determine rate of control response frames */
		for (j = i; j >= 0; j--) {
			if ((rs->rs_rates[j] & IEEE80211_RATE_BASIC) &&
			    rt2860_rates[rn->ridx[i]].phy ==
			    rt2860_rates[rn->ridx[j]].phy)
				break;
		}
		if (j >= 0) {
			rn->ctl_ridx[i] = rn->ridx[j];
		} else {
			/* no basic rate found, use mandatory one */
			rn->ctl_ridx[i] = rt2860_rates[ridx].ctl_ridx;
		}
		DPRINTF(("rate=0x%02x ridx=%d ctl_ridx=%d\n",
		    rs->rs_rates[i], rn->ridx[i], rn->ctl_ridx[i]));
	}
}

#ifndef IEEE80211_STA_ONLY
void
rt2860_node_leave(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct rt2860_softc *sc = ic->ic_softc;
	uint8_t wcid = ((struct rt2860_node *)ni)->wcid;

	/* clear Rx WCID search table entry */
	RAL_SET_REGION_4(sc, RT2860_WCID_ENTRY(wcid), 0, 2);
}
#endif

int
rt2860_ampdu_rx_start(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct rt2860_softc *sc = ic->ic_softc;
	uint8_t wcid = ((struct rt2860_node *)ni)->wcid;
	uint32_t tmp;

	/* update BA session mask */
	tmp = RAL_READ(sc, RT2860_WCID_ENTRY(wcid) + 4);
	tmp |= (1 << tid) << 16;
	RAL_WRITE(sc, RT2860_WCID_ENTRY(wcid) + 4, tmp);
	return 0;
}

void
rt2860_ampdu_rx_stop(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct rt2860_softc *sc = ic->ic_softc;
	uint8_t wcid = ((struct rt2860_node *)ni)->wcid;
	uint32_t tmp;

	/* update BA session mask */
	tmp = RAL_READ(sc, RT2860_WCID_ENTRY(wcid) + 4);
	tmp &= ~((1 << tid) << 16);
	RAL_WRITE(sc, RT2860_WCID_ENTRY(wcid) + 4, tmp);
}

int
rt2860_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct rt2860_softc *sc = ic->ic_if.if_softc;
	enum ieee80211_state ostate;
	uint32_t tmp;

	ostate = ic->ic_state;

	if (ostate == IEEE80211_S_RUN) {
		/* turn link LED off */
		rt2860_set_leds(sc, RT2860_LED_RADIO);
	}

	switch (nstate) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* abort TSF synchronization */
			tmp = RAL_READ(sc, RT2860_BCN_TIME_CFG);
			RAL_WRITE(sc, RT2860_BCN_TIME_CFG,
			    tmp & ~(RT2860_BCN_TX_EN | RT2860_TSF_TIMER_EN |
			    RT2860_TBTT_TIMER_EN));
		}
		rt2860_set_gp_timer(sc, 0);
		break;

	case IEEE80211_S_SCAN:
		rt2860_switch_chan(sc, ic->ic_bss->ni_chan);
		if (ostate != IEEE80211_S_SCAN)
			rt2860_set_gp_timer(sc, 150);
		break;

	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		rt2860_set_gp_timer(sc, 0);
		rt2860_switch_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_RUN:
		rt2860_set_gp_timer(sc, 0);
		rt2860_switch_chan(sc, ic->ic_bss->ni_chan);

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			rt2860_updateslot(ic);
			rt2860_enable_mrr(sc);
			rt2860_set_txpreamble(sc);
			rt2860_set_basicrates(sc);
			rt2860_set_bssid(sc, ic->ic_bss->ni_bssid);
		}

#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS)
			(void)rt2860_setup_beacon(sc);
#endif

		if (ic->ic_opmode == IEEE80211_M_STA) {
			/* fake a join to init the tx rate */
			rt2860_newassoc(ic, ic->ic_bss, 1);
		}

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			rt2860_enable_tsf_sync(sc);
			rt2860_set_gp_timer(sc, 500);
		}

		/* turn link LED on */
		rt2860_set_leds(sc, RT2860_LED_RADIO |
		    (IEEE80211_IS_CHAN_2GHZ(ic->ic_bss->ni_chan) ?
		     RT2860_LED_LINK_2GHZ : RT2860_LED_LINK_5GHZ));
		break;
	}

	return sc->sc_newstate(ic, nstate, arg);
}

/* Read 16-bit from eFUSE ROM (>=RT3071 only.) */
uint16_t
rt3090_efuse_read_2(struct rt2860_softc *sc, uint16_t addr)
{
	uint32_t tmp;
	uint16_t reg;
	int ntries;

	addr *= 2;
	/*-
	 * Read one 16-byte block into registers EFUSE_DATA[0-3]:
	 * DATA0: F E D C
	 * DATA1: B A 9 8
	 * DATA2: 7 6 5 4
	 * DATA3: 3 2 1 0
	 */
	tmp = RAL_READ(sc, RT3070_EFUSE_CTRL);
	tmp &= ~(RT3070_EFSROM_MODE_MASK | RT3070_EFSROM_AIN_MASK);
	tmp |= (addr & ~0xf) << RT3070_EFSROM_AIN_SHIFT | RT3070_EFSROM_KICK;
	RAL_WRITE(sc, RT3070_EFUSE_CTRL, tmp);
	for (ntries = 0; ntries < 500; ntries++) {
		tmp = RAL_READ(sc, RT3070_EFUSE_CTRL);
		if (!(tmp & RT3070_EFSROM_KICK))
			break;
		DELAY(2);
	}
	if (ntries == 500)
		return 0xffff;

	if ((tmp & RT3070_EFUSE_AOUT_MASK) == RT3070_EFUSE_AOUT_MASK)
		return 0xffff;	/* address not found */

	/* determine to which 32-bit register our 16-bit word belongs */
	reg = RT3070_EFUSE_DATA3 - (addr & 0xc);
	tmp = RAL_READ(sc, reg);

	return (addr & 2) ? tmp >> 16 : tmp & 0xffff;
}

/* Read 16 bits from eFUSE ROM (RT3290 only) */
uint16_t
rt3290_efuse_read_2(struct rt2860_softc *sc, uint16_t addr)
{
	uint32_t tmp;
	uint16_t reg;
	int ntries;

	addr *= 2;
	/*-
	 * Read one 16-byte block into registers EFUSE_DATA[0-3]:
	 * DATA3: 3 2 1 0
	 * DATA2: 7 6 5 4
	 * DATA1: B A 9 8
	 * DATA0: F E D C
	 */
	tmp = RAL_READ(sc, RT3290_EFUSE_CTRL);
	tmp &= ~(RT3070_EFSROM_MODE_MASK | RT3070_EFSROM_AIN_MASK);
	tmp |= (addr & ~0xf) << RT3070_EFSROM_AIN_SHIFT | RT3070_EFSROM_KICK;
	RAL_WRITE(sc, RT3290_EFUSE_CTRL, tmp);
	for (ntries = 0; ntries < 500; ntries++) {
		tmp = RAL_READ(sc, RT3290_EFUSE_CTRL);
		if (!(tmp & RT3070_EFSROM_KICK))
			break;
		DELAY(2);
	}
	if (ntries == 500)
		return 0xffff;

	if ((tmp & RT3070_EFUSE_AOUT_MASK) == RT3070_EFUSE_AOUT_MASK)
		return 0xffff;	/* address not found */

	/* determine to which 32-bit register our 16-bit word belongs */
	reg = RT3290_EFUSE_DATA3 + (addr & 0xc);
	tmp = RAL_READ(sc, reg);

	return (addr & 2) ? tmp >> 16 : tmp & 0xffff;
}

/*
 * Read 16 bits at address 'addr' from the serial EEPROM (either 93C46,
 * 93C66 or 93C86).
 */
uint16_t
rt2860_eeprom_read_2(struct rt2860_softc *sc, uint16_t addr)
{
	uint32_t tmp;
	uint16_t val;
	int n;

	/* clock C once before the first command */
	RT2860_EEPROM_CTL(sc, 0);

	RT2860_EEPROM_CTL(sc, RT2860_S);
	RT2860_EEPROM_CTL(sc, RT2860_S | RT2860_C);
	RT2860_EEPROM_CTL(sc, RT2860_S);

	/* write start bit (1) */
	RT2860_EEPROM_CTL(sc, RT2860_S | RT2860_D);
	RT2860_EEPROM_CTL(sc, RT2860_S | RT2860_D | RT2860_C);

	/* write READ opcode (10) */
	RT2860_EEPROM_CTL(sc, RT2860_S | RT2860_D);
	RT2860_EEPROM_CTL(sc, RT2860_S | RT2860_D | RT2860_C);
	RT2860_EEPROM_CTL(sc, RT2860_S);
	RT2860_EEPROM_CTL(sc, RT2860_S | RT2860_C);

	/* write address (A5-A0 or A7-A0) */
	n = ((RAL_READ(sc, RT2860_PCI_EECTRL) & 0x30) == 0) ? 5 : 7;
	for (; n >= 0; n--) {
		RT2860_EEPROM_CTL(sc, RT2860_S |
		    (((addr >> n) & 1) << RT2860_SHIFT_D));
		RT2860_EEPROM_CTL(sc, RT2860_S |
		    (((addr >> n) & 1) << RT2860_SHIFT_D) | RT2860_C);
	}

	RT2860_EEPROM_CTL(sc, RT2860_S);

	/* read data Q15-Q0 */
	val = 0;
	for (n = 15; n >= 0; n--) {
		RT2860_EEPROM_CTL(sc, RT2860_S | RT2860_C);
		tmp = RAL_READ(sc, RT2860_PCI_EECTRL);
		val |= ((tmp & RT2860_Q) >> RT2860_SHIFT_Q) << n;
		RT2860_EEPROM_CTL(sc, RT2860_S);
	}

	RT2860_EEPROM_CTL(sc, 0);

	/* clear Chip Select and clock C */
	RT2860_EEPROM_CTL(sc, RT2860_S);
	RT2860_EEPROM_CTL(sc, 0);
	RT2860_EEPROM_CTL(sc, RT2860_C);

	return val;
}

static __inline uint16_t
rt2860_srom_read(struct rt2860_softc *sc, uint8_t addr)
{
	/* either eFUSE ROM or EEPROM */
	return sc->sc_srom_read(sc, addr);
}

void
rt2860_intr_coherent(struct rt2860_softc *sc)
{
	uint32_t tmp;

	/* DMA finds data coherent event when checking the DDONE bit */

	DPRINTF(("Tx/Rx Coherent interrupt\n"));

	/* restart DMA engine */
	tmp = RAL_READ(sc, RT2860_WPDMA_GLO_CFG);
	tmp &= ~(RT2860_TX_WB_DDONE | RT2860_RX_DMA_EN | RT2860_TX_DMA_EN);
	RAL_WRITE(sc, RT2860_WPDMA_GLO_CFG, tmp);

	(void)rt2860_txrx_enable(sc);
}

void
rt2860_drain_stats_fifo(struct rt2860_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct ieee80211_amrr_node *amn;
	uint32_t stat;
	uint8_t wcid, mcs, pid;

	/* drain Tx status FIFO (maxsize = 16) */
	while ((stat = RAL_READ(sc, RT2860_TX_STAT_FIFO)) & RT2860_TXQ_VLD) {
		DPRINTFN(4, ("tx stat 0x%08x\n", stat));

		wcid = (stat >> RT2860_TXQ_WCID_SHIFT) & 0xff;

		/* if no ACK was requested, no feedback is available */
		if (!(stat & RT2860_TXQ_ACKREQ) || wcid == 0xff)
			continue;

		/* update per-STA AMRR stats */
		amn = &sc->amn[wcid];
		amn->amn_txcnt++;
		if (stat & RT2860_TXQ_OK) {
			/*
			 * Check if there were retries, ie if the Tx success
			 * rate is different from the requested rate.  Note
			 * that it works only because we do not allow rate
			 * fallback from OFDM to CCK.
			 */
			mcs = (stat >> RT2860_TXQ_MCS_SHIFT) & 0x7f;
			pid = (stat >> RT2860_TXQ_PID_SHIFT) & 0xf;
			if (mcs + 1 != pid)
				amn->amn_retrycnt++;
		} else {
			amn->amn_retrycnt++;
			ifp->if_oerrors++;
		}
	}
}

void
rt2860_tx_intr(struct rt2860_softc *sc, int qid)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct rt2860_tx_ring *ring = &sc->txq[qid];
	uint32_t hw;

	rt2860_drain_stats_fifo(sc);

	hw = RAL_READ(sc, RT2860_TX_DTX_IDX(qid));
	while (ring->next != hw) {
		struct rt2860_tx_data *data = ring->data[ring->next];

		if (data != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
			data->m= NULL;
			ieee80211_release_node(ic, data->ni);
			data->ni = NULL;

			SLIST_INSERT_HEAD(&sc->data_pool, data, next);
			ring->data[ring->next] = NULL;
		}
		ring->queued--;
		ring->next = (ring->next + 1) % RT2860_TX_RING_COUNT;
	}

	sc->sc_tx_timer = 0;
	if (ring->queued <= RT2860_TX_RING_ONEMORE)
		sc->qfullmsk &= ~(1 << qid);
	ifq_clr_oactive(&ifp->if_snd);
	rt2860_start(ifp);
}

/*
 * Return the Rx chain with the highest RSSI for a given frame.
 */
static __inline uint8_t
rt2860_maxrssi_chain(struct rt2860_softc *sc, const struct rt2860_rxwi *rxwi)
{
	uint8_t rxchain = 0;

	if (sc->nrxchains > 1) {
		if (rxwi->rssi[1] > rxwi->rssi[rxchain])
			rxchain = 1;
		if (sc->nrxchains > 2)
			if (rxwi->rssi[2] > rxwi->rssi[rxchain])
				rxchain = 2;
	}
	return rxchain;
}

void
rt2860_rx_intr(struct rt2860_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct mbuf *m, *m1;
	uint32_t hw;
	uint8_t ant, rssi;
	int error;
#if NBPFILTER > 0
	struct rt2860_rx_radiotap_header *tap;
	uint16_t phy;
#endif

	hw = RAL_READ(sc, RT2860_FS_DRX_IDX) & 0xfff;
	while (sc->rxq.cur != hw) {
		struct rt2860_rx_data *data = &sc->rxq.data[sc->rxq.cur];
		struct rt2860_rxd *rxd = &sc->rxq.rxd[sc->rxq.cur];
		struct rt2860_rxwi *rxwi;

		bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
		    sc->rxq.cur * sizeof (struct rt2860_rxd),
		    sizeof (struct rt2860_rxd), BUS_DMASYNC_POSTREAD);

		if (__predict_false(!(rxd->sdl0 & htole16(RT2860_RX_DDONE)))) {
			DPRINTF(("RXD DDONE bit not set!\n"));
			break;	/* should not happen */
		}

		if (__predict_false(rxd->flags &
		    htole32(RT2860_RX_CRCERR | RT2860_RX_ICVERR))) {
			ifp->if_ierrors++;
			goto skip;
		}

		if (__predict_false(rxd->flags & htole32(RT2860_RX_MICERR))) {
			/* report MIC failures to net80211 for TKIP */
			ic->ic_stats.is_rx_locmicfail++;
			ieee80211_michael_mic_failure(ic, 0/* XXX */);
			ifp->if_ierrors++;
			goto skip;
		}

		m1 = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
		if (__predict_false(m1 == NULL)) {
			ifp->if_ierrors++;
			goto skip;
		}

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, data->map);

		error = bus_dmamap_load(sc->sc_dmat, data->map,
		    mtod(m1, void *), MCLBYTES, NULL,
		    BUS_DMA_READ | BUS_DMA_NOWAIT);
		if (__predict_false(error != 0)) {
			m_freem(m1);

			/* try to reload the old mbuf */
			error = bus_dmamap_load(sc->sc_dmat, data->map,
			    mtod(data->m, void *), MCLBYTES, NULL,
			    BUS_DMA_READ | BUS_DMA_NOWAIT);
			if (__predict_false(error != 0)) {
				panic("%s: could not load old rx mbuf",
				    sc->sc_dev.dv_xname);
			}
			/* physical address may have changed */
			rxd->sdp0 = htole32(data->map->dm_segs[0].ds_addr);
			ifp->if_ierrors++;
			goto skip;
		}

		/*
		 * New mbuf successfully loaded, update Rx ring and continue
		 * processing.
		 */
		m = data->m;
		data->m = m1;
		rxd->sdp0 = htole32(data->map->dm_segs[0].ds_addr);

		rxwi = mtod(m, struct rt2860_rxwi *);

		/* finalize mbuf */
		m->m_data = (caddr_t)(rxwi + 1);
		m->m_pkthdr.len = m->m_len = letoh16(rxwi->len) & 0xfff;

		wh = mtod(m, struct ieee80211_frame *);
		memset(&rxi, 0, sizeof(rxi));
		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
			/* frame is decrypted by hardware */
			wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
			rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
		}

		/* HW may insert 2 padding bytes after 802.11 header */
		if (rxd->flags & htole32(RT2860_RX_L2PAD)) {
			u_int hdrlen = ieee80211_get_hdrlen(wh);
			memmove((caddr_t)wh + 2, wh, hdrlen);
			m->m_data += 2;
			wh = mtod(m, struct ieee80211_frame *);
		}

		ant = rt2860_maxrssi_chain(sc, rxwi);
		rssi = rxwi->rssi[ant];

#if NBPFILTER > 0
		if (__predict_true(sc->sc_drvbpf == NULL))
			goto skipbpf;

		tap = &sc->sc_rxtap;
		tap->wr_flags = 0;
		tap->wr_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);
		tap->wr_antsignal = rssi;
		tap->wr_antenna = ant;
		tap->wr_dbm_antsignal = rt2860_rssi2dbm(sc, rssi, ant);
		tap->wr_rate = 2;	/* in case it can't be found below */
		phy = letoh16(rxwi->phy);
		switch (phy & RT2860_PHY_MODE) {
		case RT2860_PHY_CCK:
			switch ((phy & RT2860_PHY_MCS) & ~RT2860_PHY_SHPRE) {
			case 0:	tap->wr_rate =   2; break;
			case 1:	tap->wr_rate =   4; break;
			case 2:	tap->wr_rate =  11; break;
			case 3:	tap->wr_rate =  22; break;
			}
			if (phy & RT2860_PHY_SHPRE)
				tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
			break;
		case RT2860_PHY_OFDM:
			switch (phy & RT2860_PHY_MCS) {
			case 0:	tap->wr_rate =  12; break;
			case 1:	tap->wr_rate =  18; break;
			case 2:	tap->wr_rate =  24; break;
			case 3:	tap->wr_rate =  36; break;
			case 4:	tap->wr_rate =  48; break;
			case 5:	tap->wr_rate =  72; break;
			case 6:	tap->wr_rate =  96; break;
			case 7:	tap->wr_rate = 108; break;
			}
			break;
		}
		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_rxtap_len, m,
		    BPF_DIRECTION_IN);
skipbpf:
#endif
		/* grab a reference to the source node */
		ni = ieee80211_find_rxnode(ic, wh);

		/* send the frame to the 802.11 layer */
		rxi.rxi_rssi = rssi;
		ieee80211_inputm(ifp, m, ni, &rxi, &ml);

		/* node is no longer needed */
		ieee80211_release_node(ic, ni);

skip:		rxd->sdl0 &= ~htole16(RT2860_RX_DDONE);

		bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
		    sc->rxq.cur * sizeof (struct rt2860_rxd),
		    sizeof (struct rt2860_rxd), BUS_DMASYNC_PREWRITE);

		sc->rxq.cur = (sc->rxq.cur + 1) % RT2860_RX_RING_COUNT;
	}
	if_input(ifp, &ml);

	/* tell HW what we have processed */
	RAL_WRITE(sc, RT2860_RX_CALC_IDX,
	    (sc->rxq.cur - 1) % RT2860_RX_RING_COUNT);
}

void
rt2860_tbtt_intr(struct rt2860_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
		/* one less beacon until next DTIM */
		if (ic->ic_dtim_count == 0)
			ic->ic_dtim_count = ic->ic_dtim_period - 1;
		else
			ic->ic_dtim_count--;

		/* update dynamic parts of beacon */
		rt2860_setup_beacon(sc);

		/* flush buffered multicast frames */
		if (ic->ic_dtim_count == 0)
			ieee80211_notify_dtim(ic);
	}
#endif
	/* check if protection mode has changed */
	if ((sc->sc_ic_flags ^ ic->ic_flags) & IEEE80211_F_USEPROT) {
		rt2860_updateprot(ic);
		sc->sc_ic_flags = ic->ic_flags;
	}
}

void
rt2860_gp_intr(struct rt2860_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	DPRINTFN(2, ("GP timeout state=%d\n", ic->ic_state));

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(&ic->ic_if);
	else if (ic->ic_state == IEEE80211_S_RUN)
		rt2860_updatestats(sc);
}

int
rt2860_intr(void *arg)
{
	struct rt2860_softc *sc = arg;
	uint32_t r;

	r = RAL_READ(sc, RT2860_INT_STATUS);
	if (__predict_false(r == 0xffffffff))
		return 0;	/* device likely went away */
	if (r == 0)
		return 0;	/* not for us */

	/* acknowledge interrupts */
	RAL_WRITE(sc, RT2860_INT_STATUS, r);

	if (r & RT2860_TX_RX_COHERENT)
		rt2860_intr_coherent(sc);

	if (r & RT2860_MAC_INT_2)	/* TX status */
		rt2860_drain_stats_fifo(sc);

	if (r & RT2860_TX_DONE_INT5)
		rt2860_tx_intr(sc, 5);

	if (r & RT2860_RX_DONE_INT)
		rt2860_rx_intr(sc);

	if (r & RT2860_TX_DONE_INT4)
		rt2860_tx_intr(sc, 4);

	if (r & RT2860_TX_DONE_INT3)
		rt2860_tx_intr(sc, 3);

	if (r & RT2860_TX_DONE_INT2)
		rt2860_tx_intr(sc, 2);

	if (r & RT2860_TX_DONE_INT1)
		rt2860_tx_intr(sc, 1);

	if (r & RT2860_TX_DONE_INT0)
		rt2860_tx_intr(sc, 0);

	if (r & RT2860_MAC_INT_0)	/* TBTT */
		rt2860_tbtt_intr(sc);

	if (r & RT2860_MAC_INT_3)	/* Auto wakeup */
		/* TBD wakeup */;

	if (r & RT2860_MAC_INT_4)	/* GP timer */
		rt2860_gp_intr(sc);

	return 1;
}

int
rt2860_tx(struct rt2860_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2860_node *rn = (void *)ni;
	struct rt2860_tx_ring *ring;
	struct rt2860_tx_data *data;
	struct rt2860_txd *txd;
	struct rt2860_txwi *txwi;
	struct ieee80211_frame *wh;
	bus_dma_segment_t *seg;
	u_int hdrlen;
	uint16_t qos, dur;
	uint8_t type, qsel, mcs, pid, tid, qid;
	int nsegs, hasqos, ridx, ctl_ridx;

	/* the data pool contains at least one element, pick the first */
	data = SLIST_FIRST(&sc->data_pool);

	wh = mtod(m, struct ieee80211_frame *);
	hdrlen = ieee80211_get_hdrlen(wh);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	if ((hasqos = ieee80211_has_qos(wh))) {
		qos = ieee80211_get_qos(wh);
		tid = qos & IEEE80211_QOS_TID;
		qid = ieee80211_up_to_ac(ic, tid);
	} else {
		qos = 0;
		tid = 0;
		qid = (type == IEEE80211_FC0_TYPE_MGT) ?
		    sc->mgtqid : EDCA_AC_BE;
	}
	ring = &sc->txq[qid];

	/* pickup a rate index */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA) {
		ridx = (ic->ic_curmode == IEEE80211_MODE_11A) ?
		    RT2860_RIDX_OFDM6 : RT2860_RIDX_CCK1;
		ctl_ridx = rt2860_rates[ridx].ctl_ridx;
	} else if (ic->ic_fixed_rate != -1) {
		ridx = sc->fixed_ridx;
		ctl_ridx = rt2860_rates[ridx].ctl_ridx;
	} else {
		ridx = rn->ridx[ni->ni_txrate];
		ctl_ridx = rn->ctl_ridx[ni->ni_txrate];
	}

	/* get MCS code from rate index */
	mcs = rt2860_rates[ridx].mcs;

	/* setup TX Wireless Information */
	txwi = data->txwi;
	txwi->flags = 0;
	/* let HW generate seq numbers for non-QoS frames */
	txwi->xflags = hasqos ? 0 : RT2860_TX_NSEQ;
	txwi->wcid = (type == IEEE80211_FC0_TYPE_DATA) ? rn->wcid : 0xff;
	txwi->len = htole16(m->m_pkthdr.len);
	if (rt2860_rates[ridx].phy == IEEE80211_T_DS) {
		txwi->phy = htole16(RT2860_PHY_CCK);
		if (ridx != RT2860_RIDX_CCK1 &&
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			mcs |= RT2860_PHY_SHPRE;
	} else
		txwi->phy = htole16(RT2860_PHY_OFDM);
	txwi->phy |= htole16(mcs);

	/*
	 * We store the MCS code into the driver-private PacketID field.
	 * The PacketID is latched into TX_STAT_FIFO when Tx completes so
	 * that we know at which initial rate the frame was transmitted.
	 * We add 1 to the MCS code because setting the PacketID field to
	 * 0 means that we don't want feedback in TX_STAT_FIFO.
	 */
	pid = (mcs + 1) & 0xf;
	txwi->len |= htole16(pid << RT2860_TX_PID_SHIFT);

	/* check if RTS/CTS or CTS-to-self protection is required */
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (m->m_pkthdr.len + IEEE80211_CRC_LEN > ic->ic_rtsthreshold ||
	     ((ic->ic_flags & IEEE80211_F_USEPROT) &&
	      rt2860_rates[ridx].phy == IEEE80211_T_OFDM)))
		txwi->txop = RT2860_TX_TXOP_HT;
	else
		txwi->txop = RT2860_TX_TXOP_BACKOFF;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (!hasqos || (qos & IEEE80211_QOS_ACK_POLICY_MASK) !=
	     IEEE80211_QOS_ACK_POLICY_NOACK)) {
		txwi->xflags |= RT2860_TX_ACK;

		if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
			dur = rt2860_rates[ctl_ridx].sp_ack_dur;
		else
			dur = rt2860_rates[ctl_ridx].lp_ack_dur;
		*(uint16_t *)wh->i_dur = htole16(dur);
	}
#ifndef IEEE80211_STA_ONLY
	/* ask MAC to insert timestamp into probe responses */
	if ((wh->i_fc[0] &
	     (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
	     (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP))
	    /* NOTE: beacons do not pass through tx_data() */
		txwi->flags |= RT2860_TX_TS;
#endif

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct rt2860_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rt2860_rates[ridx].rate;
		tap->wt_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);
		if (mcs & RT2860_PHY_SHPRE)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len, m,
		    BPF_DIRECTION_OUT);
	}
#endif

	/* copy and trim 802.11 header */
	memcpy(txwi + 1, wh, hdrlen);
	m_adj(m, hdrlen);

	KASSERT (ring->queued <= RT2860_TX_RING_ONEMORE); /* <1> */

	if (bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m, BUS_DMA_NOWAIT)) {
		if (m_defrag(m, M_DONTWAIT))
			return (ENOBUFS);
		if (bus_dmamap_load_mbuf(sc->sc_dmat,
		    data->map, m, BUS_DMA_NOWAIT))
			return (EFBIG);
	}

	/* The map will fit into the tx ring: (a "full" ring may have a few
	 * unused descriptors, at most (txds(MAX_SCATTER) - 1))
	 *
	 *   ring->queued + txds(data->map->nsegs)
	 * <=	{ <0> data->map->nsegs <= MAX_SCATTER }
	 *   ring->queued + txds(MAX_SCATTER)
	 * <=	{ <1> ring->queued <= TX_RING_MAX - txds(MAX_SCATTER) }
	 *   TX_RING_MAX - txds(MAX_SCATTER) + txds(MAX_SCATTER)
	 * <=   { arithmetic }
	 *   TX_RING_MAX
	 */

	qsel = (qid < EDCA_NUM_AC) ? RT2860_TX_QSEL_EDCA : RT2860_TX_QSEL_MGMT;

	/* first segment is TXWI + 802.11 header */
	txd = &ring->txd[ring->cur];
	txd->sdp0 = htole32(data->paddr);
	txd->sdl0 = htole16(sizeof (struct rt2860_txwi) + hdrlen);
	txd->flags = qsel;

	/* setup payload segments */
	seg = data->map->dm_segs;
	for (nsegs = data->map->dm_nsegs; nsegs >= 2; nsegs -= 2) {
		txd->sdp1 = htole32(seg->ds_addr);
		txd->sdl1 = htole16(seg->ds_len);
		seg++;
		ring->cur = (ring->cur + 1) % RT2860_TX_RING_COUNT;
		/* grab a new Tx descriptor */
		txd = &ring->txd[ring->cur];
		txd->sdp0 = htole32(seg->ds_addr);
		txd->sdl0 = htole16(seg->ds_len);
		txd->flags = qsel;
		seg++;
	}
	/* finalize last segment */
	if (nsegs > 0) {
		txd->sdp1 = htole32(seg->ds_addr);
		txd->sdl1 = htole16(seg->ds_len | RT2860_TX_LS1);
	} else {
		txd->sdl0 |= htole16(RT2860_TX_LS0);
		txd->sdl1 = 0;
	}

	/* remove from the free pool and link it into the SW Tx slot */
	SLIST_REMOVE_HEAD(&sc->data_pool, next);
	data->m = m;
	data->ni = ni;
	ring->data[ring->cur] = data;

	bus_dmamap_sync(sc->sc_dmat, sc->txwi_map,
	    (caddr_t)txwi - sc->txwi_vaddr, RT2860_TXWI_DMASZ,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(4, ("sending frame qid=%d wcid=%d nsegs=%d ridx=%d\n",
	    qid, txwi->wcid, data->map->dm_nsegs, ridx));

	ring->cur = (ring->cur + 1) % RT2860_TX_RING_COUNT;
	ring->queued += 1 + (data->map->dm_nsegs / 2);
	if (ring->queued > RT2860_TX_RING_ONEMORE)
		sc->qfullmsk |= 1 << qid;

	/* kick Tx */
	RAL_WRITE(sc, RT2860_TX_CTX_IDX(qid), ring->cur);

	return 0;
}

void
rt2860_start(struct ifnet *ifp)
{
	struct rt2860_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (SLIST_EMPTY(&sc->data_pool) || sc->qfullmsk != 0) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
		/* send pending management frames first */
		m = mq_dequeue(&ic->ic_mgtq);
		if (m != NULL) {
			ni = m->m_pkthdr.ph_cookie;
			goto sendit;
		}
		if (ic->ic_state != IEEE80211_S_RUN)
			break;

		/* send buffered frames for power-save mode */
		m = mq_dequeue(&ic->ic_pwrsaveq);
		if (m != NULL) {
			ni = m->m_pkthdr.ph_cookie;
			goto sendit;
		}

		/* encapsulate and send data frames */
		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;
#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
		if ((m = ieee80211_encap(ifp, m, &ni)) == NULL)
			continue;
sendit:
#if NBPFILTER > 0
		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_OUT);
#endif
		if (rt2860_tx(sc, m, ni) != 0) {
			m_freem(m);
			ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			continue;
		}

		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

void
rt2860_watchdog(struct ifnet *ifp)
{
	struct rt2860_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			rt2860_stop(ifp, 0);
			rt2860_init(ifp);
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
rt2860_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct rt2860_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING))
				rt2860_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				rt2860_stop(ifp, 1);
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
				rt2860_switch_chan(sc, ic->ic_ibss_chan);
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			rt2860_stop(ifp, 0);
			rt2860_init(ifp);
		}
		error = 0;
	}

	splx(s);

	return error;
}

/*
 * Reading and writing from/to the BBP is different from RT2560 and RT2661.
 * We access the BBP through the 8051 microcontroller unit which means that
 * the microcode must be loaded first.
 */
void
rt2860_mcu_bbp_write(struct rt2860_softc *sc, uint8_t reg, uint8_t val)
{
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2860_H2M_BBPAGENT) & RT2860_BBP_CSR_KICK))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not write to BBP through MCU\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	RAL_WRITE(sc, RT2860_H2M_BBPAGENT, RT2860_BBP_RW_PARALLEL |
	    RT2860_BBP_CSR_KICK | reg << 8 | val);
	RAL_BARRIER_WRITE(sc);

	rt2860_mcu_cmd(sc, RT2860_MCU_CMD_BBP, 0, 0);
	DELAY(1000);
}

uint8_t
rt2860_mcu_bbp_read(struct rt2860_softc *sc, uint8_t reg)
{
	uint32_t val;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2860_H2M_BBPAGENT) & RT2860_BBP_CSR_KICK))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not read from BBP through MCU\n",
		    sc->sc_dev.dv_xname);
		return 0;
	}

	RAL_WRITE(sc, RT2860_H2M_BBPAGENT, RT2860_BBP_RW_PARALLEL |
	    RT2860_BBP_CSR_KICK | RT2860_BBP_CSR_READ | reg << 8);
	RAL_BARRIER_WRITE(sc);

	rt2860_mcu_cmd(sc, RT2860_MCU_CMD_BBP, 0, 0);
	DELAY(1000);

	for (ntries = 0; ntries < 100; ntries++) {
		val = RAL_READ(sc, RT2860_H2M_BBPAGENT);
		if (!(val & RT2860_BBP_CSR_KICK))
			return val & 0xff;
		DELAY(1);
	}
	printf("%s: could not read from BBP through MCU\n",
	    sc->sc_dev.dv_xname);

	return 0;
}

/*
 * Write to one of the 4 programmable 24-bit RF registers.
 */
void
rt2860_rf_write(struct rt2860_softc *sc, uint8_t reg, uint32_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2860_RF_CSR_CFG0) & RT2860_RF_REG_CTRL))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not write to RF\n", sc->sc_dev.dv_xname);
		return;
	}

	/* RF registers are 24-bit on the RT2860 */
	tmp = RT2860_RF_REG_CTRL | 24 << RT2860_RF_REG_WIDTH_SHIFT |
	    (val & 0x3fffff) << 2 | (reg & 3);
	RAL_WRITE(sc, RT2860_RF_CSR_CFG0, tmp);
}

uint8_t
rt3090_rf_read(struct rt2860_softc *sc, uint8_t reg)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT3070_RF_CSR_CFG) & RT3070_RF_KICK))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not read RF register\n",
		    sc->sc_dev.dv_xname);
		return 0xff;
	}
	tmp = RT3070_RF_KICK | reg << 8;
	RAL_WRITE(sc, RT3070_RF_CSR_CFG, tmp);

	for (ntries = 0; ntries < 100; ntries++) {
		tmp = RAL_READ(sc, RT3070_RF_CSR_CFG);
		if (!(tmp & RT3070_RF_KICK))
			break;
		DELAY(1);
	}
	if (ntries == 100) {
		printf("%s: could not read RF register\n",
		    sc->sc_dev.dv_xname);
		return 0xff;
	}
	return tmp & 0xff;
}

void
rt3090_rf_write(struct rt2860_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 10; ntries++) {
		if (!(RAL_READ(sc, RT3070_RF_CSR_CFG) & RT3070_RF_KICK))
			break;
		DELAY(10);
	}
	if (ntries == 10) {
		printf("%s: could not write to RF\n", sc->sc_dev.dv_xname);
		return;
	}

	tmp = RT3070_RF_WRITE | RT3070_RF_KICK | reg << 8 | val;
	RAL_WRITE(sc, RT3070_RF_CSR_CFG, tmp);
}

/*
 * Send a command to the 8051 microcontroller unit.
 */
int
rt2860_mcu_cmd(struct rt2860_softc *sc, uint8_t cmd, uint16_t arg, int wait)
{
	int slot, ntries;
	uint32_t tmp;
	uint8_t cid;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2860_H2M_MAILBOX) & RT2860_H2M_BUSY))
			break;
		DELAY(2);
	}
	if (ntries == 100)
		return EIO;

	cid = wait ? cmd : RT2860_TOKEN_NO_INTR;
	RAL_WRITE(sc, RT2860_H2M_MAILBOX, RT2860_H2M_BUSY | cid << 16 | arg);
	RAL_BARRIER_WRITE(sc);
	RAL_WRITE(sc, RT2860_HOST_CMD, cmd);

	if (!wait)
		return 0;
	/* wait for the command to complete */
	for (ntries = 0; ntries < 200; ntries++) {
		tmp = RAL_READ(sc, RT2860_H2M_MAILBOX_CID);
		/* find the command slot */
		for (slot = 0; slot < 4; slot++, tmp >>= 8)
			if ((tmp & 0xff) == cid)
				break;
		if (slot < 4)
			break;
		DELAY(100);
	}
	if (ntries == 200) {
		/* clear command and status */
		RAL_WRITE(sc, RT2860_H2M_MAILBOX_STATUS, 0xffffffff);
		RAL_WRITE(sc, RT2860_H2M_MAILBOX_CID, 0xffffffff);
		return ETIMEDOUT;
	}
	/* get command status (1 means success) */
	tmp = RAL_READ(sc, RT2860_H2M_MAILBOX_STATUS);
	tmp = (tmp >> (slot * 8)) & 0xff;
	DPRINTF(("MCU command=0x%02x slot=%d status=0x%02x\n",
	    cmd, slot, tmp));
	/* clear command and status */
	RAL_WRITE(sc, RT2860_H2M_MAILBOX_STATUS, 0xffffffff);
	RAL_WRITE(sc, RT2860_H2M_MAILBOX_CID, 0xffffffff);
	return (tmp == 1) ? 0 : EIO;
}

void
rt2860_enable_mrr(struct rt2860_softc *sc)
{
#define CCK(mcs)	(mcs)
#define OFDM(mcs)	(1 << 3 | (mcs))
	RAL_WRITE(sc, RT2860_LG_FBK_CFG0,
	    OFDM(6) << 28 |	/* 54->48 */
	    OFDM(5) << 24 |	/* 48->36 */
	    OFDM(4) << 20 |	/* 36->24 */
	    OFDM(3) << 16 |	/* 24->18 */
	    OFDM(2) << 12 |	/* 18->12 */
	    OFDM(1) <<  8 |	/* 12-> 9 */
	    OFDM(0) <<  4 |	/*  9-> 6 */
	    OFDM(0));		/*  6-> 6 */

	RAL_WRITE(sc, RT2860_LG_FBK_CFG1,
	    CCK(2) << 12 |	/* 11->5.5 */
	    CCK(1) <<  8 |	/* 5.5-> 2 */
	    CCK(0) <<  4 |	/*   2-> 1 */
	    CCK(0));		/*   1-> 1 */
#undef OFDM
#undef CCK
}

void
rt2860_set_txpreamble(struct rt2860_softc *sc)
{
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2860_AUTO_RSP_CFG);
	tmp &= ~RT2860_CCK_SHORT_EN;
	if (sc->sc_ic.ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= RT2860_CCK_SHORT_EN;
	RAL_WRITE(sc, RT2860_AUTO_RSP_CFG, tmp);
}

void
rt2860_set_basicrates(struct rt2860_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* set basic rates mask */
	if (ic->ic_curmode == IEEE80211_MODE_11B)
		RAL_WRITE(sc, RT2860_LEGACY_BASIC_RATE, 0x003);
	else if (ic->ic_curmode == IEEE80211_MODE_11A)
		RAL_WRITE(sc, RT2860_LEGACY_BASIC_RATE, 0x150);
	else	/* 11g */
		RAL_WRITE(sc, RT2860_LEGACY_BASIC_RATE, 0x15f);
}

void
rt2860_select_chan_group(struct rt2860_softc *sc, int group)
{
	uint32_t tmp;
	uint8_t agc;

	/* Wait for BBP to settle */
	DELAY(1000);

	rt2860_mcu_bbp_write(sc, 62, 0x37 - sc->lna[group]);
	rt2860_mcu_bbp_write(sc, 63, 0x37 - sc->lna[group]);
	rt2860_mcu_bbp_write(sc, 64, 0x37 - sc->lna[group]);
	rt2860_mcu_bbp_write(sc, 86, 0x00);

	if (group == 0) {
		if (sc->ext_2ghz_lna) {
			rt2860_mcu_bbp_write(sc, 82, 0x62);
			rt2860_mcu_bbp_write(sc, 75, 0x46);
		} else {
			rt2860_mcu_bbp_write(sc, 82, 0x84);
			rt2860_mcu_bbp_write(sc, 75, 0x50);
		}
	} else {
		if (sc->mac_ver == 0x3572)
			rt2860_mcu_bbp_write(sc, 82, 0x94);
		else
			rt2860_mcu_bbp_write(sc, 82, 0xf2);

		if (sc->ext_5ghz_lna)
			rt2860_mcu_bbp_write(sc, 75, 0x46);
		else
			rt2860_mcu_bbp_write(sc, 75, 0x50);
	}

	tmp = RAL_READ(sc, RT2860_TX_BAND_CFG);
	tmp &= ~(RT2860_5G_BAND_SEL_N | RT2860_5G_BAND_SEL_P);
	tmp |= (group == 0) ? RT2860_5G_BAND_SEL_N : RT2860_5G_BAND_SEL_P;
	RAL_WRITE(sc, RT2860_TX_BAND_CFG, tmp);

	/* enable appropriate Power Amplifiers and Low Noise Amplifiers */
	tmp = RT2860_RFTR_EN | RT2860_TRSW_EN | RT2860_LNA_PE0_EN;
	if (sc->nrxchains > 1)
		tmp |= RT2860_LNA_PE1_EN;
	if (sc->mac_ver == 0x3593 && sc->nrxchains > 2)
		tmp |= RT3593_LNA_PE2_EN;
	if (group == 0) {	/* 2GHz */
		tmp |= RT2860_PA_PE_G0_EN;
		if (sc->ntxchains > 1)
			tmp |= RT2860_PA_PE_G1_EN;
		if (sc->mac_ver == 0x3593 && sc->ntxchains > 2)
			tmp |= RT3593_PA_PE_G2_EN;
	} else {		/* 5GHz */
		tmp |= RT2860_PA_PE_A0_EN;
		if (sc->ntxchains > 1)
			tmp |= RT2860_PA_PE_A1_EN;
		if (sc->mac_ver == 0x3593 && sc->ntxchains > 2)
			tmp |= RT3593_PA_PE_A2_EN;
	}
	if (sc->mac_ver == 0x3572) {
		rt3090_rf_write(sc, 8, 0x00);
		RAL_WRITE(sc, RT2860_TX_PIN_CFG, tmp);
		rt3090_rf_write(sc, 8, 0x80);
	} else
		RAL_WRITE(sc, RT2860_TX_PIN_CFG, tmp);

	if (sc->mac_ver == 0x3593) {
		tmp = RAL_READ(sc, RT2860_GPIO_CTRL);
		if (sc->sc_flags & RT2860_PCIE) {
			tmp &= ~0x01010000;
			if (group == 0)
				tmp |= 0x00010000;
		} else {
			tmp &= ~0x00008080;
			if (group == 0)
				tmp |= 0x00000080;
		}
		tmp = (tmp & ~0x00001000) | 0x00000010;
		RAL_WRITE(sc, RT2860_GPIO_CTRL, tmp);
	}

	/* set initial AGC value */
	if (group == 0) {	/* 2GHz band */
		if (sc->mac_ver >= 0x3071)
			agc = 0x1c + sc->lna[0] * 2;
		else
			agc = 0x2e + sc->lna[0];
	} else {		/* 5GHz band */
		if (sc->mac_ver == 0x3572)
			agc = 0x22 + (sc->lna[group] * 5) / 3;
		else
			agc = 0x32 + (sc->lna[group] * 5) / 3;
	}
	rt2860_mcu_bbp_write(sc, 66, agc);

	DELAY(1000);
}

void
rt2860_set_chan(struct rt2860_softc *sc, u_int chan)
{
	const struct rfprog *rfprog = rt2860_rf2850;
	uint32_t r2, r3, r4;
	int8_t txpow1, txpow2;
	u_int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rfprog[i].chan != chan; i++)
		;

	r2 = rfprog[i].r2;
	if (sc->ntxchains == 1)
		r2 |= 1 << 12;		/* 1T: disable Tx chain 2 */
	if (sc->nrxchains == 1)
		r2 |= 1 << 15 | 1 << 4;	/* 1R: disable Rx chains 2 & 3 */
	else if (sc->nrxchains == 2)
		r2 |= 1 << 4;		/* 2R: disable Rx chain 3 */

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];
	txpow2 = sc->txpow2[i];
	if (chan > 14) {
		if (txpow1 >= 0)
			txpow1 = txpow1 << 1 | 1;
		else
			txpow1 = (7 + txpow1) << 1;
		if (txpow2 >= 0)
			txpow2 = txpow2 << 1 | 1;
		else
			txpow2 = (7 + txpow2) << 1;
	}
	r3 = rfprog[i].r3 | txpow1 << 7;
	r4 = rfprog[i].r4 | sc->freq << 13 | txpow2 << 4;

	rt2860_rf_write(sc, RT2860_RF1, rfprog[i].r1);
	rt2860_rf_write(sc, RT2860_RF2, r2);
	rt2860_rf_write(sc, RT2860_RF3, r3);
	rt2860_rf_write(sc, RT2860_RF4, r4);

	DELAY(200);

	rt2860_rf_write(sc, RT2860_RF1, rfprog[i].r1);
	rt2860_rf_write(sc, RT2860_RF2, r2);
	rt2860_rf_write(sc, RT2860_RF3, r3 | 1);
	rt2860_rf_write(sc, RT2860_RF4, r4);

	DELAY(200);

	rt2860_rf_write(sc, RT2860_RF1, rfprog[i].r1);
	rt2860_rf_write(sc, RT2860_RF2, r2);
	rt2860_rf_write(sc, RT2860_RF3, r3);
	rt2860_rf_write(sc, RT2860_RF4, r4);
}

void
rt3090_set_chan(struct rt2860_softc *sc, u_int chan)
{
	int8_t txpow1, txpow2;
	uint8_t rf;
	int i;

	KASSERT(chan >= 1 && chan <= 14);	/* RT3090 is 2GHz only */

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++)
		;

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];
	txpow2 = sc->txpow2[i];

	rt3090_rf_write(sc, 2, rt3090_freqs[i].n);
	rf = rt3090_rf_read(sc, 3);
	rf = (rf & ~0x0f) | rt3090_freqs[i].k;
	rt3090_rf_write(sc, 3, rf);
	rf = rt3090_rf_read(sc, 6);
	rf = (rf & ~0x03) | rt3090_freqs[i].r;
	rt3090_rf_write(sc, 6, rf);

	/* set Tx0 power */
	rf = rt3090_rf_read(sc, 12);
	rf = (rf & ~0x1f) | txpow1;
	rt3090_rf_write(sc, 12, rf);

	/* set Tx1 power */
	rf = rt3090_rf_read(sc, 13);
	rf = (rf & ~0x1f) | txpow2;
	rt3090_rf_write(sc, 13, rf);

	rf = rt3090_rf_read(sc, 1);
	rf &= ~0xfc;
	if (sc->ntxchains == 1)
		rf |= RT3070_TX1_PD | RT3070_TX2_PD;
	else if (sc->ntxchains == 2)
		rf |= RT3070_TX2_PD;
	if (sc->nrxchains == 1)
		rf |= RT3070_RX1_PD | RT3070_RX2_PD;
	else if (sc->nrxchains == 2)
		rf |= RT3070_RX2_PD;
	rt3090_rf_write(sc, 1, rf);

	/* set RF offset */
	rf = rt3090_rf_read(sc, 23);
	rf = (rf & ~0x7f) | sc->freq;
	rt3090_rf_write(sc, 23, rf);

	/* program RF filter */
	rf = rt3090_rf_read(sc, 24);	/* Tx */
	rf = (rf & ~0x3f) | sc->rf24_20mhz;
	rt3090_rf_write(sc, 24, rf);
	rf = rt3090_rf_read(sc, 31);	/* Rx */
	rf = (rf & ~0x3f) | sc->rf24_20mhz;
	rt3090_rf_write(sc, 31, rf);

	/* enable RF tuning */
	rf = rt3090_rf_read(sc, 7);
	rt3090_rf_write(sc, 7, rf | RT3070_TUNE);
}

void
rt5390_set_chan(struct rt2860_softc *sc, u_int chan)
{
	uint8_t h20mhz, rf, tmp;
	int8_t txpow1, txpow2;
	int i;

	/* RT5390 is 2GHz only */
	KASSERT(chan >= 1 && chan <= 14);

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++)
		;

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];
	txpow2 = sc->txpow2[i];

	rt3090_rf_write(sc, 8, rt3090_freqs[i].n);
	rt3090_rf_write(sc, 9, rt3090_freqs[i].k & 0x0f);
	rf = rt3090_rf_read(sc, 11);
	rf = (rf & ~0x03) | (rt3090_freqs[i].r & 0x03);
	rt3090_rf_write(sc, 11, rf);

	rf = rt3090_rf_read(sc, 49);
	rf = (rf & ~0x3f) | (txpow1 & 0x3f);
	/* the valid range of the RF R49 is 0x00~0x27 */
	if ((rf & 0x3f) > 0x27)
		rf = (rf & ~0x3f) | 0x27;
	rt3090_rf_write(sc, 49, rf);
	if (sc->mac_ver == 0x5392) {
		rf = rt3090_rf_read(sc, 50);
		rf = (rf & ~0x3f) | (txpow2 & 0x3f);
		/* the valid range of the RF R50 is 0x00~0x27 */
		if ((rf & 0x3f) > 0x27)
			rf = (rf & ~0x3f) | 0x27;
		rt3090_rf_write(sc, 50, rf);
	}

	rf = rt3090_rf_read(sc, 1);
	rf |= RT3070_RF_BLOCK | RT3070_PLL_PD | RT3070_RX0_PD | RT3070_TX0_PD;
	if (sc->mac_ver == 0x5392)
		rf |= RT3070_RX1_PD | RT3070_TX1_PD;
	rt3090_rf_write(sc, 1, rf);

	rf = rt3090_rf_read(sc, 2);
	rt3090_rf_write(sc, 2, rf | RT3593_RESCAL);
	DELAY(1000);
	rt3090_rf_write(sc, 2, rf & ~RT3593_RESCAL);

	rf = rt3090_rf_read(sc, 17);
	tmp = rf;
	rf = (rf & ~0x7f) | (sc->freq & 0x7f);
	rf = MIN(rf, 0x5f);
	if (tmp != rf)
		rt2860_mcu_cmd(sc, 0x74, (tmp << 8 ) | rf, 0);

	if (sc->mac_ver == 0x5390) {
		if (chan <= 4)
			rf = 0x73;
		else if (chan >= 5 && chan <= 6)
			rf = 0x63;
		else if (chan >= 7 && chan <= 10)
			rf = 0x53;
		else
			rf = 43;
		rt3090_rf_write(sc, 55, rf);

		if (chan == 1)
			rf = 0x0c;
		else if (chan == 2)
			rf = 0x0b;
		else if (chan == 3)
			rf = 0x0a;
		else if (chan >= 4 && chan <= 6)
			rf = 0x09;
		else if (chan >= 7 && chan <= 12)
			rf = 0x08;
		else if (chan == 13)
			rf = 0x07;
		else
			rf = 0x06;
		rt3090_rf_write(sc, 59, rf);
	} else if (sc->mac_ver == 0x3290) {
		if (chan == 6)
			rt2860_mcu_bbp_write(sc, 68, 0x0c);
		else
			rt2860_mcu_bbp_write(sc, 68, 0x0b);

		if (chan >= 1 && chan < 6)
			rf = 0x0f;
		else if (chan >= 7 && chan <= 11)
			rf = 0x0e;
		else if (chan >= 12 && chan <= 14)
			rf = 0x0d;
		rt3090_rf_write(sc, 59, rf);
	}

	/* Tx/Rx h20M */
	h20mhz = (sc->rf24_20mhz & 0x20) >> 5;
	rf = rt3090_rf_read(sc, 30);
	rf = (rf & ~0x06) | (h20mhz << 1) | (h20mhz << 2);
	rt3090_rf_write(sc, 30, rf);

	/* Rx BB filter VCM */
	rf = rt3090_rf_read(sc, 30);
	rf = (rf & ~0x18) | 0x10;
	rt3090_rf_write(sc, 30, rf);

	/* Initiate VCO calibration. */
	rf = rt3090_rf_read(sc, 3);
	rf |= RT3593_VCOCAL;
	rt3090_rf_write(sc, 3, rf);
}

int
rt3090_rf_init(struct rt2860_softc *sc)
{
	uint32_t tmp;
	uint8_t rf, bbp;
	int i;

	rf = rt3090_rf_read(sc, 30);
	/* toggle RF R30 bit 7 */
	rt3090_rf_write(sc, 30, rf | 0x80);
	DELAY(1000);
	rt3090_rf_write(sc, 30, rf & ~0x80);

	tmp = RAL_READ(sc, RT3070_LDO_CFG0);
	tmp &= ~0x1f000000;
	if (sc->patch_dac && sc->mac_rev < 0x0211)
		tmp |= 0x0d000000;	/* 1.35V */
	else
		tmp |= 0x01000000;	/* 1.2V */
	RAL_WRITE(sc, RT3070_LDO_CFG0, tmp);

	/* patch LNA_PE_G1 */
	tmp = RAL_READ(sc, RT3070_GPIO_SWITCH);
	RAL_WRITE(sc, RT3070_GPIO_SWITCH, tmp & ~0x20);

	/* initialize RF registers to default value */
	if (sc->mac_ver == 0x3572) {
		for (i = 0; i < nitems(rt3572_def_rf); i++) {
			rt3090_rf_write(sc, rt3572_def_rf[i].reg,
			    rt3572_def_rf[i].val);
		}
	} else {
		for (i = 0; i < nitems(rt3090_def_rf); i++) {
			rt3090_rf_write(sc, rt3090_def_rf[i].reg,
			    rt3090_def_rf[i].val);
		}
	}

	/* select 20MHz bandwidth */
	rt3090_rf_write(sc, 31, 0x14);

	rf = rt3090_rf_read(sc, 6);
	rt3090_rf_write(sc, 6, rf | 0x40);

	if (sc->mac_ver != 0x3593) {
		/* calibrate filter for 20MHz bandwidth */
		sc->rf24_20mhz = 0x1f;	/* default value */
		rt3090_filter_calib(sc, 0x07, 0x16, &sc->rf24_20mhz);

		/* select 40MHz bandwidth */
		bbp = rt2860_mcu_bbp_read(sc, 4);
		rt2860_mcu_bbp_write(sc, 4, (bbp & ~0x08) | 0x10);
		rf = rt3090_rf_read(sc, 31);
		rt3090_rf_write(sc, 31, rf | 0x20);

		/* calibrate filter for 40MHz bandwidth */
		sc->rf24_40mhz = 0x2f;	/* default value */
		rt3090_filter_calib(sc, 0x27, 0x19, &sc->rf24_40mhz);

		/* go back to 20MHz bandwidth */
		bbp = rt2860_mcu_bbp_read(sc, 4);
		rt2860_mcu_bbp_write(sc, 4, bbp & ~0x18);
	}
	if (sc->mac_rev < 0x0211)
		rt3090_rf_write(sc, 27, 0x03);

	tmp = RAL_READ(sc, RT3070_OPT_14);
	RAL_WRITE(sc, RT3070_OPT_14, tmp | 1);

	if (sc->rf_rev == RT3070_RF_3020)
		rt3090_set_rx_antenna(sc, 0);

	bbp = rt2860_mcu_bbp_read(sc, 138);
	if (sc->mac_ver == 0x3593) {
		if (sc->ntxchains == 1)
			bbp |= 0x60;	/* turn off DAC1 and DAC2 */
		else if (sc->ntxchains == 2)
			bbp |= 0x40;	/* turn off DAC2 */
		if (sc->nrxchains == 1)
			bbp &= ~0x06;	/* turn off ADC1 and ADC2 */
		else if (sc->nrxchains == 2)
			bbp &= ~0x04;	/* turn off ADC2 */
	} else {
		if (sc->ntxchains == 1)
			bbp |= 0x20;	/* turn off DAC1 */
		if (sc->nrxchains == 1)
			bbp &= ~0x02;	/* turn off ADC1 */
	}
	rt2860_mcu_bbp_write(sc, 138, bbp);

	rf = rt3090_rf_read(sc, 1);
	rf &= ~(RT3070_RX0_PD | RT3070_TX0_PD);
	rf |= RT3070_RF_BLOCK | RT3070_RX1_PD | RT3070_TX1_PD;
	rt3090_rf_write(sc, 1, rf);

	rf = rt3090_rf_read(sc, 15);
	rt3090_rf_write(sc, 15, rf & ~RT3070_TX_LO2);

	rf = rt3090_rf_read(sc, 17);
	rf &= ~RT3070_TX_LO1;
	if (sc->mac_rev >= 0x0211 && !sc->ext_2ghz_lna)
		rf |= 0x20;	/* fix for long range Rx issue */
	if (sc->txmixgain_2ghz >= 2)
		rf = (rf & ~0x7) | sc->txmixgain_2ghz;
	rt3090_rf_write(sc, 17, rf);

	rf = rt3090_rf_read(sc, 20);
	rt3090_rf_write(sc, 20, rf & ~RT3070_RX_LO1);

	rf = rt3090_rf_read(sc, 21);
	rt3090_rf_write(sc, 21, rf & ~RT3070_RX_LO2);

	return 0;
}

void
rt5390_rf_init(struct rt2860_softc *sc)
{
	uint8_t rf, bbp;
	int i;

	rf = rt3090_rf_read(sc, 2);
	/* Toggle RF R2 bit 7. */
	rt3090_rf_write(sc, 2, rf | RT3593_RESCAL);
	DELAY(1000);
	rt3090_rf_write(sc, 2, rf & ~RT3593_RESCAL);

	/* Initialize RF registers to default value. */
	if (sc->mac_ver == 0x5392) {
		for (i = 0; i < nitems(rt5392_def_rf); i++) {
			rt3090_rf_write(sc, rt5392_def_rf[i].reg,
			    rt5392_def_rf[i].val);
		}
	} else if (sc->mac_ver == 0x3290) {
		for (i = 0; i < nitems(rt3290_def_rf); i++) {
			rt3090_rf_write(sc, rt3290_def_rf[i].reg,
			    rt3290_def_rf[i].val);
		}
	} else {
		for (i = 0; i < nitems(rt5390_def_rf); i++) {
			rt3090_rf_write(sc, rt5390_def_rf[i].reg,
			    rt5390_def_rf[i].val);
		}
	}

	sc->rf24_20mhz = 0x1f;
	sc->rf24_40mhz = 0x2f;

	if (sc->mac_rev < 0x0211)
		rt3090_rf_write(sc, 27, 0x03);

	/* Set led open drain enable. */
	RAL_WRITE(sc, RT3070_OPT_14, RAL_READ(sc, RT3070_OPT_14) | 1);

	RAL_WRITE(sc, RT2860_TX_SW_CFG1, 0);
	RAL_WRITE(sc, RT2860_TX_SW_CFG2, 0);

	if (sc->mac_ver == 0x3290 ||
	    sc->mac_ver == 0x5390)
		rt3090_set_rx_antenna(sc, 0);

	/* Patch RSSI inaccurate issue. */
	rt2860_mcu_bbp_write(sc, 79, 0x13);
	rt2860_mcu_bbp_write(sc, 80, 0x05);
	rt2860_mcu_bbp_write(sc, 81, 0x33);

	/* Enable DC filter. */
	if (sc->mac_rev >= 0x0211 ||
	    sc->mac_ver == 0x3290)
		rt2860_mcu_bbp_write(sc, 103, 0xc0);

	bbp = rt2860_mcu_bbp_read(sc, 138);
	if (sc->ntxchains == 1)
		bbp |= 0x20;    /* Turn off DAC1. */
	if (sc->nrxchains == 1)
		bbp &= ~0x02;   /* Turn off ADC1. */
	rt2860_mcu_bbp_write(sc, 138, bbp);

	/* Enable RX LO1 and LO2. */
	rt3090_rf_write(sc, 38, rt3090_rf_read(sc, 38) & ~RT5390_RX_LO1);
	rt3090_rf_write(sc, 39, rt3090_rf_read(sc, 39) & ~RT5390_RX_LO2);

	/* Avoid data lost and CRC error. */
	rt2860_mcu_bbp_write(sc, 4,
	    rt2860_mcu_bbp_read(sc, 4) | RT5390_MAC_IF_CTRL);

	rf = rt3090_rf_read(sc, 30);
	rf = (rf & ~0x18) | 0x10;
	rt3090_rf_write(sc, 30, rf);

	/* Disable hardware antenna diversity. */
	if (sc->mac_ver == 0x5390)
		rt2860_mcu_bbp_write(sc, 154, 0);
}

void
rt3090_rf_wakeup(struct rt2860_softc *sc)
{
	uint32_t tmp;
	uint8_t rf;

	if (sc->mac_ver == 0x3593) {
		/* enable VCO */
		rf = rt3090_rf_read(sc, 1);
		rt3090_rf_write(sc, 1, rf | RT3593_VCO);

		/* initiate VCO calibration */
		rf = rt3090_rf_read(sc, 3);
		rt3090_rf_write(sc, 3, rf | RT3593_VCOCAL);

		/* enable VCO bias current control */
		rf = rt3090_rf_read(sc, 6);
		rt3090_rf_write(sc, 6, rf | RT3593_VCO_IC);

		/* initiate res calibration */
		rf = rt3090_rf_read(sc, 2);
		rt3090_rf_write(sc, 2, rf | RT3593_RESCAL);

		/* set reference current control to 0.33 mA */
		rf = rt3090_rf_read(sc, 22);
		rf &= ~RT3593_CP_IC_MASK;
		rf |= 1 << RT3593_CP_IC_SHIFT;
		rt3090_rf_write(sc, 22, rf);

		/* enable RX CTB */
		rf = rt3090_rf_read(sc, 46);
		rt3090_rf_write(sc, 46, rf | RT3593_RX_CTB);

		rf = rt3090_rf_read(sc, 20);
		rf &= ~(RT3593_LDO_RF_VC_MASK | RT3593_LDO_PLL_VC_MASK);
		rt3090_rf_write(sc, 20, rf);
	} else {
		/* enable RF block */
		rf = rt3090_rf_read(sc, 1);
		rt3090_rf_write(sc, 1, rf | RT3070_RF_BLOCK);

		/* enable VCO bias current control */
		rf = rt3090_rf_read(sc, 7);
		rt3090_rf_write(sc, 7, rf | 0x30);

		rf = rt3090_rf_read(sc, 9);
		rt3090_rf_write(sc, 9, rf | 0x0e);

		/* enable RX CTB */
		rf = rt3090_rf_read(sc, 21);
		rt3090_rf_write(sc, 21, rf | RT3070_RX_CTB);

		/* fix Tx to Rx IQ glitch by raising RF voltage */
		rf = rt3090_rf_read(sc, 27);
		rf &= ~0x77;
		if (sc->mac_rev < 0x0211)
			rf |= 0x03;
		rt3090_rf_write(sc, 27, rf);
	}
	if (sc->patch_dac && sc->mac_rev < 0x0211) {
		tmp = RAL_READ(sc, RT3070_LDO_CFG0);
		tmp = (tmp & ~0x1f000000) | 0x0d000000;
		RAL_WRITE(sc, RT3070_LDO_CFG0, tmp);
	}
}

void
rt5390_rf_wakeup(struct rt2860_softc *sc)
{
	uint32_t tmp;
	uint8_t rf;
	
	rf = rt3090_rf_read(sc, 1);
	rf |= RT3070_RF_BLOCK | RT3070_PLL_PD | RT3070_RX0_PD |
	    RT3070_TX0_PD;
	if (sc->mac_ver == 0x5392)
		rf |= RT3070_RX1_PD | RT3070_TX1_PD;
	rt3090_rf_write(sc, 1, rf);

	rf = rt3090_rf_read(sc, 6);
	rf |= RT3593_VCO_IC | RT3593_VCOCAL;
	if (sc->mac_ver == 0x5390)
		rf &= ~RT3593_VCO_IC;
	rt3090_rf_write(sc, 6, rf);

	rt3090_rf_write(sc, 2, rt3090_rf_read(sc, 2) | RT3593_RESCAL);

	rf = rt3090_rf_read(sc, 22);
	rf = (rf & ~0xe0) | 0x20;
	rt3090_rf_write(sc, 22, rf);

	rt3090_rf_write(sc, 42, rt3090_rf_read(sc, 42) | RT5390_RX_CTB);
	rt3090_rf_write(sc, 20, rt3090_rf_read(sc, 20) & ~0x77);
	rt3090_rf_write(sc, 3, rt3090_rf_read(sc, 3) | RT3593_VCOCAL);

	if (sc->patch_dac && sc->mac_rev < 0x0211) {
		tmp = RAL_READ(sc, RT3070_LDO_CFG0);
		tmp = (tmp & ~0x1f000000) | 0x0d000000;
		RAL_WRITE(sc, RT3070_LDO_CFG0, tmp);
	}
}

int
rt3090_filter_calib(struct rt2860_softc *sc, uint8_t init, uint8_t target,
    uint8_t *val)
{
	uint8_t rf22, rf24;
	uint8_t bbp55_pb, bbp55_sb, delta;
	int ntries;

	/* program filter */
	rf24 = rt3090_rf_read(sc, 24);
	rf24 = (rf24 & 0xc0) | init;	/* initial filter value */
	rt3090_rf_write(sc, 24, rf24);

	/* enable baseband loopback mode */
	rf22 = rt3090_rf_read(sc, 22);
	rt3090_rf_write(sc, 22, rf22 | RT3070_BB_LOOPBACK);

	/* set power and frequency of passband test tone */
	rt2860_mcu_bbp_write(sc, 24, 0x00);
	for (ntries = 0; ntries < 100; ntries++) {
		/* transmit test tone */
		rt2860_mcu_bbp_write(sc, 25, 0x90);
		DELAY(1000);
		/* read received power */
		bbp55_pb = rt2860_mcu_bbp_read(sc, 55);
		if (bbp55_pb != 0)
			break;
	}
	if (ntries == 100)
		return ETIMEDOUT;

	/* set power and frequency of stopband test tone */
	rt2860_mcu_bbp_write(sc, 24, 0x06);
	for (ntries = 0; ntries < 100; ntries++) {
		/* transmit test tone */
		rt2860_mcu_bbp_write(sc, 25, 0x90);
		DELAY(1000);
		/* read received power */
		bbp55_sb = rt2860_mcu_bbp_read(sc, 55);

		delta = bbp55_pb - bbp55_sb;
		if (delta > target)
			break;

		/* reprogram filter */
		rf24++;
		rt3090_rf_write(sc, 24, rf24);
	}
	if (ntries < 100) {
		if (rf24 != init)
			rf24--;	/* backtrack */
		*val = rf24;
		rt3090_rf_write(sc, 24, rf24);
	}

	/* restore initial state */
	rt2860_mcu_bbp_write(sc, 24, 0x00);

	/* disable baseband loopback mode */
	rf22 = rt3090_rf_read(sc, 22);
	rt3090_rf_write(sc, 22, rf22 & ~RT3070_BB_LOOPBACK);

	return 0;
}

void
rt3090_rf_setup(struct rt2860_softc *sc)
{
	uint8_t bbp;
	int i;

	if (sc->mac_rev >= 0x0211) {
		/* enable DC filter */
		rt2860_mcu_bbp_write(sc, 103, 0xc0);

		/* improve power consumption */
		bbp = rt2860_mcu_bbp_read(sc, 31);
		rt2860_mcu_bbp_write(sc, 31, bbp & ~0x03);
	}

	RAL_WRITE(sc, RT2860_TX_SW_CFG1, 0);
	if (sc->mac_rev < 0x0211) {
		RAL_WRITE(sc, RT2860_TX_SW_CFG2,
		    sc->patch_dac ? 0x2c : 0x0f);
	} else
		RAL_WRITE(sc, RT2860_TX_SW_CFG2, 0);

	/* initialize RF registers from ROM */
	if (sc->mac_ver < 0x5390) {
		for (i = 0; i < 10; i++) {
			if (sc->rf[i].reg == 0 || sc->rf[i].reg == 0xff)
			 	continue;
			rt3090_rf_write(sc, sc->rf[i].reg, sc->rf[i].val);
		}
	}
}

void
rt2860_set_leds(struct rt2860_softc *sc, uint16_t which)
{
	rt2860_mcu_cmd(sc, RT2860_MCU_CMD_LEDS,
	    which | (sc->leds & 0x7f), 0);
}

/*
 * Hardware has a general-purpose programmable timer interrupt that can
 * periodically raise MAC_INT_4.
 */
void
rt2860_set_gp_timer(struct rt2860_softc *sc, int ms)
{
	uint32_t tmp;

	/* disable GP timer before reprogramming it */
	tmp = RAL_READ(sc, RT2860_INT_TIMER_EN);
	RAL_WRITE(sc, RT2860_INT_TIMER_EN, tmp & ~RT2860_GP_TIMER_EN);

	if (ms == 0)
		return;

	tmp = RAL_READ(sc, RT2860_INT_TIMER_CFG);
	ms *= 16;	/* Unit: 64us */
	tmp = (tmp & 0xffff) | ms << RT2860_GP_TIMER_SHIFT;
	RAL_WRITE(sc, RT2860_INT_TIMER_CFG, tmp);

	/* enable GP timer */
	tmp = RAL_READ(sc, RT2860_INT_TIMER_EN);
	RAL_WRITE(sc, RT2860_INT_TIMER_EN, tmp | RT2860_GP_TIMER_EN);
}

void
rt2860_set_bssid(struct rt2860_softc *sc, const uint8_t *bssid)
{
	RAL_WRITE(sc, RT2860_MAC_BSSID_DW0,
	    bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24);
	RAL_WRITE(sc, RT2860_MAC_BSSID_DW1,
	    bssid[4] | bssid[5] << 8);
}

void
rt2860_set_macaddr(struct rt2860_softc *sc, const uint8_t *addr)
{
	RAL_WRITE(sc, RT2860_MAC_ADDR_DW0,
	    addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	RAL_WRITE(sc, RT2860_MAC_ADDR_DW1,
	    addr[4] | addr[5] << 8 | 0xff << 16);
}

void
rt2860_updateslot(struct ieee80211com *ic)
{
	struct rt2860_softc *sc = ic->ic_softc;
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2860_BKOFF_SLOT_CFG);
	tmp &= ~0xff;
	tmp |= (ic->ic_flags & IEEE80211_F_SHSLOT) ?
	    IEEE80211_DUR_DS_SHSLOT : IEEE80211_DUR_DS_SLOT;
	RAL_WRITE(sc, RT2860_BKOFF_SLOT_CFG, tmp);
}

void
rt2860_updateprot(struct ieee80211com *ic)
{
	struct rt2860_softc *sc = ic->ic_softc;
	uint32_t tmp;

	tmp = RT2860_RTSTH_EN | RT2860_PROT_NAV_SHORT | RT2860_TXOP_ALLOW_ALL;
	/* setup protection frame rate (MCS code) */
	tmp |= (ic->ic_curmode == IEEE80211_MODE_11A) ?
	    rt2860_rates[RT2860_RIDX_OFDM6].mcs :
	    rt2860_rates[RT2860_RIDX_CCK11].mcs;

	/* CCK frames don't require protection */
	RAL_WRITE(sc, RT2860_CCK_PROT_CFG, tmp);

	if (ic->ic_flags & IEEE80211_F_USEPROT) {
		if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
			tmp |= RT2860_PROT_CTRL_RTS_CTS;
		else if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
			tmp |= RT2860_PROT_CTRL_CTS;
	}
	RAL_WRITE(sc, RT2860_OFDM_PROT_CFG, tmp);
}

void
rt2860_updateedca(struct ieee80211com *ic)
{
	struct rt2860_softc *sc = ic->ic_softc;
	int aci;

	/* update MAC TX configuration registers */
	for (aci = 0; aci < EDCA_NUM_AC; aci++) {
		RAL_WRITE(sc, RT2860_EDCA_AC_CFG(aci),
		    ic->ic_edca_ac[aci].ac_ecwmax << 16 |
		    ic->ic_edca_ac[aci].ac_ecwmin << 12 |
		    ic->ic_edca_ac[aci].ac_aifsn  <<  8 |
		    ic->ic_edca_ac[aci].ac_txoplimit);
	}

	/* update SCH/DMA registers too */
	RAL_WRITE(sc, RT2860_WMM_AIFSN_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_aifsn  << 12 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_aifsn  <<  8 |
	    ic->ic_edca_ac[EDCA_AC_BK].ac_aifsn  <<  4 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_aifsn);
	RAL_WRITE(sc, RT2860_WMM_CWMIN_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_ecwmin << 12 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_ecwmin <<  8 |
	    ic->ic_edca_ac[EDCA_AC_BK].ac_ecwmin <<  4 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_ecwmin);
	RAL_WRITE(sc, RT2860_WMM_CWMAX_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_ecwmax << 12 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_ecwmax <<  8 |
	    ic->ic_edca_ac[EDCA_AC_BK].ac_ecwmax <<  4 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_ecwmax);
	RAL_WRITE(sc, RT2860_WMM_TXOP0_CFG,
	    ic->ic_edca_ac[EDCA_AC_BK].ac_txoplimit << 16 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_txoplimit);
	RAL_WRITE(sc, RT2860_WMM_TXOP1_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_txoplimit << 16 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_txoplimit);
}

int
rt2860_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct rt2860_softc *sc = ic->ic_softc;
	bus_size_t base;
	uint32_t attr;
	uint8_t mode, wcid, iv[8];

	/* defer setting of WEP keys until interface is brought up */
	if ((ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) !=
	    (IFF_UP | IFF_RUNNING))
		return 0;

	/* map net80211 cipher to RT2860 security mode */
	switch (k->k_cipher) {
	case IEEE80211_CIPHER_WEP40:
		mode = RT2860_MODE_WEP40;
		break;
	case IEEE80211_CIPHER_WEP104:
		mode = RT2860_MODE_WEP104;
		break;
	case IEEE80211_CIPHER_TKIP:
		mode = RT2860_MODE_TKIP;
		break;
	case IEEE80211_CIPHER_CCMP:
		mode = RT2860_MODE_AES_CCMP;
		break;
	default:
		return EINVAL;
	}

	if (k->k_flags & IEEE80211_KEY_GROUP) {
		wcid = 0;	/* NB: update WCID0 for group keys */
		base = RT2860_SKEY(0, k->k_id);
	} else {
		wcid = ((struct rt2860_node *)ni)->wcid;
		base = RT2860_PKEY(wcid);
	}

	if (k->k_cipher == IEEE80211_CIPHER_TKIP) {
		RAL_WRITE_REGION_1(sc, base, k->k_key, 16);
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
			RAL_WRITE_REGION_1(sc, base + 16, &k->k_key[16], 8);
			RAL_WRITE_REGION_1(sc, base + 24, &k->k_key[24], 8);
		} else
#endif
		{
			RAL_WRITE_REGION_1(sc, base + 16, &k->k_key[24], 8);
			RAL_WRITE_REGION_1(sc, base + 24, &k->k_key[16], 8);
		}
	} else
		RAL_WRITE_REGION_1(sc, base, k->k_key, k->k_len);

	if (!(k->k_flags & IEEE80211_KEY_GROUP) ||
	    (k->k_flags & IEEE80211_KEY_TX)) {
		/* set initial packet number in IV+EIV */
		if (k->k_cipher == IEEE80211_CIPHER_WEP40 ||
		    k->k_cipher == IEEE80211_CIPHER_WEP104) {
			uint32_t val = arc4random();
			/* skip weak IVs from Fluhrer/Mantin/Shamir */
			if (val >= 0x03ff00 && (val & 0xf8ff00) == 0x00ff00)
				val += 0x000100;
			iv[0] = val;
			iv[1] = val >> 8;
			iv[2] = val >> 16;
			iv[3] = k->k_id << 6;
			iv[4] = iv[5] = iv[6] = iv[7] = 0;
		} else {
			if (k->k_cipher == IEEE80211_CIPHER_TKIP) {
				iv[0] = k->k_tsc >> 8;
				iv[1] = (iv[0] | 0x20) & 0x7f;
				iv[2] = k->k_tsc;
			} else /* CCMP */ {
				iv[0] = k->k_tsc;
				iv[1] = k->k_tsc >> 8;
				iv[2] = 0;
			}
			iv[3] = k->k_id << 6 | IEEE80211_WEP_EXTIV;
			iv[4] = k->k_tsc >> 16;
			iv[5] = k->k_tsc >> 24;
			iv[6] = k->k_tsc >> 32;
			iv[7] = k->k_tsc >> 40;
		}
		RAL_WRITE_REGION_1(sc, RT2860_IVEIV(wcid), iv, 8);
	}

	if (k->k_flags & IEEE80211_KEY_GROUP) {
		/* install group key */
		attr = RAL_READ(sc, RT2860_SKEY_MODE_0_7);
		attr &= ~(0xf << (k->k_id * 4));
		attr |= mode << (k->k_id * 4);
		RAL_WRITE(sc, RT2860_SKEY_MODE_0_7, attr);
	} else {
		/* install pairwise key */
		attr = RAL_READ(sc, RT2860_WCID_ATTR(wcid));
		attr = (attr & ~0xf) | (mode << 1) | RT2860_RX_PKEY_EN;
		RAL_WRITE(sc, RT2860_WCID_ATTR(wcid), attr);
	}
	return 0;
}

void
rt2860_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct rt2860_softc *sc = ic->ic_softc;
	uint32_t attr;
	uint8_t wcid;

	if (k->k_flags & IEEE80211_KEY_GROUP) {
		/* remove group key */
		attr = RAL_READ(sc, RT2860_SKEY_MODE_0_7);
		attr &= ~(0xf << (k->k_id * 4));
		RAL_WRITE(sc, RT2860_SKEY_MODE_0_7, attr);

	} else {
		/* remove pairwise key */
		wcid = ((struct rt2860_node *)ni)->wcid;
		attr = RAL_READ(sc, RT2860_WCID_ATTR(wcid));
		attr &= ~0xf;
		RAL_WRITE(sc, RT2860_WCID_ATTR(wcid), attr);
	}
}

#if NBPFILTER > 0
int8_t
rt2860_rssi2dbm(struct rt2860_softc *sc, uint8_t rssi, uint8_t rxchain)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c = ic->ic_ibss_chan;
	int delta;

	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		u_int chan = ieee80211_chan2ieee(ic, c);
		delta = sc->rssi_5ghz[rxchain];

		/* determine channel group */
		if (chan <= 64)
			delta -= sc->lna[1];
		else if (chan <= 128)
			delta -= sc->lna[2];
		else
			delta -= sc->lna[3];
	} else
		delta = sc->rssi_2ghz[rxchain] - sc->lna[0];

	return -12 - delta - rssi;
}
#endif

/*
 * Add `delta' (signed) to each 4-bit sub-word of a 32-bit word.
 * Used to adjust per-rate Tx power registers.
 */
static __inline uint32_t
b4inc(uint32_t b32, int8_t delta)
{
	int8_t i, b4;

	for (i = 0; i < 8; i++) {
		b4 = b32 & 0xf;
		b4 += delta;
		if (b4 < 0)
			b4 = 0;
		else if (b4 > 0xf)
			b4 = 0xf;
		b32 = b32 >> 4 | b4 << 28;
	}
	return b32;
}

const char *
rt2860_get_rf(uint16_t rev)
{
	switch (rev) {
	case RT2860_RF_2820:	return "RT2820";
	case RT2860_RF_2850:	return "RT2850";
	case RT2860_RF_2720:	return "RT2720";
	case RT2860_RF_2750:	return "RT2750";
	case RT3070_RF_3020:	return "RT3020";
	case RT3070_RF_2020:	return "RT2020";
	case RT3070_RF_3021:	return "RT3021";
	case RT3070_RF_3022:	return "RT3022";
	case RT3070_RF_3052:	return "RT3052";
	case RT3070_RF_3320:	return "RT3320";
	case RT3070_RF_3053:	return "RT3053";
	case RT3290_RF_3290:	return "RT3290";
	case RT5390_RF_5360:	return "RT5360";
	case RT5390_RF_5390:	return "RT5390";
	case RT5390_RF_5392:	return "RT5392";
	default:		return "unknown";
	}
}

int
rt2860_read_eeprom(struct rt2860_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int8_t delta_2ghz, delta_5ghz;
	uint32_t tmp;
	uint16_t val;
	int ridx, ant, i;

	/* check whether the ROM is eFUSE ROM or EEPROM */
	sc->sc_srom_read = rt2860_eeprom_read_2;
	if (sc->mac_ver == 0x3290) {
		tmp = RAL_READ(sc, RT3290_EFUSE_CTRL);
		DPRINTF(("EFUSE_CTRL=0x%08x\n", tmp));
		if (tmp & RT3070_SEL_EFUSE)
			sc->sc_srom_read = rt3290_efuse_read_2;
	} else if (sc->mac_ver >= 0x3071) {
		tmp = RAL_READ(sc, RT3070_EFUSE_CTRL);
		DPRINTF(("EFUSE_CTRL=0x%08x\n", tmp));
		if (tmp & RT3070_SEL_EFUSE)
			sc->sc_srom_read = rt3090_efuse_read_2;
	}

	/* read EEPROM version */
	val = rt2860_srom_read(sc, RT2860_EEPROM_VERSION);
	DPRINTF(("EEPROM rev=%d, FAE=%d\n", val & 0xff, val >> 8));

	/* read MAC address */
	val = rt2860_srom_read(sc, RT2860_EEPROM_MAC01);
	ic->ic_myaddr[0] = val & 0xff;
	ic->ic_myaddr[1] = val >> 8;
	val = rt2860_srom_read(sc, RT2860_EEPROM_MAC23);
	ic->ic_myaddr[2] = val & 0xff;
	ic->ic_myaddr[3] = val >> 8;
	val = rt2860_srom_read(sc, RT2860_EEPROM_MAC45);
	ic->ic_myaddr[4] = val & 0xff;
	ic->ic_myaddr[5] = val >> 8;

	/* read country code */
	val = rt2860_srom_read(sc, RT2860_EEPROM_COUNTRY);
	DPRINTF(("EEPROM region code=0x%04x\n", val));

	/* read vendor BBP settings */
	for (i = 0; i < 8; i++) {
		val = rt2860_srom_read(sc, RT2860_EEPROM_BBP_BASE + i);
		sc->bbp[i].val = val & 0xff;
		sc->bbp[i].reg = val >> 8;
		DPRINTF(("BBP%d=0x%02x\n", sc->bbp[i].reg, sc->bbp[i].val));
	}
	if (sc->mac_ver >= 0x3071) {
		/* read vendor RF settings */
		for (i = 0; i < 10; i++) {
			val = rt2860_srom_read(sc, RT3071_EEPROM_RF_BASE + i);
			sc->rf[i].val = val & 0xff;
			sc->rf[i].reg = val >> 8;
			DPRINTF(("RF%d=0x%02x\n", sc->rf[i].reg,
			    sc->rf[i].val));
		}
	}

	/* read RF frequency offset from EEPROM */
	val = rt2860_srom_read(sc, RT2860_EEPROM_FREQ_LEDS);
	sc->freq = ((val & 0xff) != 0xff) ? val & 0xff : 0;
	DPRINTF(("EEPROM freq offset %d\n", sc->freq & 0xff));
	if ((val >> 8) != 0xff) {
		/* read LEDs operating mode */
		sc->leds = val >> 8;
		sc->led[0] = rt2860_srom_read(sc, RT2860_EEPROM_LED1);
		sc->led[1] = rt2860_srom_read(sc, RT2860_EEPROM_LED2);
		sc->led[2] = rt2860_srom_read(sc, RT2860_EEPROM_LED3);
	} else {
		/* broken EEPROM, use default settings */
		sc->leds = 0x01;
		sc->led[0] = 0x5555;
		sc->led[1] = 0x2221;
		sc->led[2] = 0xa9f8;
	}
	DPRINTF(("EEPROM LED mode=0x%02x, LEDs=0x%04x/0x%04x/0x%04x\n",
	    sc->leds, sc->led[0], sc->led[1], sc->led[2]));

	/* read RF information */
	val = rt2860_srom_read(sc, RT2860_EEPROM_ANTENNA);
	DPRINTF(("EEPROM ANT 0x%04x\n", val));
	if (sc->mac_ver >= 0x5390 || sc->mac_ver == 0x3290)
		sc->rf_rev = rt2860_srom_read(sc, RT2860_EEPROM_CHIPID);
	else
		sc->rf_rev = (val >> 8) & 0xf;
	sc->ntxchains = (val >> 4) & 0xf;
	sc->nrxchains = val & 0xf;
	DPRINTF(("EEPROM RF rev=0x%02x chains=%dT%dR\n",
	    sc->rf_rev, sc->ntxchains, sc->nrxchains));

	/* check if RF supports automatic Tx access gain control */
	val = rt2860_srom_read(sc, RT2860_EEPROM_CONFIG);
	DPRINTF(("EEPROM CFG 0x%04x\n", val));
	/* check if driver should patch the DAC issue */
	if ((val >> 8) != 0xff)
		sc->patch_dac = (val >> 15) & 1;
	if ((val & 0xff) != 0xff) {
		sc->ext_5ghz_lna = (val >> 3) & 1;
		sc->ext_2ghz_lna = (val >> 2) & 1;
		/* check if RF supports automatic Tx access gain control */
		sc->calib_2ghz = sc->calib_5ghz = 0; /* XXX (val >> 1) & 1 */
		/* check if we have a hardware radio switch */
		sc->rfswitch = val & 1;
	}
	if (sc->sc_flags & RT2860_ADVANCED_PS) {
		/* read PCIe power save level */
		val = rt2860_srom_read(sc, RT2860_EEPROM_PCIE_PSLEVEL);
		if ((val & 0xff) != 0xff) {
			sc->pslevel = val & 0x3;
			val = rt2860_srom_read(sc, RT2860_EEPROM_REV);
			if ((val & 0xff80) != 0x9280)
				sc->pslevel = MIN(sc->pslevel, 1);
			DPRINTF(("EEPROM PCIe PS Level=%d\n", sc->pslevel));
		}
	}

	/* read power settings for 2GHz channels */
	for (i = 0; i < 14; i += 2) {
		val = rt2860_srom_read(sc,
		    RT2860_EEPROM_PWR2GHZ_BASE1 + i / 2);
		sc->txpow1[i + 0] = (int8_t)(val & 0xff);
		sc->txpow1[i + 1] = (int8_t)(val >> 8);

		if (sc->mac_ver != 0x5390) {
			val = rt2860_srom_read(sc,
			    RT2860_EEPROM_PWR2GHZ_BASE2 + i / 2);
			sc->txpow2[i + 0] = (int8_t)(val & 0xff);
			sc->txpow2[i + 1] = (int8_t)(val >> 8);
		}
	}
	/* fix broken Tx power entries */
	for (i = 0; i < 14; i++) {
		if (sc->txpow1[i] < 0 ||
		    sc->txpow1[i] > ((sc->mac_ver >= 0x5390) ? 39 : 31))
		        sc->txpow1[i] = 5;
		if (sc->mac_ver != 0x5390) {
			if (sc->txpow2[i] < 0 ||
			    sc->txpow2[i] > ((sc->mac_ver == 0x5392) ? 39 : 31))
			        sc->txpow2[i] = 5;
		}
		DPRINTF(("chan %d: power1=%d, power2=%d\n",
		    rt2860_rf2850[i].chan, sc->txpow1[i], sc->txpow2[i]));
	}
	/* read power settings for 5GHz channels */
	for (i = 0; i < 40; i += 2) {
		val = rt2860_srom_read(sc,
		    RT2860_EEPROM_PWR5GHZ_BASE1 + i / 2);
		sc->txpow1[i + 14] = (int8_t)(val & 0xff);
		sc->txpow1[i + 15] = (int8_t)(val >> 8);

		val = rt2860_srom_read(sc,
		    RT2860_EEPROM_PWR5GHZ_BASE2 + i / 2);
		sc->txpow2[i + 14] = (int8_t)(val & 0xff);
		sc->txpow2[i + 15] = (int8_t)(val >> 8);
	}
	/* fix broken Tx power entries */
	for (i = 0; i < 40; i++) {
		if (sc->txpow1[14 + i] < -7 || sc->txpow1[14 + i] > 15)
			sc->txpow1[14 + i] = 5;
		if (sc->txpow2[14 + i] < -7 || sc->txpow2[14 + i] > 15)
			sc->txpow2[14 + i] = 5;
		DPRINTF(("chan %d: power1=%d, power2=%d\n",
		    rt2860_rf2850[14 + i].chan, sc->txpow1[14 + i],
		    sc->txpow2[14 + i]));
	}

	/* read Tx power compensation for each Tx rate */
	val = rt2860_srom_read(sc, RT2860_EEPROM_DELTAPWR);
	delta_2ghz = delta_5ghz = 0;
	if ((val & 0xff) != 0xff && (val & 0x80)) {
		delta_2ghz = val & 0xf;
		if (!(val & 0x40))	/* negative number */
			delta_2ghz = -delta_2ghz;
	}
	val >>= 8;
	if ((val & 0xff) != 0xff && (val & 0x80)) {
		delta_5ghz = val & 0xf;
		if (!(val & 0x40))	/* negative number */
			delta_5ghz = -delta_5ghz;
	}
	DPRINTF(("power compensation=%d (2GHz), %d (5GHz)\n",
	    delta_2ghz, delta_5ghz));

	for (ridx = 0; ridx < 5; ridx++) {
		uint32_t reg;

		val = rt2860_srom_read(sc, RT2860_EEPROM_RPWR + ridx * 2);
		reg = val;
		val = rt2860_srom_read(sc, RT2860_EEPROM_RPWR + ridx * 2 + 1);
		reg |= (uint32_t)val << 16;

		sc->txpow20mhz[ridx] = reg;
		sc->txpow40mhz_2ghz[ridx] = b4inc(reg, delta_2ghz);
		sc->txpow40mhz_5ghz[ridx] = b4inc(reg, delta_5ghz);

		DPRINTF(("ridx %d: power 20MHz=0x%08x, 40MHz/2GHz=0x%08x, "
		    "40MHz/5GHz=0x%08x\n", ridx, sc->txpow20mhz[ridx],
		    sc->txpow40mhz_2ghz[ridx], sc->txpow40mhz_5ghz[ridx]));
	}

	/* read factory-calibrated samples for temperature compensation */
	val = rt2860_srom_read(sc, RT2860_EEPROM_TSSI1_2GHZ);
	sc->tssi_2ghz[0] = val & 0xff;	/* [-4] */
	sc->tssi_2ghz[1] = val >> 8;	/* [-3] */
	val = rt2860_srom_read(sc, RT2860_EEPROM_TSSI2_2GHZ);
	sc->tssi_2ghz[2] = val & 0xff;	/* [-2] */
	sc->tssi_2ghz[3] = val >> 8;	/* [-1] */
	val = rt2860_srom_read(sc, RT2860_EEPROM_TSSI3_2GHZ);
	sc->tssi_2ghz[4] = val & 0xff;	/* [+0] */
	sc->tssi_2ghz[5] = val >> 8;	/* [+1] */
	val = rt2860_srom_read(sc, RT2860_EEPROM_TSSI4_2GHZ);
	sc->tssi_2ghz[6] = val & 0xff;	/* [+2] */
	sc->tssi_2ghz[7] = val >> 8;	/* [+3] */
	val = rt2860_srom_read(sc, RT2860_EEPROM_TSSI5_2GHZ);
	sc->tssi_2ghz[8] = val & 0xff;	/* [+4] */
	sc->step_2ghz = val >> 8;
	DPRINTF(("TSSI 2GHz: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x "
	    "0x%02x 0x%02x step=%d\n", sc->tssi_2ghz[0], sc->tssi_2ghz[1],
	    sc->tssi_2ghz[2], sc->tssi_2ghz[3], sc->tssi_2ghz[4],
	    sc->tssi_2ghz[5], sc->tssi_2ghz[6], sc->tssi_2ghz[7],
	    sc->tssi_2ghz[8], sc->step_2ghz));
	/* check that ref value is correct, otherwise disable calibration */
	if (sc->tssi_2ghz[4] == 0xff)
		sc->calib_2ghz = 0;

	val = rt2860_srom_read(sc, RT2860_EEPROM_TSSI1_5GHZ);
	sc->tssi_5ghz[0] = val & 0xff;	/* [-4] */
	sc->tssi_5ghz[1] = val >> 8;	/* [-3] */
	val = rt2860_srom_read(sc, RT2860_EEPROM_TSSI2_5GHZ);
	sc->tssi_5ghz[2] = val & 0xff;	/* [-2] */
	sc->tssi_5ghz[3] = val >> 8;	/* [-1] */
	val = rt2860_srom_read(sc, RT2860_EEPROM_TSSI3_5GHZ);
	sc->tssi_5ghz[4] = val & 0xff;	/* [+0] */
	sc->tssi_5ghz[5] = val >> 8;	/* [+1] */
	val = rt2860_srom_read(sc, RT2860_EEPROM_TSSI4_5GHZ);
	sc->tssi_5ghz[6] = val & 0xff;	/* [+2] */
	sc->tssi_5ghz[7] = val >> 8;	/* [+3] */
	val = rt2860_srom_read(sc, RT2860_EEPROM_TSSI5_5GHZ);
	sc->tssi_5ghz[8] = val & 0xff;	/* [+4] */
	sc->step_5ghz = val >> 8;
	DPRINTF(("TSSI 5GHz: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x "
	    "0x%02x 0x%02x step=%d\n", sc->tssi_5ghz[0], sc->tssi_5ghz[1],
	    sc->tssi_5ghz[2], sc->tssi_5ghz[3], sc->tssi_5ghz[4],
	    sc->tssi_5ghz[5], sc->tssi_5ghz[6], sc->tssi_5ghz[7],
	    sc->tssi_5ghz[8], sc->step_5ghz));
	/* check that ref value is correct, otherwise disable calibration */
	if (sc->tssi_5ghz[4] == 0xff)
		sc->calib_5ghz = 0;

	/* read RSSI offsets and LNA gains from EEPROM */
	val = rt2860_srom_read(sc, RT2860_EEPROM_RSSI1_2GHZ);
	sc->rssi_2ghz[0] = val & 0xff;	/* Ant A */
	sc->rssi_2ghz[1] = val >> 8;	/* Ant B */
	val = rt2860_srom_read(sc, RT2860_EEPROM_RSSI2_2GHZ);
	if (sc->mac_ver >= 0x3071) {
		/*
		 * On RT3090 chips (limited to 2 Rx chains), this ROM
		 * field contains the Tx mixer gain for the 2GHz band.
		 */
		if ((val & 0xff) != 0xff)
			sc->txmixgain_2ghz = val & 0x7;
		DPRINTF(("tx mixer gain=%u (2GHz)\n", sc->txmixgain_2ghz));
	} else
		sc->rssi_2ghz[2] = val & 0xff;	/* Ant C */
	sc->lna[2] = val >> 8;		/* channel group 2 */

	val = rt2860_srom_read(sc, RT2860_EEPROM_RSSI1_5GHZ);
	sc->rssi_5ghz[0] = val & 0xff;	/* Ant A */
	sc->rssi_5ghz[1] = val >> 8;	/* Ant B */
	val = rt2860_srom_read(sc, RT2860_EEPROM_RSSI2_5GHZ);
	sc->rssi_5ghz[2] = val & 0xff;	/* Ant C */
	sc->lna[3] = val >> 8;		/* channel group 3 */

	val = rt2860_srom_read(sc, RT2860_EEPROM_LNA);
	if (sc->mac_ver >= 0x3071)
		sc->lna[0] = RT3090_DEF_LNA;
	else				/* channel group 0 */
		sc->lna[0] = val & 0xff;
	sc->lna[1] = val >> 8;		/* channel group 1 */

	/* fix broken 5GHz LNA entries */
	if (sc->lna[2] == 0 || sc->lna[2] == 0xff) {
		DPRINTF(("invalid LNA for channel group %d\n", 2));
		sc->lna[2] = sc->lna[1];
	}
	if (sc->lna[3] == 0 || sc->lna[3] == 0xff) {
		DPRINTF(("invalid LNA for channel group %d\n", 3));
		sc->lna[3] = sc->lna[1];
	}

	/* fix broken RSSI offset entries */
	for (ant = 0; ant < 3; ant++) {
		if (sc->rssi_2ghz[ant] < -10 || sc->rssi_2ghz[ant] > 10) {
			DPRINTF(("invalid RSSI%d offset: %d (2GHz)\n",
			    ant + 1, sc->rssi_2ghz[ant]));
			sc->rssi_2ghz[ant] = 0;
		}
		if (sc->rssi_5ghz[ant] < -10 || sc->rssi_5ghz[ant] > 10) {
			DPRINTF(("invalid RSSI%d offset: %d (5GHz)\n",
			    ant + 1, sc->rssi_5ghz[ant]));
			sc->rssi_5ghz[ant] = 0;
		}
	}

	return 0;
}

int
rt2860_bbp_init(struct rt2860_softc *sc)
{
	int i, ntries;

	/* wait for BBP to wake up */
	for (ntries = 0; ntries < 20; ntries++) {
		uint8_t bbp0 = rt2860_mcu_bbp_read(sc, 0);
		if (bbp0 != 0 && bbp0 != 0xff)
			break;
	}
	if (ntries == 20) {
		printf("%s: timeout waiting for BBP to wake up\n",
		    sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	/* initialize BBP registers to default values */
	if (sc->mac_ver >= 0x5390 ||
	    sc->mac_ver == 0x3290)
		rt5390_bbp_init(sc);
	else {
		for (i = 0; i < nitems(rt2860_def_bbp); i++) {
			rt2860_mcu_bbp_write(sc, rt2860_def_bbp[i].reg,
			    rt2860_def_bbp[i].val);
		}
	}

	/* fix BBP84 for RT2860E */
	if (sc->mac_ver == 0x2860 && sc->mac_rev != 0x0101)
		rt2860_mcu_bbp_write(sc, 84, 0x19);

	if (sc->mac_ver >= 0x3071) {
		rt2860_mcu_bbp_write(sc, 79, 0x13);
		rt2860_mcu_bbp_write(sc, 80, 0x05);
		rt2860_mcu_bbp_write(sc, 81, 0x33);
	} else if (sc->mac_ver == 0x2860 && sc->mac_rev == 0x0100) {
		rt2860_mcu_bbp_write(sc, 69, 0x16);
		rt2860_mcu_bbp_write(sc, 73, 0x12);
	}

	return 0;
}

void
rt5390_bbp_init(struct rt2860_softc *sc)
{
	uint8_t bbp;
	int i;

	/* Apply maximum likelihood detection for 2 stream case. */
	if (sc->nrxchains > 1) {
		bbp = rt2860_mcu_bbp_read(sc, 105);
		rt2860_mcu_bbp_write(sc, 105, bbp | RT5390_MLD);
	}

	/* Avoid data lost and CRC error. */
	bbp = rt2860_mcu_bbp_read(sc, 4);
	rt2860_mcu_bbp_write(sc, 4, bbp | RT5390_MAC_IF_CTRL);
	if (sc->mac_ver == 0x3290) {
		for (i = 0; i < nitems(rt3290_def_bbp); i++) {
			rt2860_mcu_bbp_write(sc, rt3290_def_bbp[i].reg,
			    rt3290_def_bbp[i].val);
		}
	} else {
		for (i = 0; i < nitems(rt5390_def_bbp); i++) {
			rt2860_mcu_bbp_write(sc, rt5390_def_bbp[i].reg,
			    rt5390_def_bbp[i].val);
		}
	}

	if (sc->mac_ver == 0x5392) {
		rt2860_mcu_bbp_write(sc, 84, 0x9a);
		rt2860_mcu_bbp_write(sc, 95, 0x9a);
		rt2860_mcu_bbp_write(sc, 98, 0x12);
		rt2860_mcu_bbp_write(sc, 106, 0x05);
		rt2860_mcu_bbp_write(sc, 134, 0xd0);
		rt2860_mcu_bbp_write(sc, 135, 0xf6);
	}

	bbp = rt2860_mcu_bbp_read(sc, 152);
	rt2860_mcu_bbp_write(sc, 152, bbp | 0x80);

	/* Disable hardware antenna diversity. */
	if (sc->mac_ver == 0x5390)
		rt2860_mcu_bbp_write(sc, 154, 0);
}

int
rt3290_wlan_enable(struct rt2860_softc *sc)
{
	uint32_t tmp;
	int ntries;

	/* enable chip and check readiness */
	tmp = RAL_READ(sc, RT3290_WLAN_CTRL);
	tmp |= RT3290_WLAN_EN | RT3290_FRC_WL_ANT_SET |
	    RT3290_GPIO_OUT_OE_ALL;
	RAL_WRITE(sc, RT3290_WLAN_CTRL, tmp);

	for (ntries = 0; ntries < 200; ntries++) {
		tmp = RAL_READ(sc, RT3290_CMB_CTRL);
		if ((tmp & RT3290_PLL_LD) &&
		    (tmp & RT3290_XTAL_RDY))
			break;
		DELAY(20);
	}
	if (ntries == 200)
		return EIO;

	/* toggle reset */
	tmp = RAL_READ(sc, RT3290_WLAN_CTRL);
	tmp |= RT3290_WLAN_RESET | RT3290_WLAN_CLK_EN;
	tmp &= ~RT3290_PCIE_APP0_CLK_REQ;
	RAL_WRITE(sc, RT3290_WLAN_CTRL, tmp);
	DELAY(20);
	tmp &= ~RT3290_WLAN_RESET;
	RAL_WRITE(sc, RT3290_WLAN_CTRL, tmp);
	DELAY(1000);

	/* clear garbage interrupts */
	RAL_WRITE(sc, RT2860_INT_STATUS, 0x7fffffff);

	return 0;
}

int
rt2860_txrx_enable(struct rt2860_softc *sc)
{
	uint32_t tmp;
	int ntries;

	/* enable Tx/Rx DMA engine */
	RAL_WRITE(sc, RT2860_MAC_SYS_CTRL, RT2860_MAC_TX_EN);
	RAL_BARRIER_READ_WRITE(sc);
	for (ntries = 0; ntries < 200; ntries++) {
		tmp = RAL_READ(sc, RT2860_WPDMA_GLO_CFG);
		if ((tmp & (RT2860_TX_DMA_BUSY | RT2860_RX_DMA_BUSY)) == 0)
			break;
		DELAY(1000);
	}
	if (ntries == 200) {
		printf("%s: timeout waiting for DMA engine\n",
		    sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	DELAY(50);

	tmp |= RT2860_RX_DMA_EN | RT2860_TX_DMA_EN |
	    RT2860_WPDMA_BT_SIZE64 << RT2860_WPDMA_BT_SIZE_SHIFT;
	RAL_WRITE(sc, RT2860_WPDMA_GLO_CFG, tmp);

	/* set Rx filter */
	tmp = RT2860_DROP_CRC_ERR | RT2860_DROP_PHY_ERR;
	if (sc->sc_ic.ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RT2860_DROP_UC_NOME | RT2860_DROP_DUPL |
		    RT2860_DROP_CTS | RT2860_DROP_BA | RT2860_DROP_ACK |
		    RT2860_DROP_VER_ERR | RT2860_DROP_CTRL_RSV |
		    RT2860_DROP_CFACK | RT2860_DROP_CFEND;
		if (sc->sc_ic.ic_opmode == IEEE80211_M_STA)
			tmp |= RT2860_DROP_RTS | RT2860_DROP_PSPOLL;
	}
	RAL_WRITE(sc, RT2860_RX_FILTR_CFG, tmp);

	RAL_WRITE(sc, RT2860_MAC_SYS_CTRL,
	    RT2860_MAC_RX_EN | RT2860_MAC_TX_EN);

	return 0;
}

int
rt2860_init(struct ifnet *ifp)
{
	struct rt2860_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	uint8_t bbp1, bbp3;
	int i, qid, ridx, ntries, error;

	/* for CardBus, power on the socket */
	if (!(sc->sc_flags & RT2860_ENABLED)) {
		if (sc->sc_enable != NULL && (*sc->sc_enable)(sc) != 0) {
			printf("%s: could not enable device\n",
			    sc->sc_dev.dv_xname);
			return EIO;
		}
		sc->sc_flags |= RT2860_ENABLED;
	}

	if (sc->mac_ver == 0x3290) {
		if ((error = rt3290_wlan_enable(sc)) != 0) {
			printf("%s: could not enable wlan\n",
			    sc->sc_dev.dv_xname);
			rt2860_stop(ifp, 1);
			return error;	
		}
	}

	if (sc->mac_ver == 0x3290 && sc->rfswitch){
		/* hardware has a radio switch on GPIO pin 0 */
		if (!(RAL_READ(sc, RT3290_WLAN_CTRL) & RT3290_RADIO_EN)) {
			printf("%s: radio is disabled by hardware switch\n",
 			    sc->sc_dev.dv_xname);
		}
	} else if (sc->rfswitch) {
		/* hardware has a radio switch on GPIO pin 2 */
		if (!(RAL_READ(sc, RT2860_GPIO_CTRL) & (1 << 2))) {
			printf("%s: radio is disabled by hardware switch\n",
			    sc->sc_dev.dv_xname);
#ifdef notyet
			rt2860_stop(ifp, 1);
			return EPERM;
#endif
		}
	}
	RAL_WRITE(sc, RT2860_PWR_PIN_CFG, RT2860_IO_RA_PE);

	/* disable DMA */
	tmp = RAL_READ(sc, RT2860_WPDMA_GLO_CFG);
	tmp &= 0xff0;
	RAL_WRITE(sc, RT2860_WPDMA_GLO_CFG, tmp);

	/* PBF hardware reset */
	RAL_WRITE(sc, RT2860_SYS_CTRL, 0xe1f);
	RAL_BARRIER_WRITE(sc);
	RAL_WRITE(sc, RT2860_SYS_CTRL, 0xe00);

	if ((error = rt2860_load_microcode(sc)) != 0) {
		printf("%s: could not load 8051 microcode\n",
		    sc->sc_dev.dv_xname);
		rt2860_stop(ifp, 1);
		return error;
	}

	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	rt2860_set_macaddr(sc, ic->ic_myaddr);

	/* init Tx power for all Tx rates (from EEPROM) */
	for (ridx = 0; ridx < 5; ridx++) {
		if (sc->txpow20mhz[ridx] == 0xffffffff)
			continue;
		RAL_WRITE(sc, RT2860_TX_PWR_CFG(ridx), sc->txpow20mhz[ridx]);
	}

	for (ntries = 0; ntries < 100; ntries++) {
		tmp = RAL_READ(sc, RT2860_WPDMA_GLO_CFG);
		if ((tmp & (RT2860_TX_DMA_BUSY | RT2860_RX_DMA_BUSY)) == 0)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for DMA engine\n",
		    sc->sc_dev.dv_xname);
		rt2860_stop(ifp, 1);
		return ETIMEDOUT;
	}
	tmp &= 0xff0;
	RAL_WRITE(sc, RT2860_WPDMA_GLO_CFG, tmp);

	/* reset Rx ring and all 6 Tx rings */
	RAL_WRITE(sc, RT2860_WPDMA_RST_IDX, 0x1003f);

	/* PBF hardware reset */
	RAL_WRITE(sc, RT2860_SYS_CTRL, 0xe1f);
	RAL_BARRIER_WRITE(sc);
	RAL_WRITE(sc, RT2860_SYS_CTRL, 0xe00);

	RAL_WRITE(sc, RT2860_PWR_PIN_CFG, RT2860_IO_RA_PE | RT2860_IO_RF_PE);

	RAL_WRITE(sc, RT2860_MAC_SYS_CTRL, RT2860_BBP_HRST | RT2860_MAC_SRST);
	RAL_BARRIER_WRITE(sc);
	RAL_WRITE(sc, RT2860_MAC_SYS_CTRL, 0);

	for (i = 0; i < nitems(rt2860_def_mac); i++)
		RAL_WRITE(sc, rt2860_def_mac[i].reg, rt2860_def_mac[i].val);
	if (sc->mac_ver == 0x3290 ||
	    sc->mac_ver >= 0x5390)
		RAL_WRITE(sc, RT2860_TX_SW_CFG0, 0x00000404);
	else if (sc->mac_ver >= 0x3071) {
		/* set delay of PA_PE assertion to 1us (unit of 0.25us) */
		RAL_WRITE(sc, RT2860_TX_SW_CFG0,
		    4 << RT2860_DLY_PAPE_EN_SHIFT);
	}

	if (!(RAL_READ(sc, RT2860_PCI_CFG) & RT2860_PCI_CFG_PCI)) {
		sc->sc_flags |= RT2860_PCIE;
		/* PCIe has different clock cycle count than PCI */
		tmp = RAL_READ(sc, RT2860_US_CYC_CNT);
		tmp = (tmp & ~0xff) | 0x7d;
		RAL_WRITE(sc, RT2860_US_CYC_CNT, tmp);
	}

	/* wait while MAC is busy */
	for (ntries = 0; ntries < 100; ntries++) {
		if (!(RAL_READ(sc, RT2860_MAC_STATUS_REG) &
		    (RT2860_RX_STATUS_BUSY | RT2860_TX_STATUS_BUSY)))
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for MAC\n", sc->sc_dev.dv_xname);
		rt2860_stop(ifp, 1);
		return ETIMEDOUT;
	}

	/* clear Host to MCU mailbox */
	RAL_WRITE(sc, RT2860_H2M_BBPAGENT, 0);
	RAL_WRITE(sc, RT2860_H2M_MAILBOX, 0);

	rt2860_mcu_cmd(sc, RT2860_MCU_CMD_RFRESET, 0, 0);
	DELAY(1000);

	if ((error = rt2860_bbp_init(sc)) != 0) {
		rt2860_stop(ifp, 1);
		return error;
	}

	/* clear RX WCID search table */
	RAL_SET_REGION_4(sc, RT2860_WCID_ENTRY(0), 0, 512);
	/* clear pairwise key table */
	RAL_SET_REGION_4(sc, RT2860_PKEY(0), 0, 2048);
	/* clear IV/EIV table */
	RAL_SET_REGION_4(sc, RT2860_IVEIV(0), 0, 512);
	/* clear WCID attribute table */
	RAL_SET_REGION_4(sc, RT2860_WCID_ATTR(0), 0, 256);
	/* clear shared key table */
	RAL_SET_REGION_4(sc, RT2860_SKEY(0, 0), 0, 8 * 32);
	/* clear shared key mode */
	RAL_SET_REGION_4(sc, RT2860_SKEY_MODE_0_7, 0, 4);

	/* init Tx rings (4 EDCAs + HCCA + Mgt) */
	for (qid = 0; qid < 6; qid++) {
		RAL_WRITE(sc, RT2860_TX_BASE_PTR(qid), sc->txq[qid].paddr);
		RAL_WRITE(sc, RT2860_TX_MAX_CNT(qid), RT2860_TX_RING_COUNT);
		RAL_WRITE(sc, RT2860_TX_CTX_IDX(qid), 0);
	}

	/* init Rx ring */
	RAL_WRITE(sc, RT2860_RX_BASE_PTR, sc->rxq.paddr);
	RAL_WRITE(sc, RT2860_RX_MAX_CNT, RT2860_RX_RING_COUNT);
	RAL_WRITE(sc, RT2860_RX_CALC_IDX, RT2860_RX_RING_COUNT - 1);

	/* setup maximum buffer sizes */
	RAL_WRITE(sc, RT2860_MAX_LEN_CFG, 1 << 12 |
	    (MCLBYTES - sizeof (struct rt2860_rxwi) - 2));

	for (ntries = 0; ntries < 100; ntries++) {
		tmp = RAL_READ(sc, RT2860_WPDMA_GLO_CFG);
		if ((tmp & (RT2860_TX_DMA_BUSY | RT2860_RX_DMA_BUSY)) == 0)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for DMA engine\n",
		    sc->sc_dev.dv_xname);
		rt2860_stop(ifp, 1);
		return ETIMEDOUT;
	}
	tmp &= 0xff0;
	RAL_WRITE(sc, RT2860_WPDMA_GLO_CFG, tmp);

	/* disable interrupts mitigation */
	RAL_WRITE(sc, RT2860_DELAY_INT_CFG, 0);

	/* write vendor-specific BBP values (from EEPROM) */
	for (i = 0; i < 8; i++) {
		if (sc->bbp[i].reg == 0 || sc->bbp[i].reg == 0xff)
			continue;
		rt2860_mcu_bbp_write(sc, sc->bbp[i].reg, sc->bbp[i].val);
	}

	/* select Main antenna for 1T1R devices */
	if (sc->rf_rev == RT3070_RF_2020 ||
	    sc->rf_rev == RT3070_RF_3020 ||
	    sc->rf_rev == RT3290_RF_3290 ||
	    sc->rf_rev == RT3070_RF_3320 ||
	    sc->rf_rev == RT5390_RF_5390)
		rt3090_set_rx_antenna(sc, 0);

	/* send LEDs operating mode to microcontroller */
	rt2860_mcu_cmd(sc, RT2860_MCU_CMD_LED1, sc->led[0], 0);
	rt2860_mcu_cmd(sc, RT2860_MCU_CMD_LED2, sc->led[1], 0);
	rt2860_mcu_cmd(sc, RT2860_MCU_CMD_LED3, sc->led[2], 0);

	if (sc->mac_ver == 0x3290 ||
	    sc->mac_ver >= 0x5390)
		rt5390_rf_init(sc);
	else if (sc->mac_ver >= 0x3071)
		rt3090_rf_init(sc);

	rt2860_mcu_cmd(sc, RT2860_MCU_CMD_SLEEP, 0x02ff, 1);
	rt2860_mcu_cmd(sc, RT2860_MCU_CMD_WAKEUP, 0, 1);

	if (sc->mac_ver == 0x3290 ||
	    sc->mac_ver >= 0x5390)
		rt5390_rf_wakeup(sc);
	else if (sc->mac_ver >= 0x3071)
		rt3090_rf_wakeup(sc);

	/* disable non-existing Rx chains */
	bbp3 = rt2860_mcu_bbp_read(sc, 3);
	bbp3 &= ~(1 << 3 | 1 << 4);
	if (sc->nrxchains == 2)
		bbp3 |= 1 << 3;
	else if (sc->nrxchains == 3)
		bbp3 |= 1 << 4;
	rt2860_mcu_bbp_write(sc, 3, bbp3);

	/* disable non-existing Tx chains */
	bbp1 = rt2860_mcu_bbp_read(sc, 1);
	if (sc->ntxchains == 1)
		bbp1 = (bbp1 & ~(1 << 3 | 1 << 4));
	else if (sc->mac_ver == 0x3593 && sc->ntxchains == 2)
		bbp1 = (bbp1 & ~(1 << 4)) | 1 << 3;
	else if (sc->mac_ver == 0x3593 && sc->ntxchains == 3)
		bbp1 = (bbp1 & ~(1 << 3)) | 1 << 4;
	rt2860_mcu_bbp_write(sc, 1, bbp1);

	if (sc->mac_ver >= 0x3071)
		rt3090_rf_setup(sc);

	/* select default channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	rt2860_switch_chan(sc, ic->ic_ibss_chan);

	/* reset RF from MCU */
	rt2860_mcu_cmd(sc, RT2860_MCU_CMD_RFRESET, 0, 0);

	/* set RTS threshold */
	tmp = RAL_READ(sc, RT2860_TX_RTS_CFG);
	tmp &= ~0xffff00;
	tmp |= ic->ic_rtsthreshold << 8;
	RAL_WRITE(sc, RT2860_TX_RTS_CFG, tmp);

	/* setup initial protection mode */
	sc->sc_ic_flags = ic->ic_flags;
	rt2860_updateprot(ic);

	/* turn radio LED on */
	rt2860_set_leds(sc, RT2860_LED_RADIO);

	/* enable Tx/Rx DMA engine */
	if ((error = rt2860_txrx_enable(sc)) != 0) {
		rt2860_stop(ifp, 1);
		return error;
	}

	/* clear pending interrupts */
	RAL_WRITE(sc, RT2860_INT_STATUS, 0xffffffff);
	/* enable interrupts */
	RAL_WRITE(sc, RT2860_INT_MASK, 0x3fffc);

	if (sc->sc_flags & RT2860_ADVANCED_PS)
		rt2860_mcu_cmd(sc, RT2860_MCU_CMD_PSLEVEL, sc->pslevel, 0);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (ic->ic_flags & IEEE80211_F_WEPON) {
		/* install WEP keys */
		for (i = 0; i < IEEE80211_WEP_NKID; i++)
			(void)rt2860_set_key(ic, NULL, &ic->ic_nw_keys[i]);
	}

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);

	return 0;
}

void
rt2860_stop(struct ifnet *ifp, int disable)
{
	struct rt2860_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	int qid;

	if (ifp->if_flags & IFF_RUNNING)
		rt2860_set_leds(sc, 0);	/* turn all LEDs off */

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);	/* free all nodes */

	/* disable interrupts */
	RAL_WRITE(sc, RT2860_INT_MASK, 0);

	/* disable GP timer */
	rt2860_set_gp_timer(sc, 0);

	/* disable Rx */
	tmp = RAL_READ(sc, RT2860_MAC_SYS_CTRL);
	tmp &= ~(RT2860_MAC_RX_EN | RT2860_MAC_TX_EN);
	RAL_WRITE(sc, RT2860_MAC_SYS_CTRL, tmp);

	/* reset adapter */
	RAL_WRITE(sc, RT2860_MAC_SYS_CTRL, RT2860_BBP_HRST | RT2860_MAC_SRST);
	RAL_BARRIER_WRITE(sc);
	RAL_WRITE(sc, RT2860_MAC_SYS_CTRL, 0);

	/* reset Tx and Rx rings (and reclaim TXWIs) */
	sc->qfullmsk = 0;
	for (qid = 0; qid < 6; qid++)
		rt2860_reset_tx_ring(sc, &sc->txq[qid]);
	rt2860_reset_rx_ring(sc, &sc->rxq);

	/* for CardBus, power down the socket */
	if (disable && sc->sc_disable != NULL) {
		if (sc->sc_flags & RT2860_ENABLED) {
			(*sc->sc_disable)(sc);
			sc->sc_flags &= ~RT2860_ENABLED;
		}
	}
}

int
rt2860_load_microcode(struct rt2860_softc *sc)
{
	int ntries;

	/* set "host program ram write selection" bit */
	RAL_WRITE(sc, RT2860_SYS_CTRL, RT2860_HST_PM_SEL);
	/* write microcode image */
	RAL_WRITE_REGION_1(sc, RT2860_FW_BASE, sc->ucode, sc->ucsize);
	/* kick microcontroller unit */
	RAL_WRITE(sc, RT2860_SYS_CTRL, 0);
	RAL_BARRIER_WRITE(sc);
	RAL_WRITE(sc, RT2860_SYS_CTRL, RT2860_MCU_RESET);

	RAL_WRITE(sc, RT2860_H2M_BBPAGENT, 0);
	RAL_WRITE(sc, RT2860_H2M_MAILBOX, 0);

	/* wait until microcontroller is ready */
	RAL_BARRIER_READ_WRITE(sc);
	for (ntries = 0; ntries < 1000; ntries++) {
		if (RAL_READ(sc, RT2860_SYS_CTRL) & RT2860_MCU_READY)
			break;
		DELAY(1000);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for MCU to initialize\n",
		    sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}
	return 0;
}

/*
 * This function is called periodically to adjust Tx power based on
 * temperature variation.
 */
void
rt2860_calib(struct rt2860_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const uint8_t *tssi;
	uint8_t step, bbp49;
	int8_t ridx, d;

	/* read current temperature */
	bbp49 = rt2860_mcu_bbp_read(sc, 49);

	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_bss->ni_chan)) {
		tssi = &sc->tssi_2ghz[4];
		step = sc->step_2ghz;
	} else {
		tssi = &sc->tssi_5ghz[4];
		step = sc->step_5ghz;
	}

	if (bbp49 < tssi[0]) {		/* lower than reference */
		/* use higher Tx power than default */
		for (d = 0; d > -4 && bbp49 <= tssi[d - 1]; d--)
			;
	} else if (bbp49 > tssi[0]) {	/* greater than reference */
		/* use lower Tx power than default */
		for (d = 0; d < +4 && bbp49 >= tssi[d + 1]; d++)
			;
	} else {
		/* use default Tx power */
		d = 0;
	}
	d *= step;

	DPRINTF(("BBP49=0x%02x, adjusting Tx power by %d\n", bbp49, d));

	/* write adjusted Tx power values for each Tx rate */
	for (ridx = 0; ridx < 5; ridx++) {
		if (sc->txpow20mhz[ridx] == 0xffffffff)
			continue;
		RAL_WRITE(sc, RT2860_TX_PWR_CFG(ridx),
		    b4inc(sc->txpow20mhz[ridx], d));
	}
}

void
rt3090_set_rx_antenna(struct rt2860_softc *sc, int aux)
{
	uint32_t tmp;
	if (aux) {
		if (sc->mac_ver == 0x5390) {
			rt2860_mcu_bbp_write(sc, 152,
			    rt2860_mcu_bbp_read(sc, 152) & ~0x80);
		} else {
			tmp = RAL_READ(sc, RT2860_PCI_EECTRL);
			RAL_WRITE(sc, RT2860_PCI_EECTRL,
			    tmp & ~RT2860_C);
			tmp = RAL_READ(sc, RT2860_GPIO_CTRL);
			RAL_WRITE(sc, RT2860_GPIO_CTRL,
			    (tmp & ~0x0808) | 0x08);
		}
	} else {
		if (sc->mac_ver == 0x5390) {
			rt2860_mcu_bbp_write(sc, 152,
			    rt2860_mcu_bbp_read(sc, 152) | 0x80);
		} else {
			tmp = RAL_READ(sc, RT2860_PCI_EECTRL);
			RAL_WRITE(sc, RT2860_PCI_EECTRL,
			    tmp | RT2860_C);
			tmp = RAL_READ(sc, RT2860_GPIO_CTRL);
			RAL_WRITE(sc, RT2860_GPIO_CTRL,
			    tmp & ~0x0808);
		}
	}
}

void
rt2860_switch_chan(struct rt2860_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	u_int chan, group;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return;

	if (sc->mac_ver == 0x3290 ||
	    sc->mac_ver >= 0x5390)
		rt5390_set_chan(sc, chan);
	else if (sc->mac_ver >= 0x3071)
		rt3090_set_chan(sc, chan);
	else
		rt2860_set_chan(sc, chan);

	/* determine channel group */
	if (chan <= 14)
		group = 0;
	else if (chan <= 64)
		group = 1;
	else if (chan <= 128)
		group = 2;
	else
		group = 3;

	/* XXX necessary only when group has changed! */
	if (sc->mac_ver <= 0x5390)
		rt2860_select_chan_group(sc, group);

	DELAY(1000);
}

#ifndef IEEE80211_STA_ONLY
int
rt2860_setup_beacon(struct rt2860_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rt2860_txwi txwi;
	struct mbuf *m;
	int ridx;

	if ((m = ieee80211_beacon_alloc(ic, ic->ic_bss)) == NULL)
		return ENOBUFS;

	memset(&txwi, 0, sizeof txwi);
	txwi.wcid = 0xff;
	txwi.len = htole16(m->m_pkthdr.len);
	/* send beacons at the lowest available rate */
	ridx = (ic->ic_curmode == IEEE80211_MODE_11A) ?
	    RT2860_RIDX_OFDM6 : RT2860_RIDX_CCK1;
	txwi.phy = htole16(rt2860_rates[ridx].mcs);
	if (rt2860_rates[ridx].phy == IEEE80211_T_OFDM)
		txwi.phy |= htole16(RT2860_PHY_OFDM);
	txwi.txop = RT2860_TX_TXOP_HT;
	txwi.flags = RT2860_TX_TS;
	txwi.xflags = RT2860_TX_NSEQ;

	RAL_WRITE_REGION_1(sc, RT2860_BCN_BASE(0),
	    (uint8_t *)&txwi, sizeof txwi);
	RAL_WRITE_REGION_1(sc, RT2860_BCN_BASE(0) + sizeof txwi,
	    mtod(m, uint8_t *), m->m_pkthdr.len);

	m_freem(m);

	return 0;
}
#endif

void
rt2860_enable_tsf_sync(struct rt2860_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	tmp = RAL_READ(sc, RT2860_BCN_TIME_CFG);

	tmp &= ~0x1fffff;
	tmp |= ic->ic_bss->ni_intval * 16;
	tmp |= RT2860_TSF_TIMER_EN | RT2860_TBTT_TIMER_EN;
	if (ic->ic_opmode == IEEE80211_M_STA) {
		/*
		 * Local TSF is always updated with remote TSF on beacon
		 * reception.
		 */
		tmp |= 1 << RT2860_TSF_SYNC_MODE_SHIFT;
	}
#ifndef IEEE80211_STA_ONLY
	else if (ic->ic_opmode == IEEE80211_M_IBSS) {
		tmp |= RT2860_BCN_TX_EN;
		/*
		 * Local TSF is updated with remote TSF on beacon reception
		 * only if the remote TSF is greater than local TSF.
		 */
		tmp |= 2 << RT2860_TSF_SYNC_MODE_SHIFT;
	} else if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
		tmp |= RT2860_BCN_TX_EN;
		/* SYNC with nobody */
		tmp |= 3 << RT2860_TSF_SYNC_MODE_SHIFT;
	}
#endif

	RAL_WRITE(sc, RT2860_BCN_TIME_CFG, tmp);
}

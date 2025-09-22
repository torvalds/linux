/*	$OpenBSD: if_mtw.c,v 1.12 2025/03/28 23:17:00 hastings Exp $	*/
/*
 * Copyright (c) 2008-2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2013-2014 Kevin Lo
 * Copyright (c) 2021 James Hastings
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

/*
 * MediaTek MT7601U 802.11b/g/n WLAN.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/device.h>
#include <sys/endian.h>

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
#include <net80211/ieee80211_ra.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/ic/mtwreg.h>
#include <dev/usb/if_mtwvar.h>

#ifdef MTW_DEBUG
#define DPRINTF(x)	do { if (mtw_debug) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (mtw_debug >= (n)) printf x; } while (0)
int mtw_debug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

#define USB_ID(v, p)	{ USB_VENDOR_##v, USB_PRODUCT_##v##_##p }
static const struct usb_devno mtw_devs[] = {
	USB_ID(ASUS,		USBN10V2),
	USB_ID(AZUREWAVE,	MT7601_1),
	USB_ID(AZUREWAVE,	MT7601_2),
	USB_ID(DLINK,		DWA127B1),
	USB_ID(EDIMAX,		EW7711UANV2),
	USB_ID(MEDIATEK,	MT7601_1),
	USB_ID(MEDIATEK,	MT7601_2),
	USB_ID(RALINK,		MT7601),
	USB_ID(RALINK,		MT7601_2),
	USB_ID(RALINK,		MT7601_3),
	USB_ID(RALINK,		MT7601_4),
	USB_ID(RALINK,		MT7601_5),
	USB_ID(XIAOMI,		MT7601U),
};

int		mtw_match(struct device *, void *, void *);
void		mtw_attach(struct device *, struct device *, void *);
int		mtw_detach(struct device *, int);
void		mtw_attachhook(struct device *);
int		mtw_open_pipes(struct mtw_softc *);
void		mtw_close_pipes(struct mtw_softc *);
int		mtw_alloc_rx_ring(struct mtw_softc *, int);
void		mtw_free_rx_ring(struct mtw_softc *, int);
int		mtw_alloc_tx_ring(struct mtw_softc *, int);
void		mtw_free_tx_ring(struct mtw_softc *, int);
int		mtw_alloc_mcu_ring(struct mtw_softc *);
void		mtw_free_mcu_ring(struct mtw_softc *);
int		mtw_ucode_write(struct mtw_softc *, const uint8_t *,
		    uint32_t, uint32_t);
void		mtw_ucode_setup(struct mtw_softc *);
int		mtw_load_microcode(struct mtw_softc *);
int		mtw_reset(struct mtw_softc *);
int		mtw_read(struct mtw_softc *, uint16_t, uint32_t *);
int		mtw_read_cfg(struct mtw_softc *, uint16_t, uint32_t *);
int		mtw_read_region_1(struct mtw_softc *, uint16_t,
		    uint8_t *, int);
int		mtw_write_2(struct mtw_softc *, uint16_t, uint16_t);
int		mtw_write(struct mtw_softc *, uint16_t, uint32_t);
int		mtw_write_cfg(struct mtw_softc *, uint16_t, uint32_t);
int		mtw_write_ivb(struct mtw_softc *, const uint8_t *, uint16_t);
int		mtw_write_region_1(struct mtw_softc *, uint16_t,
		    uint8_t *, int);
int		mtw_set_region_4(struct mtw_softc *, uint16_t, uint32_t, int);
int		mtw_efuse_read_2(struct mtw_softc *, uint16_t, uint16_t *);
int		mtw_eeprom_read_2(struct mtw_softc *, uint16_t, uint16_t *);
int		mtw_rf_read(struct mtw_softc *, uint8_t, uint8_t, uint8_t *);
int		mtw_rf_write(struct mtw_softc *, uint8_t, uint8_t, uint8_t);
int		mtw_bbp_read(struct mtw_softc *, uint8_t, uint8_t *);
int		mtw_bbp_write(struct mtw_softc *, uint8_t, uint8_t);
int		mtw_usb_dma_read(struct mtw_softc *, uint32_t *);
int		mtw_usb_dma_write(struct mtw_softc *, uint32_t);
int		mtw_mcu_calibrate(struct mtw_softc *, int, uint32_t);
int		mtw_mcu_channel(struct mtw_softc *, uint32_t, uint32_t, uint32_t);
int		mtw_mcu_radio(struct mtw_softc *, int, uint32_t);
int		mtw_mcu_cmd(struct mtw_softc *, int, void *, int);
const char *	mtw_get_rf(int);
void		mtw_get_txpower(struct mtw_softc *);
int		mtw_read_eeprom(struct mtw_softc *);
struct		ieee80211_node *mtw_node_alloc(struct ieee80211com *);
int		mtw_media_change(struct ifnet *);
void		mtw_next_scan(void *);
void		mtw_task(void *);
void		mtw_do_async(struct mtw_softc *, void (*)(struct mtw_softc *,
		    void *), void *, int);
int		mtw_newstate(struct ieee80211com *, enum ieee80211_state, int);
void		mtw_newstate_cb(struct mtw_softc *, void *);
void		mtw_updateedca(struct ieee80211com *);
void		mtw_updateedca_cb(struct mtw_softc *, void *);
void		mtw_updateslot(struct ieee80211com *);
void		mtw_updateslot_cb(struct mtw_softc *, void *);
int		mtw_set_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		mtw_set_key_cb(struct mtw_softc *, void *);
void		mtw_delete_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		mtw_delete_key_cb(struct mtw_softc *, void *);
void		mtw_calibrate_to(void *);
void		mtw_calibrate_cb(struct mtw_softc *, void *);
void		mtw_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);
void		mtw_rx_frame(struct mtw_softc *, uint8_t *, int,
		    struct mbuf_list *);
void		mtw_rxeof(struct usbd_xfer *, void *, usbd_status);
void		mtw_txeof(struct usbd_xfer *, void *, usbd_status);
int		mtw_tx(struct mtw_softc *, struct mbuf *,
		    struct ieee80211_node *);
void		mtw_start(struct ifnet *);
void		mtw_watchdog(struct ifnet *);
int		mtw_ioctl(struct ifnet *, u_long, caddr_t);
void		mtw_select_chan_group(struct mtw_softc *, int);
void		mt7601_set_agc(struct mtw_softc *, uint8_t);
void		mt7601_set_chan(struct mtw_softc *, u_int);
int		mtw_set_chan(struct mtw_softc *, struct ieee80211_channel *);
void		mtw_enable_tsf_sync(struct mtw_softc *);
void		mtw_abort_tsf_sync(struct mtw_softc *);
void		mtw_enable_mrr(struct mtw_softc *);
void		mtw_set_txrts(struct mtw_softc *);
void		mtw_set_txpreamble(struct mtw_softc *);
void		mtw_set_basicrates(struct mtw_softc *);
void		mtw_set_leds(struct mtw_softc *, uint16_t);
void		mtw_set_bssid(struct mtw_softc *, const uint8_t *);
void		mtw_set_macaddr(struct mtw_softc *, const uint8_t *);
#if NBPFILTER > 0
int8_t		mtw_rssi2dbm(struct mtw_softc *, uint8_t, uint8_t);
#endif
int		mt7601_bbp_init(struct mtw_softc *);
int		mt7601_rf_init(struct mtw_softc *);
int		mt7601_rf_setup(struct mtw_softc *);
int		mt7601_rf_temperature(struct mtw_softc *, int8_t *);
int		mt7601_r49_read(struct mtw_softc *, uint8_t, int8_t *);
int		mt7601_rxdc_cal(struct mtw_softc *);
int		mtw_wlan_enable(struct mtw_softc *, int);
int		mtw_txrx_enable(struct mtw_softc *);
int		mtw_init(struct ifnet *);
void		mtw_stop(struct ifnet *, int);

struct cfdriver mtw_cd = {
	NULL, "mtw", DV_IFNET
};

const struct cfattach mtw_ca = {
	sizeof (struct mtw_softc), mtw_match, mtw_attach, mtw_detach
};

static const struct {
	uint32_t	reg;
	uint32_t	val;
} mt7601_def_mac[] = {
	MT7601_DEF_MAC
};

static const struct {
	uint8_t		reg;
	uint8_t		val;
} mt7601_def_bbp[] = {
	MT7601_DEF_BBP
};

static const struct {
	u_int		chan;
	uint8_t		r17, r18, r19, r20;
} mt7601_rf_chan[] = {
	MT7601_RF_CHAN
};

static const struct {
	uint8_t		reg;
	uint8_t		val;
} mt7601_rf_bank0[] = {
	MT7601_BANK0_RF
},mt7601_rf_bank4[] = {
	MT7601_BANK4_RF
},mt7601_rf_bank5[] = {
	MT7601_BANK5_RF
};

int
mtw_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return UMATCH_NONE;

	return (usb_lookup(mtw_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE;
}

void
mtw_attach(struct device *parent, struct device *self, void *aux)
{
	struct mtw_softc *sc = (struct mtw_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i, error, nrx, ntx, ntries;
	uint32_t ver;

	sc->sc_udev = uaa->device;
	sc->sc_iface = uaa->iface;

	/*
	 * Find all bulk endpoints.
	 */
	nrx = ntx = 0;
	id = usbd_get_interface_descriptor(sc->sc_iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL || UE_GET_XFERTYPE(ed->bmAttributes) != UE_BULK)
			continue;

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN) {
			sc->rxq[nrx].pipe_no = ed->bEndpointAddress;
			nrx++;
		} else if (ntx < 6) {
			if (ntx == 0)
				sc->txq[MTW_TXQ_MCU].pipe_no =
				    ed->bEndpointAddress;
			else
				sc->txq[ntx - 1].pipe_no =
				    ed->bEndpointAddress;
			ntx++;
		}
	}
	/* make sure we've got them all */
	if (nrx < 2 || ntx < 6) {
		printf("%s: missing endpoint\n", sc->sc_dev.dv_xname);
		return;
	}

	/* wait for the chip to settle */
	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_ASIC_VER, &ver)) != 0)
			return;
		if (ver != 0 && ver != 0xffffffff)
			break;
		DPRINTF(("%08x ", ver));
		DELAY(10);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for NIC to initialize\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	sc->asic_ver = ver >> 16;
	sc->asic_rev = ver & 0xffff;

	usb_init_task(&sc->sc_task, mtw_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->scan_to, mtw_next_scan, sc);
	timeout_set(&sc->calib_to, mtw_calibrate_to, sc);

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 10;

	config_mountroot(self, mtw_attachhook);
}

int
mtw_detach(struct device *self, int flags)
{
	struct mtw_softc *sc = (struct mtw_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int qid, s;

	s = splusb();

	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
	if (timeout_initialized(&sc->calib_to))
		timeout_del(&sc->calib_to);

	/* wait for all queued asynchronous commands to complete */
	usb_rem_wait_task(sc->sc_udev, &sc->sc_task);

	usbd_ref_wait(sc->sc_udev);

	if (ifp->if_softc != NULL) {
		ifp->if_flags &= ~IFF_RUNNING;
		ifq_clr_oactive(&ifp->if_snd);
		ieee80211_ifdetach(ifp);
		if_detach(ifp);
	}

	/* free rings and close pipes */
	mtw_free_mcu_ring(sc);
	for (qid = 0; qid < MTW_TXQ_COUNT; qid++)
		mtw_free_tx_ring(sc, qid);
	mtw_free_rx_ring(sc, 0);
	mtw_free_rx_ring(sc, 1);
	mtw_close_pipes(sc);

	splx(s);
	return 0;
}

void
mtw_attachhook(struct device *self)
{
	struct mtw_softc *sc = (struct mtw_softc *)self;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	uint32_t tmp;
	int ntries, error, i;

	if (usbd_is_dying(sc->sc_udev))
		return;

	/* open all bulk pipes */
	if ((error = mtw_open_pipes(sc)) != 0) {
		printf("%s: could not open pipes\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/* enable WLAN core */
	if ((error = mtw_wlan_enable(sc, 1)) != 0) {
		printf("%s: could not enable WLAN core\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/* load firmware */
	if ((error = mtw_load_microcode(sc)) != 0) {
		printf("%s: could not load microcode\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/* init MCU Tx ring */
	if ((error = mtw_alloc_mcu_ring(sc)) != 0)
		goto fail;

	/* init MCU Rx ring */
	if ((error = mtw_alloc_rx_ring(sc, 1)) != 0)
		goto fail;

	mtw_usb_dma_read(sc, &tmp);
	mtw_usb_dma_write(sc, tmp | (MTW_USB_RX_EN | MTW_USB_TX_EN));

	/* read MAC version */
	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_MAC_VER_ID, &tmp)) != 0)
			goto fail;
		if (tmp != 0 && tmp != 0xffffffff)
			break;
		DELAY(10);
	}
	if (ntries == 100) {
		printf("%s: failed reading MAC\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	sc->mac_ver = tmp >> 16;
	sc->mac_rev = tmp & 0xffff;
	
	/* retrieve RF rev. no and various other things from EEPROM */
	mtw_read_eeprom(sc);

	printf("%s: MAC/BBP MT%04X (rev 0x%04X), RF %s (MIMO %dT%dR), "
	    "address %s\n", sc->sc_dev.dv_xname, sc->mac_ver,
	    sc->mac_rev, mtw_get_rf(sc->rf_rev), sc->ntxchains,
	    sc->nrxchains, ether_sprintf(ic->ic_myaddr));

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WEP |		/* WEP */
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
	ifp->if_ioctl = mtw_ioctl;
	ifp->if_start = mtw_start;
	ifp->if_watchdog = mtw_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_node_alloc = mtw_node_alloc;
	ic->ic_newassoc = mtw_newassoc;
	ic->ic_updateslot = mtw_updateslot;
	ic->ic_updateedca = mtw_updateedca;
	ic->ic_set_key = mtw_set_key;
	ic->ic_delete_key = mtw_delete_key;

	/* override 802.11 state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = mtw_newstate;
	ieee80211_media_init(ifp, mtw_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(MTW_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(MTW_TX_RADIOTAP_PRESENT);
#endif
fail:
	return;
}

int
mtw_open_pipes(struct mtw_softc *sc)
{
	struct mtw_tx_ring *txq;
	struct mtw_rx_ring *rxq;
	int i, error;

	for (i = 0; i < MTW_TXQ_COUNT; i++) {
		txq = &sc->txq[i];

		if ((error = usbd_open_pipe(sc->sc_iface, txq->pipe_no, 0,
		    &txq->pipeh)) != 0)
			return error;
	}

	for (i = 0; i < MTW_RXQ_COUNT; i++) {
		rxq = &sc->rxq[i];

		if ((error = usbd_open_pipe(sc->sc_iface, rxq->pipe_no, 0,
		    &rxq->pipeh)) != 0)
			return error;
	}
	return 0;
}

void
mtw_close_pipes(struct mtw_softc *sc)
{
	struct mtw_tx_ring *txq;
	struct mtw_rx_ring *rxq;
	int i;

	for (i = 0; i < MTW_TXQ_COUNT; i++) {
		txq = &sc->txq[i];

		usbd_close_pipe(txq->pipeh);
		txq->pipeh = NULL;
	}

	for (i = 0; i < MTW_RXQ_COUNT; i++) {
		rxq = &sc->rxq[i];

		usbd_close_pipe(rxq->pipeh);
		rxq->pipeh = NULL;
	}
}

int
mtw_alloc_rx_ring(struct mtw_softc *sc, int qid)
{
	struct mtw_rx_ring *rxq = &sc->rxq[qid];
	int i, error = 0;

	for (i = 0; i < MTW_RX_RING_COUNT; i++) {
		struct mtw_rx_data *data = &rxq->data[i];

		data->sc = sc;	/* backpointer for callbacks */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			error = ENOMEM;
			goto fail;
		}
		data->buf = usbd_alloc_buffer(data->xfer, MTW_MAX_RXSZ);
		if (data->buf == NULL) {
			error = ENOMEM;
			goto fail;
		}
	}
	if (error != 0)
fail:		mtw_free_rx_ring(sc, 0);
	return error;
}

void
mtw_free_rx_ring(struct mtw_softc *sc, int qid)
{
	struct mtw_rx_ring *rxq = &sc->rxq[qid];
	int i;

	usbd_abort_pipe(rxq->pipeh);

	for (i = 0; i < MTW_RX_RING_COUNT; i++) {
		if (rxq->data[i].xfer != NULL)
			usbd_free_xfer(rxq->data[i].xfer);
		rxq->data[i].xfer = NULL;
	}
}

int
mtw_alloc_tx_ring(struct mtw_softc *sc, int qid)
{
	struct mtw_tx_ring *txq = &sc->txq[qid];
	int i, error = 0;
	uint16_t txwisize;

	txwisize = sizeof(struct mtw_txwi);

	txq->cur = txq->queued = 0;

	for (i = 0; i < MTW_TX_RING_COUNT; i++) {
		struct mtw_tx_data *data = &txq->data[i];

		data->sc = sc;	/* backpointer for callbacks */
		data->qid = qid;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			error = ENOMEM;
			goto fail;
		}

		data->buf = usbd_alloc_buffer(data->xfer, MTW_MAX_TXSZ);
		if (data->buf == NULL) {
			error = ENOMEM;
			goto fail;
		}

		/* zeroize the TXD + TXWI part */
		memset(data->buf, 0, MTW_MAX_TXSZ);
	}
	if (error != 0)
fail:		mtw_free_tx_ring(sc, qid);
	return error;
}

void
mtw_free_tx_ring(struct mtw_softc *sc, int qid)
{
	struct mtw_tx_ring *txq = &sc->txq[qid];
	int i;

	usbd_abort_pipe(txq->pipeh);

	for (i = 0; i < MTW_TX_RING_COUNT; i++) {
		if (txq->data[i].xfer != NULL)
			usbd_free_xfer(txq->data[i].xfer);
		txq->data[i].xfer = NULL;
	}
}

int
mtw_alloc_mcu_ring(struct mtw_softc *sc)
{
	struct mtw_tx_ring *ring = &sc->sc_mcu;
	struct mtw_tx_data *data = &ring->data[0];
	int error = 0;
	
	ring->cur = ring->queued = 0;

	data->sc = sc;	/* backpointer for callbacks */
	data->qid = 5;

	data->xfer = usbd_alloc_xfer(sc->sc_udev);
	if (data->xfer == NULL) {
		error = ENOMEM;
		goto fail;
	}

	data->buf = usbd_alloc_buffer(data->xfer, MTW_MAX_TXSZ);
	if (data->buf == NULL) {
		error = ENOMEM;
		goto fail;
	}
	/* zeroize the TXD */
	memset(data->buf, 0, 4);

	if (error != 0)
fail:		mtw_free_mcu_ring(sc);
	return error;


}
void
mtw_free_mcu_ring(struct mtw_softc *sc)
{
	struct mtw_tx_ring *txq = &sc->sc_mcu;

	if (txq->data[0].xfer != NULL)
		usbd_free_xfer(txq->data[0].xfer);
	txq->data[0].xfer = NULL;
}

int
mtw_ucode_write(struct mtw_softc *sc, const uint8_t *fw, uint32_t len,
    uint32_t offset)
{
	struct mtw_tx_ring *ring = &sc->txq[MTW_TXQ_MCU];
	struct usbd_xfer *xfer;
	struct mtw_txd *txd;
	uint8_t *buf;
	uint32_t blksz, sent, tmp, xferlen;
	int error;

	blksz = 0x2000;
	if (sc->asic_ver == 0x7612 && offset >= 0x90000)
		blksz = 0x800; /* MT7612 ROM Patch */

	xfer = usbd_alloc_xfer(sc->sc_udev);
	if (xfer == NULL) {
		error = ENOMEM;
		goto fail;
	}
	buf = usbd_alloc_buffer(xfer, blksz + 12);
	if (buf == NULL) {
		error = ENOMEM;
		goto fail;
	}

	sent = 0;
	for (;;) {
		xferlen = min(len - sent, blksz);
		if (xferlen == 0)
			break;

		txd = (struct mtw_txd *)buf;
		txd->len = htole16(xferlen);
		txd->flags = htole16(MTW_TXD_DATA | MTW_TXD_MCU);

		memcpy(buf + sizeof(struct mtw_txd), fw + sent, xferlen);
		memset(buf + sizeof(struct mtw_txd) + xferlen, 0, MTW_DMA_PAD);
		mtw_write_cfg(sc, MTW_MCU_DMA_ADDR, offset + sent);
		mtw_write_cfg(sc, MTW_MCU_DMA_LEN, (xferlen << 16));

		usbd_setup_xfer(xfer, ring->pipeh, NULL, buf,
		    xferlen + sizeof(struct mtw_txd) + MTW_DMA_PAD,
		    USBD_SHORT_XFER_OK | USBD_SYNCHRONOUS | USBD_NO_COPY,
		    MTW_TX_TIMEOUT, NULL);
		if ((error = usbd_transfer(xfer)) != 0)
			break;

		mtw_read(sc, MTW_MCU_FW_IDX, &tmp);
		mtw_write(sc, MTW_MCU_FW_IDX, tmp++);

		sent += xferlen;
	}
fail:
	if (xfer != NULL) {
		usbd_free_xfer(xfer);
		xfer = NULL;
	}
	return error;
}

void
mtw_ucode_setup(struct mtw_softc *sc)
{
	mtw_usb_dma_write(sc, (MTW_USB_TX_EN | MTW_USB_RX_EN));
	mtw_write(sc, MTW_FCE_PSE_CTRL, 1);
	mtw_write(sc, MTW_TX_CPU_FCE_BASE, 0x400230);
	mtw_write(sc, MTW_TX_CPU_FCE_MAX_COUNT, 1);
	mtw_write(sc, MTW_MCU_FW_IDX, 1);
	mtw_write(sc, MTW_FCE_PDMA, 0x44);
	mtw_write(sc, MTW_FCE_SKIP_FS, 3);
}

int
mtw_load_microcode(struct mtw_softc *sc)
{
	const struct mtw_ucode_hdr *hdr;
	const struct mtw_ucode *fw;
	const char *fwname;
	u_char *ucode;
	size_t size;
	uint32_t tmp, iofs, dofs;
	int ntries, error;
	int dlen, ilen;

	/* is firmware already running? */
	mtw_read_cfg(sc, MTW_MCU_DMA_ADDR, &tmp);
	if (tmp == MTW_MCU_READY)
		return 0;

	if (sc->asic_ver == 0x7612) {
		fwname = "mtw-mt7662u_rom_patch";

		if ((error = loadfirmware(fwname, &ucode, &size)) != 0) {
			printf("%s: failed loadfirmware of file %s (error %d)\n",
			    sc->sc_dev.dv_xname, fwname, error);
			return error;
		}
		fw = (const struct mtw_ucode *) ucode + 0x1e;
		ilen = size - 0x1e;

		mtw_ucode_setup(sc);

		if ((error = mtw_ucode_write(sc, fw->data, ilen, 0x90000)) != 0)
			goto fail;

		mtw_usb_dma_write(sc, 0x00e41814);
		free(ucode, M_DEVBUF, size);
	}

	fwname = "mtw-mt7601u";
	iofs = 0x40;
	dofs = 0;
	if (sc->asic_ver == 0x7612) {
		fwname = "mtw-mt7662u";
		iofs = 0x80040;
		dofs = 0x110800;
	} else if (sc->asic_ver == 0x7610) {
		fwname = "mtw-mt7610u";
		dofs = 0x80000;
	}

	if ((error = loadfirmware(fwname, &ucode, &size)) != 0) {
		printf("%s: failed loadfirmware of file %s (error %d)\n",
		    sc->sc_dev.dv_xname, fwname, error);
		return error;
	}

	if (size < sizeof(struct mtw_ucode_hdr)) {
		printf("%s: firmware header too short\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	fw = (const struct mtw_ucode *) ucode;
	hdr = (const struct mtw_ucode_hdr *) &fw->hdr;

	if (size < sizeof(struct mtw_ucode_hdr) + letoh32(hdr->ilm_len) +
	    letoh32(hdr->dlm_len)) {
		printf("%s: firmware payload too short\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ilen = le32toh(hdr->ilm_len) - MTW_MCU_IVB_LEN;
	dlen = le32toh(hdr->dlm_len);

	if (ilen > size || dlen > size) {
		printf("%s: firmware payload too large\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	mtw_write(sc, MTW_FCE_PDMA, 0);
	mtw_write(sc, MTW_FCE_PSE_CTRL, 0);
	mtw_ucode_setup(sc);

	if ((error = mtw_ucode_write(sc, fw->data, ilen, iofs)) != 0)
		goto fail;
	if (dlen > 0 && dofs > 0) {
		if ((error = mtw_ucode_write(sc, fw->data + ilen,
		    dlen, dofs)) != 0)
			goto fail;
	}

	/* write interrupt vectors */
	if (sc->asic_ver == 0x7612) {
		/* MT7612 */
		if ((error = mtw_ucode_write(sc, fw->ivb,
		    MTW_MCU_IVB_LEN, 0x80000)) != 0)
			goto fail;
		mtw_write_cfg(sc, MTW_MCU_DMA_ADDR, 0x00095000);
		mtw_write_ivb(sc, NULL, 0);
	} else {
		/* MT7601/MT7610 */
		if ((error = mtw_write_ivb(sc, fw->ivb,
		    MTW_MCU_IVB_LEN)) != 0)
			goto fail;
	}

	/* wait until microcontroller is ready */
	usbd_delay_ms(sc->sc_udev, 10);

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read_cfg(sc, MTW_MCU_DMA_ADDR, &tmp)) != 0)
			return error;
		if (tmp & MTW_MCU_READY)
			break;
		usbd_delay_ms(sc->sc_udev, 100);
	}

	if (ntries == 100) {
		printf("%s: timeout waiting for MCU to initialize\n",
		    sc->sc_dev.dv_xname);
		error = ETIMEDOUT;
	}

	DPRINTF(("%s: loaded firmware ver %d.%d\n", sc->sc_dev.dv_xname,
	    le16toh(hdr->build_ver), le16toh(hdr->fw_ver)));
fail:
	free(ucode, M_DEVBUF, size);
	return error;
}

int
mtw_reset(struct mtw_softc *sc)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MTW_RESET;
	USETW(req.wValue, 1);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return usbd_do_request(sc->sc_udev, &req, NULL);
}

int
mtw_read(struct mtw_softc *sc, uint16_t reg, uint32_t *val)
{
	uint32_t tmp;
	int error;

	error = mtw_read_region_1(sc, reg,
	    (uint8_t *)&tmp, sizeof tmp);
	if (error == 0)
		*val = letoh32(tmp);
	else
		*val = 0xffffffff;
	return error;
}

int
mtw_read_cfg(struct mtw_softc *sc, uint16_t reg, uint32_t *val)
{
	usb_device_request_t req;
	uint32_t tmp;
	int error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MTW_READ_CFG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 4);
	error = usbd_do_request(sc->sc_udev, &req, &tmp);

	if (error == 0)
		*val = letoh32(tmp);
	else
		*val = 0xffffffff;
	return error;
}

int
mtw_read_region_1(struct mtw_softc *sc, uint16_t reg,
    uint8_t *buf, int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MTW_READ_REGION_1;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);
	return usbd_do_request(sc->sc_udev, &req, buf);
}

int
mtw_write_2(struct mtw_softc *sc, uint16_t reg, uint16_t val)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MTW_WRITE_2;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);
	return usbd_do_request(sc->sc_udev, &req, NULL);
}

int
mtw_write(struct mtw_softc *sc, uint16_t reg, uint32_t val)
{
	int error;

	if ((error = mtw_write_2(sc, reg, val & 0xffff)) == 0)
		error = mtw_write_2(sc, reg + 2, val >> 16);
	return error;
}

int
mtw_write_cfg(struct mtw_softc *sc, uint16_t reg, uint32_t val)
{
	usb_device_request_t req;
	int error;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MTW_WRITE_CFG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 4);
	val = htole32(val);
	error = usbd_do_request(sc->sc_udev, &req, &val);
	return error;
}

int
mtw_write_ivb(struct mtw_softc *sc, const uint8_t *buf, uint16_t len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MTW_RESET;
	USETW(req.wValue, 0x12);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	return usbd_do_request(sc->sc_udev, &req, (void *)buf);
}

int
mtw_write_region_1(struct mtw_softc *sc, uint16_t reg,
    uint8_t *buf, int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MTW_WRITE_REGION_1;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);
	return usbd_do_request(sc->sc_udev, &req, buf);
}

int
mtw_set_region_4(struct mtw_softc *sc, uint16_t reg, uint32_t val, int count)
{
	int error = 0;

	for (; count > 0 && error == 0; count--, reg += 4)
		error = mtw_write(sc, reg, val);
	return error;
}

/* Read 16-bit from eFUSE ROM. */
int
mtw_efuse_read_2(struct mtw_softc *sc, uint16_t addr, uint16_t *val)
{
	uint32_t tmp;
	uint16_t reg;
	int error, ntries;

	if ((error = mtw_read(sc, MTW_EFUSE_CTRL, &tmp)) != 0)
		return error;

	addr *= 2;
	/*
	 * Read one 16-byte block into registers EFUSE_DATA[0-3]:
	 * DATA0: 3 2 1 0
	 * DATA1: 7 6 5 4
	 * DATA2: B A 9 8
	 * DATA3: F E D C
	 */
	tmp &= ~(MTW_EFSROM_MODE_MASK | MTW_EFSROM_AIN_MASK);
	tmp |= (addr & ~0xf) << MTW_EFSROM_AIN_SHIFT | MTW_EFSROM_KICK;
	mtw_write(sc, MTW_EFUSE_CTRL, tmp);
	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_EFUSE_CTRL, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_EFSROM_KICK))
			break;
		DELAY(2);
	}
	if (ntries == 100)
		return ETIMEDOUT;

	if ((tmp & MTW_EFUSE_AOUT_MASK) == MTW_EFUSE_AOUT_MASK) {
		*val = 0xffff;	/* address not found */
		return 0;
	}
	/* determine to which 32-bit register our 16-bit word belongs */
	reg = MTW_EFUSE_DATA0 + (addr & 0xc);
	if ((error = mtw_read(sc, reg, &tmp)) != 0)
		return error;

	*val = (addr & 2) ? tmp >> 16 : tmp & 0xffff;
	return 0;
}

int
mtw_eeprom_read_2(struct mtw_softc *sc, uint16_t addr, uint16_t *val)
{
	usb_device_request_t req;
	uint16_t tmp;
	int error;

	addr *= 2;
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MTW_EEPROM_READ;
	USETW(req.wValue, 0);
	USETW(req.wIndex, addr);
	USETW(req.wLength, sizeof tmp);
	error = usbd_do_request(sc->sc_udev, &req, &tmp);
	if (error == 0)
		*val = letoh16(tmp);
	else
		*val = 0xffff;
	return error;
}

static __inline int
mtw_srom_read(struct mtw_softc *sc, uint16_t addr, uint16_t *val)
{
	/* either eFUSE ROM or EEPROM */
	return sc->sc_srom_read(sc, addr, val);
}

int
mtw_rf_read(struct mtw_softc *sc, uint8_t bank, uint8_t reg, uint8_t *val)
{
	uint32_t tmp;
	int error, ntries, shift;

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_RF_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_RF_CSR_KICK))
			break;
	}
	if (ntries == 100)
		return ETIMEDOUT;

	if (sc->mac_ver == 0x7601)
		shift = MT7601_BANK_SHIFT;
	else
		shift = MT7610_BANK_SHIFT;

	tmp = MTW_RF_CSR_KICK | (bank & 0xf) << shift | reg << 8;
	if ((error = mtw_write(sc, MTW_RF_CSR, tmp)) != 0)
		return error;

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_RF_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_RF_CSR_KICK))
			break;
	}
	if (ntries == 100)
		return ETIMEDOUT;

	*val = tmp & 0xff;
	return 0;
}

int
mtw_rf_write(struct mtw_softc *sc, uint8_t bank, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int error, ntries, shift;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = mtw_read(sc, MTW_RF_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_RF_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return ETIMEDOUT;

	if (sc->mac_ver == 0x7601)
		shift = MT7601_BANK_SHIFT;
	else
		shift = MT7610_BANK_SHIFT;

	tmp = MTW_RF_CSR_WRITE | MTW_RF_CSR_KICK | (bank & 0xf) << shift |
	    reg << 8 | val;
	return mtw_write(sc, MTW_RF_CSR, tmp);
}

int
mtw_bbp_read(struct mtw_softc *sc, uint8_t reg, uint8_t *val)
{
	uint32_t tmp;
	int ntries, error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = mtw_read(sc, MTW_BBP_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return ETIMEDOUT;

	tmp = MTW_BBP_CSR_READ | MTW_BBP_CSR_KICK | reg << MTW_BBP_ADDR_SHIFT;
	if ((error = mtw_write(sc, MTW_BBP_CSR, tmp)) != 0)
		return error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = mtw_read(sc, MTW_BBP_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return ETIMEDOUT;

	*val = tmp & 0xff;
	return 0;
}

int
mtw_bbp_write(struct mtw_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int ntries, error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = mtw_read(sc, MTW_BBP_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return ETIMEDOUT;

	tmp = MTW_BBP_CSR_KICK | reg << MTW_BBP_ADDR_SHIFT | val;
	return mtw_write(sc, MTW_BBP_CSR, tmp);
}

int
mtw_usb_dma_read(struct mtw_softc *sc, uint32_t *val)
{
	if (sc->asic_ver == 0x7612)
		return mtw_read_cfg(sc, MTW_USB_U3DMA_CFG, val);
	else
		return mtw_read(sc, MTW_USB_DMA_CFG, val);
}

int
mtw_usb_dma_write(struct mtw_softc *sc, uint32_t val)
{
	if (sc->asic_ver == 0x7612)
		return mtw_write_cfg(sc, MTW_USB_U3DMA_CFG, val);
	else
		return mtw_write(sc, MTW_USB_DMA_CFG, val);
}

int
mtw_mcu_calibrate(struct mtw_softc *sc, int func, uint32_t val)
{
	struct mtw_mcu_cmd_8 cmd;
	
	cmd.func = htole32(func);
	cmd.val = htole32(val);
	return mtw_mcu_cmd(sc, 31, &cmd, sizeof(struct mtw_mcu_cmd_8));
}

int
mtw_mcu_channel(struct mtw_softc *sc, uint32_t r1, uint32_t r2, uint32_t r4)
{
	struct mtw_mcu_cmd_16 cmd;

	cmd.r1 = htole32(r1);
	cmd.r2 = htole32(r2);
	cmd.r3 = 0;
	cmd.r4 = htole32(r4);
	return mtw_mcu_cmd(sc, 30, &cmd, sizeof(struct mtw_mcu_cmd_16));
}

int
mtw_mcu_radio(struct mtw_softc *sc, int func, uint32_t val)
{
	struct mtw_mcu_cmd_16 cmd;

	cmd.r1 = htole32(func);
	cmd.r2 = htole32(val);
	cmd.r3 = 0;
	cmd.r4 = 0;
	return mtw_mcu_cmd(sc, 20, &cmd, sizeof(struct mtw_mcu_cmd_16));
}

int
mtw_mcu_cmd(struct mtw_softc *sc, int cmd, void *buf, int len)
{
	struct mtw_tx_ring *ring = &sc->sc_mcu;
	struct mtw_tx_data *data = &ring->data[0];
	struct mtw_txd *txd;
	int xferlen;

	txd = (struct mtw_txd *)(data->buf);
	txd->len = htole16(len);
	txd->flags = htole16(MTW_TXD_CMD | MTW_TXD_MCU |
	    (cmd & 0x1f) << MTW_TXD_CMD_SHIFT | (sc->cmd_seq & 0xf));

	memcpy(&txd[1], buf, len);
	memset(&txd[1] + len, 0, MTW_DMA_PAD);
	xferlen = len + sizeof(struct mtw_txd) + MTW_DMA_PAD;

	usbd_setup_xfer(data->xfer, sc->txq[MTW_TXQ_MCU].pipeh,
	    NULL, data->buf, xferlen,
	    USBD_SHORT_XFER_OK | USBD_FORCE_SHORT_XFER | USBD_SYNCHRONOUS,
	    MTW_TX_TIMEOUT, NULL);
	return usbd_transfer(data->xfer);
}

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
mtw_get_rf(int rev)
{
	switch (rev) {
	case MT7601_RF_7601:	return "MT7601";
	case MT7610_RF_7610:	return "MT7610";
	case MT7612_RF_7612:	return "MT7612";
	}
	return "unknown";
}

void
mtw_get_txpower(struct mtw_softc *sc)
{
	uint16_t val;
	int i;

	/* Read power settings for 2GHz channels. */
	for (i = 0; i < 14; i += 2) {
		mtw_srom_read(sc, MTW_EEPROM_PWR2GHZ_BASE1 + i / 2, &val);
		sc->txpow1[i + 0] = (int8_t)(val & 0xff);
		sc->txpow1[i + 1] = (int8_t)(val >> 8);
		mtw_srom_read(sc, MTW_EEPROM_PWR2GHZ_BASE2 + i / 2, &val);
		sc->txpow2[i + 0] = (int8_t)(val & 0xff);
		sc->txpow2[i + 1] = (int8_t)(val >> 8);
	}
	/* Fix broken Tx power entries. */
	for (i = 0; i < 14; i++) {
		if (sc->txpow1[i] < 0 || sc->txpow1[i] > 27)
			sc->txpow1[i] = 5;
		if (sc->txpow2[i] < 0 || sc->txpow2[i] > 27)
			sc->txpow2[i] = 5;
		DPRINTF(("chan %d: power1=%d, power2=%d\n",
		    mt7601_rf_chan[i].chan, sc->txpow1[i], sc->txpow2[i]));
	}
#if 0
	/* Read power settings for 5GHz channels. */
	for (i = 0; i < 40; i += 2) {
		mtw_srom_read(sc, MTW_EEPROM_PWR5GHZ_BASE1 + i / 2, &val);
		sc->txpow1[i + 14] = (int8_t)(val & 0xff);
		sc->txpow1[i + 15] = (int8_t)(val >> 8);

		mtw_srom_read(sc, MTW_EEPROM_PWR5GHZ_BASE2 + i / 2, &val);
		sc->txpow2[i + 14] = (int8_t)(val & 0xff);
		sc->txpow2[i + 15] = (int8_t)(val >> 8);
	}
	/* Fix broken Tx power entries. */
	for (i = 0; i < 40; i++ ) {
		if (sc->mac_ver != 0x5592) {
			if (sc->txpow1[14 + i] < -7 || sc->txpow1[14 + i] > 15)
				sc->txpow1[14 + i] = 5;
			if (sc->txpow2[14 + i] < -7 || sc->txpow2[14 + i] > 15)
				sc->txpow2[14 + i] = 5;
		}
		DPRINTF(("chan %d: power1=%d, power2=%d\n",
		    mt7601_rf_chan[14 + i].chan, sc->txpow1[14 + i],
		    sc->txpow2[14 + i]));
	}
#endif
}

int
mtw_read_eeprom(struct mtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int8_t delta_2ghz, delta_5ghz;
	uint16_t val;
	int ridx, ant;

	sc->sc_srom_read = mtw_efuse_read_2;

	/* read RF information */
	mtw_srom_read(sc, MTW_EEPROM_CHIPID, &val);
	sc->rf_rev = val;
	mtw_srom_read(sc, MTW_EEPROM_ANTENNA, &val);
	sc->ntxchains = (val >> 4) & 0xf;
	sc->nrxchains = val & 0xf;
	DPRINTF(("EEPROM RF rev=0x%02x chains=%dT%dR\n",
	    sc->rf_rev, sc->ntxchains, sc->nrxchains));

	/* read ROM version */
	mtw_srom_read(sc, MTW_EEPROM_VERSION, &val);
	DPRINTF(("EEPROM rev=%d, FAE=%d\n", val & 0xff, val >> 8));

	/* read MAC address */
	mtw_srom_read(sc, MTW_EEPROM_MAC01, &val);
	ic->ic_myaddr[0] = val & 0xff;
	ic->ic_myaddr[1] = val >> 8;
	mtw_srom_read(sc, MTW_EEPROM_MAC23, &val);
	ic->ic_myaddr[2] = val & 0xff;
	ic->ic_myaddr[3] = val >> 8;
	mtw_srom_read(sc, MTW_EEPROM_MAC45, &val);
	ic->ic_myaddr[4] = val & 0xff;
	ic->ic_myaddr[5] = val >> 8;
#if 0
	printf("eFUSE ROM\n00: ");
	for (int i = 0; i < 256; i++) {
		if (((i % 8) == 0) && i > 0)
			printf("\n%02x: ", i);
		mtw_srom_read(sc, i, &val);
		printf(" %04x", val);
	}
	printf("\n");
#endif
	/* check if RF supports automatic Tx access gain control */
	mtw_srom_read(sc, MTW_EEPROM_CONFIG, &val);
	DPRINTF(("EEPROM CFG 0x%04x\n", val));
	if ((val & 0xff) != 0xff) {
		sc->ext_5ghz_lna = (val >> 3) & 1;
		sc->ext_2ghz_lna = (val >> 2) & 1;
		/* check if RF supports automatic Tx access gain control */
		sc->calib_2ghz = sc->calib_5ghz = (val >> 1) & 1;
		/* check if we have a hardware radio switch */
		sc->rfswitch = val & 1;
	}

	/* read RF frequency offset from EEPROM */
	mtw_srom_read(sc, MTW_EEPROM_FREQ_OFFSET, &val);
	if ((val & 0xff) != 0xff)
		sc->rf_freq_offset = val;
	else
		sc->rf_freq_offset = 0;
	DPRINTF(("frequency offset 0x%x\n", sc->rf_freq_offset));

	/* Read Tx power settings. */
	mtw_get_txpower(sc);

	/* read Tx power compensation for each Tx rate */
	mtw_srom_read(sc, MTW_EEPROM_DELTAPWR, &val);
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

		mtw_srom_read(sc, MTW_EEPROM_RPWR + ridx * 2, &val);
		reg = val;
		mtw_srom_read(sc, MTW_EEPROM_RPWR + ridx * 2 + 1, &val);
		reg |= (uint32_t)val << 16;

		sc->txpow20mhz[ridx] = reg;
		sc->txpow40mhz_2ghz[ridx] = b4inc(reg, delta_2ghz);
		sc->txpow40mhz_5ghz[ridx] = b4inc(reg, delta_5ghz);

		DPRINTF(("ridx %d: power 20MHz=0x%08x, 40MHz/2GHz=0x%08x, "
		    "40MHz/5GHz=0x%08x\n", ridx, sc->txpow20mhz[ridx],
		    sc->txpow40mhz_2ghz[ridx], sc->txpow40mhz_5ghz[ridx]));
	}

	/* read RSSI offsets and LNA gains from EEPROM */
	val = 0;
	mtw_srom_read(sc, MTW_EEPROM_RSSI1_2GHZ, &val);
	sc->rssi_2ghz[0] = val & 0xff;	/* Ant A */
	sc->rssi_2ghz[1] = val >> 8;	/* Ant B */
	mtw_srom_read(sc, MTW_EEPROM_RSSI2_2GHZ, &val);
	/*
	 * On RT3070 chips (limited to 2 Rx chains), this ROM
	 * field contains the Tx mixer gain for the 2GHz band.
	 */
	if ((val & 0xff) != 0xff)
		sc->txmixgain_2ghz = val & 0x7;
	DPRINTF(("tx mixer gain=%u (2GHz)\n", sc->txmixgain_2ghz));
	sc->lna[2] = val >> 8;		/* channel group 2 */
	mtw_srom_read(sc, MTW_EEPROM_RSSI1_5GHZ, &val);
	sc->rssi_5ghz[0] = val & 0xff;	/* Ant A */
	sc->rssi_5ghz[1] = val >> 8;	/* Ant B */
	mtw_srom_read(sc, MTW_EEPROM_RSSI2_5GHZ, &val);
	sc->rssi_5ghz[2] = val & 0xff;	/* Ant C */

	sc->lna[3] = val >> 8;		/* channel group 3 */

	mtw_srom_read(sc, MTW_EEPROM_LNA, &val);
	sc->lna[0] = val & 0xff;	/* channel group 0 */
	sc->lna[1] = val >> 8;		/* channel group 1 */
	DPRINTF(("LNA0 0x%x\n", sc->lna[0]));

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

struct ieee80211_node *
mtw_node_alloc(struct ieee80211com *ic)
{
	struct mtw_node *mn;

	mn = malloc(sizeof (struct mtw_node), M_USBDEV, M_NOWAIT | M_ZERO);
	return (struct ieee80211_node *)mn;
}

int
mtw_media_change(struct ifnet *ifp)
{
	struct mtw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rate, ridx;
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if (ic->ic_fixed_rate != -1) {
		rate = ic->ic_sup_rates[ic->ic_curmode].
		    rs_rates[ic->ic_fixed_rate] & IEEE80211_RATE_VAL;
		for (ridx = 0; ridx <= MTW_RIDX_MAX; ridx++)
			if (rt2860_rates[ridx].rate == rate)
				break;
		sc->fixed_ridx = ridx;
	}

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING)) {
		mtw_stop(ifp, 0);
		error = mtw_init(ifp);
	}
	return error;
}

void
mtw_next_scan(void *arg)
{
	struct mtw_softc *sc = arg;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);

	s = splnet();
	if (sc->sc_ic.ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(&sc->sc_ic.ic_if);
	splx(s);

	usbd_ref_decr(sc->sc_udev);
}

void
mtw_task(void *arg)
{
	struct mtw_softc *sc = arg;
	struct mtw_host_cmd_ring *ring = &sc->cmdq;
	struct mtw_host_cmd *cmd;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	/* process host commands */
	s = splusb();
	while (ring->next != ring->cur) {
		cmd = &ring->cmd[ring->next];
		splx(s);
		/* callback */
		cmd->cb(sc, cmd->data);
		s = splusb();
		ring->queued--;
		ring->next = (ring->next + 1) % MTW_HOST_CMD_RING_COUNT;
	}
	splx(s);
}

void
mtw_do_async(struct mtw_softc *sc, void (*cb)(struct mtw_softc *, void *),
    void *arg, int len)
{
	struct mtw_host_cmd_ring *ring = &sc->cmdq;
	struct mtw_host_cmd *cmd;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	s = splusb();
	cmd = &ring->cmd[ring->cur];
	cmd->cb = cb;
	KASSERT(len <= sizeof (cmd->data));
	memcpy(cmd->data, arg, len);
	ring->cur = (ring->cur + 1) % MTW_HOST_CMD_RING_COUNT;

	/* if there is no pending command already, schedule a task */
	if (++ring->queued == 1)
		usb_add_task(sc->sc_udev, &sc->sc_task);
	splx(s);
}

int
mtw_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct mtw_softc *sc = ic->ic_softc;
	struct mtw_cmd_newstate cmd;

	/* do it in a process context */
	cmd.state = nstate;
	cmd.arg = arg;
	mtw_do_async(sc, mtw_newstate_cb, &cmd, sizeof cmd);
	return 0;
}

void	
mtw_newstate_cb(struct mtw_softc *sc, void *arg)
{
	struct mtw_cmd_newstate *cmd = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state ostate;
	struct ieee80211_node *ni;
	uint32_t sta[3];
	uint8_t wcid;
	int s;

	s = splnet();
	ostate = ic->ic_state;

	if (ostate == IEEE80211_S_RUN) {
		/* turn link LED on */
		mtw_set_leds(sc, MTW_LED_MODE_ON);
	}

	switch (cmd->state) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* abort TSF synchronization */
			mtw_abort_tsf_sync(sc);
		}
		break;

	case IEEE80211_S_SCAN:
		mtw_set_chan(sc, ic->ic_bss->ni_chan);
		if (!usbd_is_dying(sc->sc_udev))
			timeout_add_msec(&sc->scan_to, 200);
		break;

	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		mtw_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_RUN:
		mtw_set_chan(sc, ic->ic_bss->ni_chan);

		ni = ic->ic_bss;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			mtw_updateslot(ic);
			mtw_enable_mrr(sc);
			mtw_set_txpreamble(sc);
			mtw_set_basicrates(sc);
			mtw_set_bssid(sc, ni->ni_bssid);
		}
		if (ic->ic_opmode == IEEE80211_M_STA) {
			/* add BSS entry to the WCID table */
			wcid = MTW_AID2WCID(ni->ni_associd);
			mtw_write_region_1(sc, MTW_WCID_ENTRY(wcid),
			    ni->ni_macaddr, IEEE80211_ADDR_LEN);

			/* fake a join to init the tx rate */
			mtw_newassoc(ic, ni, 1);
		}
		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			mtw_enable_tsf_sync(sc);

			/* clear statistic registers used by AMRR */
			mtw_read_region_1(sc, MTW_TX_STA_CNT0,
			    (uint8_t *)sta, sizeof sta);
			/* start calibration timer */
			if (!usbd_is_dying(sc->sc_udev))
				timeout_add_sec(&sc->calib_to, 1);
		}

		/* turn link LED on */
		mtw_set_leds(sc, MTW_LED_MODE_BLINK_TX);
		break;
	}
	(void)sc->sc_newstate(ic, cmd->state, cmd->arg);
	splx(s);
}

void
mtw_updateedca(struct ieee80211com *ic)
{
	/* do it in a process context */
	mtw_do_async(ic->ic_softc, mtw_updateedca_cb, NULL, 0);
}

void
mtw_updateedca_cb(struct mtw_softc *sc, void *arg)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int s, aci;

	s = splnet();
	/* update MAC TX configuration registers */
	for (aci = 0; aci < EDCA_NUM_AC; aci++) {
		mtw_write(sc, MTW_EDCA_AC_CFG(aci),
		    ic->ic_edca_ac[aci].ac_ecwmax << 16 |
		    ic->ic_edca_ac[aci].ac_ecwmin << 12 |
		    ic->ic_edca_ac[aci].ac_aifsn  <<  8 |
		    ic->ic_edca_ac[aci].ac_txoplimit);
	}

	/* update SCH/DMA registers too */
	mtw_write(sc, MTW_WMM_AIFSN_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_aifsn  << 12 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_aifsn  <<  8 |
	    ic->ic_edca_ac[EDCA_AC_BK].ac_aifsn  <<  4 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_aifsn);
	mtw_write(sc, MTW_WMM_CWMIN_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_ecwmin << 12 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_ecwmin <<  8 |
	    ic->ic_edca_ac[EDCA_AC_BK].ac_ecwmin <<  4 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_ecwmin);
	mtw_write(sc, MTW_WMM_CWMAX_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_ecwmax << 12 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_ecwmax <<  8 |
	    ic->ic_edca_ac[EDCA_AC_BK].ac_ecwmax <<  4 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_ecwmax);
	mtw_write(sc, MTW_WMM_TXOP0_CFG,
	    ic->ic_edca_ac[EDCA_AC_BK].ac_txoplimit << 16 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_txoplimit);
	mtw_write(sc, MTW_WMM_TXOP1_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_txoplimit << 16 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_txoplimit);
	splx(s);
}

void
mtw_updateslot(struct ieee80211com *ic)
{
	/* do it in a process context */
	mtw_do_async(ic->ic_softc, mtw_updateslot_cb, NULL, 0);
}

void
mtw_updateslot_cb(struct mtw_softc *sc, void *arg)
{
	uint32_t tmp;

	mtw_read(sc, MTW_BKOFF_SLOT_CFG, &tmp);
	tmp &= ~0xff;
	tmp |= (sc->sc_ic.ic_flags & IEEE80211_F_SHSLOT) ?
	    IEEE80211_DUR_DS_SHSLOT : IEEE80211_DUR_DS_SLOT;
	mtw_write(sc, MTW_BKOFF_SLOT_CFG, tmp);
}

int
mtw_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct mtw_softc *sc = ic->ic_softc;
	struct mtw_cmd_key cmd;

	/* defer setting of WEP keys until interface is brought up */
	if ((ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) !=
	    (IFF_UP | IFF_RUNNING))
		return 0;

	/* do it in a process context */
	cmd.key = *k;
	cmd.ni = ni;
	mtw_do_async(sc, mtw_set_key_cb, &cmd, sizeof cmd);
	sc->sc_key_tasks++;
	return EBUSY;
}

void
mtw_set_key_cb(struct mtw_softc *sc, void *arg)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct mtw_cmd_key *cmd = arg;
	struct ieee80211_key *k = &cmd->key;
	uint32_t attr;
	uint16_t base;
	uint8_t mode, wcid, iv[8];

	sc->sc_key_tasks--;

	/* map net80211 cipher to RT2860 security mode */
	switch (k->k_cipher) {
	case IEEE80211_CIPHER_WEP40:
		mode = MTW_MODE_WEP40;
		break;
	case IEEE80211_CIPHER_WEP104:
		mode = MTW_MODE_WEP104;
		break;
	case IEEE80211_CIPHER_TKIP:
		mode = MTW_MODE_TKIP;
		break;
	case IEEE80211_CIPHER_CCMP:
		mode = MTW_MODE_AES_CCMP;
		break;
	default:
		if (cmd->ni != NULL) {
			IEEE80211_SEND_MGMT(ic, cmd->ni,
			    IEEE80211_FC0_SUBTYPE_DEAUTH,
			    IEEE80211_REASON_AUTH_LEAVE);
		}
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		return;
	}

	if (k->k_flags & IEEE80211_KEY_GROUP) {
		wcid = 0;	/* NB: update WCID0 for group keys */
		base = MTW_SKEY(0, k->k_id);
	} else {
		wcid = (cmd->ni != NULL) ? MTW_AID2WCID(cmd->ni->ni_associd) : 0;
		base = MTW_PKEY(wcid);
	}

	if (k->k_cipher == IEEE80211_CIPHER_TKIP) {
		mtw_write_region_1(sc, base, k->k_key, 16);
		mtw_write_region_1(sc, base + 16, &k->k_key[24], 8);
		mtw_write_region_1(sc, base + 24, &k->k_key[16], 8);
	} else {
		/* roundup len to 16-bit: XXX fix write_region_1() instead */
		mtw_write_region_1(sc, base, k->k_key, (k->k_len + 1) & ~1);
	}

	if (!(k->k_flags & IEEE80211_KEY_GROUP) ||
	    (k->k_flags & IEEE80211_KEY_TX)) {
		/* set initial packet number in IV+EIV */
		if (k->k_cipher == IEEE80211_CIPHER_WEP40 ||
		    k->k_cipher == IEEE80211_CIPHER_WEP104) {
			memset(iv, 0, sizeof iv);
			iv[3] = sc->sc_ic.ic_def_txkey << 6;
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
		mtw_write_region_1(sc, MTW_IVEIV(wcid), iv, 8);
	}

	if (k->k_flags & IEEE80211_KEY_GROUP) {
		/* install group key */
		mtw_read(sc, MTW_SKEY_MODE_0_7, &attr);
		attr &= ~(0xf << (k->k_id * 4));
		attr |= mode << (k->k_id * 4);
		mtw_write(sc, MTW_SKEY_MODE_0_7, attr);

		if (k->k_cipher & (IEEE80211_CIPHER_WEP104 |
		    IEEE80211_CIPHER_WEP40)) {
			mtw_read(sc, MTW_WCID_ATTR(wcid + 1), &attr);
			attr = (attr & ~0xf) | (mode << 1);
			mtw_write(sc, MTW_WCID_ATTR(wcid + 1), attr);

			mtw_set_region_4(sc, MTW_IVEIV(0), 0, 4);

			mtw_read(sc, MTW_WCID_ATTR(wcid), &attr);
			attr = (attr & ~0xf) | (mode << 1);
			mtw_write(sc, MTW_WCID_ATTR(wcid), attr);
		}
	} else {
		/* install pairwise key */
		mtw_read(sc, MTW_WCID_ATTR(wcid), &attr);
		attr = (attr & ~0xf) | (mode << 1) | MTW_RX_PKEY_EN;
		mtw_write(sc, MTW_WCID_ATTR(wcid), attr);
	}

	if (sc->sc_key_tasks == 0) {
		if (cmd->ni != NULL)
			cmd->ni->ni_port_valid = 1;
		ieee80211_set_link_state(ic, LINK_STATE_UP);
	}
}

void
mtw_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct mtw_softc *sc = ic->ic_softc;
	struct mtw_cmd_key cmd;

	if (!(ic->ic_if.if_flags & IFF_RUNNING) ||
	    ic->ic_state != IEEE80211_S_RUN)
		return;	/* nothing to do */

	/* do it in a process context */
	cmd.key = *k;
	cmd.ni = ni;
	mtw_do_async(sc, mtw_delete_key_cb, &cmd, sizeof cmd);
}

void
mtw_delete_key_cb(struct mtw_softc *sc, void *arg)
{
	struct mtw_cmd_key *cmd = arg;
	struct ieee80211_key *k = &cmd->key;
	uint32_t attr;
	uint8_t wcid;

	if (k->k_flags & IEEE80211_KEY_GROUP) {
		/* remove group key */
		mtw_read(sc, MTW_SKEY_MODE_0_7, &attr);
		attr &= ~(0xf << (k->k_id * 4));
		mtw_write(sc, MTW_SKEY_MODE_0_7, attr);

	} else {
		/* remove pairwise key */
		wcid = (cmd->ni != NULL) ? MTW_AID2WCID(cmd->ni->ni_associd) : 0;
		mtw_read(sc, MTW_WCID_ATTR(wcid), &attr);
		attr &= ~0xf;
		mtw_write(sc, MTW_WCID_ATTR(wcid), attr);
	}
}

void
mtw_calibrate_to(void *arg)
{
	/* do it in a process context */
	mtw_do_async(arg, mtw_calibrate_cb, NULL, 0);
	/* next timeout will be rescheduled in the calibration task */
}

void
mtw_calibrate_cb(struct mtw_softc *sc, void *arg)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t sta[3];
	int s, error;

	/* read statistic counters (clear on read) and update AMRR state */
	error = mtw_read_region_1(sc, MTW_TX_STA_CNT0, (uint8_t *)sta,
	    sizeof sta);
	if (error != 0)
		goto skip;

	DPRINTF(("retrycnt=%d txcnt=%d failcnt=%d\n",
	    letoh32(sta[1]) >> 16, letoh32(sta[1]) & 0xffff,
	    letoh32(sta[0]) & 0xffff));

	s = splnet();
	/* count failed TX as errors */
	ifp->if_oerrors += letoh32(sta[0]) & 0xffff;

	sc->amn.amn_retrycnt =
	    (letoh32(sta[0]) & 0xffff) +	/* failed TX count */
	    (letoh32(sta[1]) >> 16);		/* TX retransmission count */

	sc->amn.amn_txcnt =
	    sc->amn.amn_retrycnt +
	    (letoh32(sta[1]) & 0xffff);		/* successful TX count */

	ieee80211_amrr_choose(&sc->amrr, sc->sc_ic.ic_bss, &sc->amn);

	splx(s);
skip:
	if (!usbd_is_dying(sc->sc_udev))
		timeout_add_sec(&sc->calib_to, 1);
}

void
mtw_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct mtw_softc *sc = ic->ic_softc;
	struct mtw_node *mn = (void *)ni;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	uint8_t rate;
	int ridx, i, j;

	DPRINTF(("new assoc isnew=%d addr=%s\n",
	    isnew, ether_sprintf(ni->ni_macaddr)));

	ieee80211_amrr_node_init(&sc->amrr, &sc->amn);
	
	/* start at lowest available bit-rate, AMRR will raise */
	ni->ni_txrate = 0;

	for (i = 0; i < rs->rs_nrates; i++) {
		rate = rs->rs_rates[i] & IEEE80211_RATE_VAL;
		/* convert 802.11 rate to hardware rate index */
		for (ridx = 0; ridx < MTW_RIDX_MAX; ridx++)
			if (rt2860_rates[ridx].rate == rate)
				break;
		mn->ridx[i] = ridx;
		/* determine rate of control response frames */
		for (j = i; j >= 0; j--) {
			if ((rs->rs_rates[j] & IEEE80211_RATE_BASIC) &&
			    rt2860_rates[mn->ridx[i]].phy ==
			    rt2860_rates[mn->ridx[j]].phy)
				break;
		}
		if (j >= 0) {
			mn->ctl_ridx[i] = mn->ridx[j];
		} else {
			/* no basic rate found, use mandatory one */
			mn->ctl_ridx[i] = rt2860_rates[ridx].ctl_ridx;
		}
		DPRINTF(("rate=0x%02x ridx=%d ctl_ridx=%d\n",
		    rs->rs_rates[i], mn->ridx[i], mn->ctl_ridx[i]));
	}
}

/*
 * Return the Rx chain with the highest RSSI for a given frame.
 */
static __inline uint8_t
mtw_maxrssi_chain(struct mtw_softc *sc, const struct mtw_rxwi *rxwi)
{
	uint8_t rxchain = 0;

	if (sc->nrxchains > 1) {
		if (rxwi->rssi[1] > rxwi->rssi[rxchain])
			rxchain = 1;
	}
	return rxchain;
}

void
mtw_rx_frame(struct mtw_softc *sc, uint8_t *buf, int dmalen,
    struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct mtw_rxwi *rxwi;
	struct mbuf *m;
	uint32_t flags;
	uint16_t len;
#if NBPFILTER > 0
	uint16_t phy;
#endif
	uint16_t rxwisize;
	uint8_t ant, rssi;
	int s;

	/* Rx Wireless Information */
	rxwi = (struct mtw_rxwi *)(buf);
	rxwisize = sizeof(struct mtw_rxwi);
	len = letoh16(rxwi->len) & 0xfff;

	if (__predict_false(len > dmalen)) {
		DPRINTF(("bad RXWI length %u > %u\n", len, dmalen));
		return;
	}
	if (len > MCLBYTES) {
		DPRINTF(("frame too large (length=%d)\n", len));
		ifp->if_ierrors++;
		return;
	}

	flags = letoh32(rxwi->flags);
	if (__predict_false(flags & (MTW_RX_CRCERR | MTW_RX_ICVERR))) {
		ifp->if_ierrors++;
		return;
	}
	if (__predict_false((flags & MTW_RX_MICERR))) {
		/* report MIC failures to net80211 for TKIP */
		ic->ic_stats.is_rx_locmicfail++;
		ieee80211_michael_mic_failure(ic, 0/* XXX */);
		ifp->if_ierrors++;
		return;
	}

	wh = (struct ieee80211_frame *)(buf + rxwisize);
	memset(&rxi, 0, sizeof(rxi));
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
		rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
	}

	if (flags & MTW_RX_L2PAD) {
		u_int hdrlen = ieee80211_get_hdrlen(wh);
		memmove((caddr_t)wh + 2, wh, hdrlen);
		wh = (struct ieee80211_frame *)((caddr_t)wh + 2);
	}

	/* could use m_devget but net80211 wants contig mgmt frames */
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (__predict_false(m == NULL)) {
		ifp->if_ierrors++;
		return;
	}
	if (len > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if (__predict_false(!(m->m_flags & M_EXT))) {
			ifp->if_ierrors++;
			m_freem(m);
			return;
		}
	}
	/* finalize mbuf */
	memcpy(mtod(m, caddr_t), wh, len);
	m->m_pkthdr.len = m->m_len = len;

	ant = mtw_maxrssi_chain(sc, rxwi);
	rssi = rxwi->rssi[ant];

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct mtw_rx_radiotap_header *tap = &sc->sc_rxtap;
		struct mbuf mb;

		tap->wr_flags = 0;
		tap->wr_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);
		tap->wr_antsignal = rssi;
		tap->wr_antenna = ant;
		tap->wr_dbm_antsignal = mtw_rssi2dbm(sc, rssi, ant);
		tap->wr_rate = 2;	/* in case it can't be found below */
		phy = letoh16(rxwi->phy);
		switch (phy >> MT7601_PHY_SHIFT) {
		case MTW_PHY_CCK:
			switch ((phy & MTW_PHY_MCS) & ~MTW_PHY_SHPRE) {
			case 0:	tap->wr_rate =   2; break;
			case 1:	tap->wr_rate =   4; break;
			case 2:	tap->wr_rate =  11; break;
			case 3:	tap->wr_rate =  22; break;
			}
			if (phy & MTW_PHY_SHPRE)
				tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
			break;
		case MTW_PHY_OFDM:
			switch (phy & MTW_PHY_MCS) {
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
		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_rxtap_len;
		mb.m_next = m;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_IN);
	}
#endif

	s = splnet();
	ni = ieee80211_find_rxnode(ic, wh);
	rxi.rxi_rssi = rssi;
	ieee80211_inputm(ifp, m, ni, &rxi, ml);

	/* node is no longer needed */
	ieee80211_release_node(ic, ni);
	splx(s);
}

void
mtw_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mtw_rx_data *data = priv;
	struct mtw_softc *sc = data->sc;
	uint8_t *buf;
	uint32_t dmalen;
	int xferlen;

	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("RX status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->rxq[0].pipeh);
		if (status != USBD_CANCELLED)
			goto skip;
		return;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &xferlen, NULL);

	if (__predict_false(xferlen < sizeof(uint32_t) +
	    sizeof (struct mtw_rxwi) + sizeof(struct mtw_rxd))) {
		DPRINTF(("RX xfer too short %d\n", xferlen));
		goto skip;
	}

	/* HW can aggregate multiple 802.11 frames in a single USB xfer */
	buf = data->buf;
	while (xferlen > 8) {
		dmalen = letoh32(*(uint32_t *)buf) & MTW_RXD_LEN;
		if (__predict_false(dmalen == 0 || (dmalen & 3) != 0)) {
			DPRINTF(("bad DMA length %u\n", dmalen));
			break;
		}
		if (__predict_false(dmalen + 8 > xferlen)) {
			DPRINTF(("bad DMA length %u > %d\n",
			    dmalen + 8, xferlen));
			break;
		}
		mtw_rx_frame(sc, buf + sizeof(struct mtw_rxd), dmalen, &ml);
		buf += dmalen + 8;
		xferlen -= dmalen + 8;
	}
	if_input(&sc->sc_ic.ic_if, &ml);

skip:	/* setup a new transfer */
	usbd_setup_xfer(xfer, sc->rxq[0].pipeh, data, data->buf, MTW_MAX_RXSZ,
	    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT, mtw_rxeof);
	(void)usbd_transfer(data->xfer);
}

void
mtw_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct mtw_tx_data *data = priv;
	struct mtw_softc *sc = data->sc;
	struct mtw_tx_ring *txq = &sc->txq[data->qid];
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	s = splnet();
	txq->queued--;
	sc->qfullmsk &= ~(1 << data->qid);

	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("TX status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(txq->pipeh);
		ifp->if_oerrors++;
		splx(s);
		return;
	}

	sc->sc_tx_timer = 0;

	if (ifq_is_oactive(&ifp->if_snd)) {
		ifq_clr_oactive(&ifp->if_snd);
		mtw_start(ifp);
	}

	splx(s);
}

int
mtw_tx(struct mtw_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct mtw_node *mn = (void *)ni;
	struct ieee80211_frame *wh;
	struct mtw_tx_ring *ring;
	struct mtw_tx_data *data;
	struct mtw_txd *txd;
	struct mtw_txwi *txwi;
	uint16_t qos, dur;
	uint16_t txwisize;
	uint8_t type, mcs, tid, qid;
	int error, hasqos, ridx, ctl_ridx, xferlen;

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	/* select queue */
	if ((hasqos = ieee80211_has_qos(wh))) {
		qos = ieee80211_get_qos(wh);
		tid = qos & IEEE80211_QOS_TID;
		qid = ieee80211_up_to_ac(ic, tid);
	} else {
		qos = 0;
		tid = 0;
		qid = EDCA_AC_BE;
	}

	/* management frames go to MCU queue */
	if (type == IEEE80211_FC0_TYPE_MGT)
		qid = MTW_TXQ_MCU;

	ring = &sc->txq[qid];
	data = &ring->data[ring->cur];

	/* pickup a rate index */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA) {
		ridx = (ic->ic_curmode == IEEE80211_MODE_11A) ?
		    MTW_RIDX_OFDM6 : MTW_RIDX_CCK1;
		ctl_ridx = rt2860_rates[ridx].ctl_ridx;
	} else if (ic->ic_fixed_rate != -1) {
		ridx = sc->fixed_ridx;
		ctl_ridx = rt2860_rates[ridx].ctl_ridx;
	} else {
		ridx = mn->ridx[ni->ni_txrate];
		ctl_ridx = mn->ctl_ridx[ni->ni_txrate];
	}

	txwisize = sizeof(struct mtw_txwi);
	xferlen = txwisize + m->m_pkthdr.len;

	/* roundup to 32-bit alignment */
	xferlen = (xferlen + 3) & ~3;

	/* setup TX descriptor */
	txd = (struct mtw_txd *)data->buf;
	txd->flags = htole16(MTW_TXD_DATA | MTW_TXD_80211 |
	    MTW_TXD_WLAN | MTW_TXD_QSEL_EDCA);

	if (type != IEEE80211_FC0_TYPE_DATA)
		txd->flags |= htole16(MTW_TXD_WIV);
	txd->len = htole16(xferlen);
	xferlen += sizeof(struct mtw_txd);

	/* get MCS code from rate index */
	mcs = rt2860_rates[ridx].mcs;

	/* setup TX Wireless Information */
	txwi = (struct mtw_txwi *)(txd + 1);
	txwi->flags = 0;
	txwi->xflags = hasqos ? 0 : MTW_TX_NSEQ;
	txwi->wcid = (type == IEEE80211_FC0_TYPE_DATA) ?
	    MTW_AID2WCID(ni->ni_associd) : 0xff;
	txwi->len = htole16(m->m_pkthdr.len);
	txwi->txop = MTW_TX_TXOP_BACKOFF;

	if (rt2860_rates[ridx].phy == IEEE80211_T_DS) {
		txwi->phy = htole16(MTW_PHY_CCK << MT7601_PHY_SHIFT);
		if (ridx != MTW_RIDX_CCK1 &&
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			mcs |= MTW_PHY_SHPRE;
	} else if (rt2860_rates[ridx].phy == IEEE80211_T_OFDM)
		txwi->phy = htole16(MTW_PHY_OFDM << MT7601_PHY_SHIFT);
	txwi->phy |= htole16(mcs);

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (!hasqos || (qos & IEEE80211_QOS_ACK_POLICY_MASK) !=
	     IEEE80211_QOS_ACK_POLICY_NOACK)) {
		txwi->xflags |= MTW_TX_ACK;
		if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
			dur = rt2860_rates[ctl_ridx].sp_ack_dur;
		else
			dur = rt2860_rates[ctl_ridx].lp_ack_dur;
		*(uint16_t *)wh->i_dur = htole16(dur);
	}

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct mtw_tx_radiotap_header *tap = &sc->sc_txtap;
		struct mbuf mb;

		tap->wt_flags = 0;
		tap->wt_rate = rt2860_rates[ridx].rate;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		if (mcs & MTW_PHY_SHPRE)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_txtap_len;
		mb.m_next = m;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_OUT);
	}
#endif
	/* copy payload */
	m_copydata(m, 0, m->m_pkthdr.len, (caddr_t)txwi + txwisize);
	m_freem(m);

	/* 4-byte pad */
	memset(data->buf + xferlen, 0, MTW_DMA_PAD);
	xferlen += MTW_DMA_PAD;

	usbd_setup_xfer(data->xfer, ring->pipeh, data, data->buf,
	    xferlen, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    MTW_TX_TIMEOUT, mtw_txeof);
	error = usbd_transfer(data->xfer);
	if (__predict_false(error != USBD_IN_PROGRESS && error != 0))
		return error;

	ieee80211_release_node(ic, ni);

	ring->cur = (ring->cur + 1) % MTW_TX_RING_COUNT;
	if (++ring->queued >= MTW_TX_RING_COUNT)
		sc->qfullmsk |= 1 << qid;
	return 0;
}

void
mtw_start(struct ifnet *ifp)
{
	struct mtw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->qfullmsk != 0) {
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
		if (mtw_tx(sc, m, ni) != 0) {
			ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			continue;
		}

		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

void
mtw_watchdog(struct ifnet *ifp)
{
	struct mtw_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			/* mtw_init(ifp); XXX needs a process context! */
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
mtw_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct mtw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s, error = 0;

	if (usbd_is_dying(sc->sc_udev))
		return ENXIO;

	usbd_ref_incr(sc->sc_udev);

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING))
				mtw_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				mtw_stop(ifp, 1);
		}
		break;

	case SIOCS80211CHANNEL:
		/*
		 * This allows for fast channel switching in monitor mode
		 * (used by kismet).
		 */
		error = ieee80211_ioctl(ifp, cmd, data);
		if (error == ENETRESET &&
		    ic->ic_opmode == IEEE80211_M_MONITOR) {
			if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
			    (IFF_UP | IFF_RUNNING))
				mtw_set_chan(sc, ic->ic_ibss_chan);
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			mtw_stop(ifp, 0);
			error = mtw_init(ifp);
		} else
			error = 0;
	}
	splx(s);

	usbd_ref_decr(sc->sc_udev);

	return error;
}

void
mtw_select_chan_group(struct mtw_softc *sc, int group)
{
	uint32_t tmp;
	uint8_t bbp;

	/* Tx band 20MHz 2G */
	mtw_read(sc, MTW_TX_BAND_CFG, &tmp);
	tmp &= ~(MTW_TX_BAND_SEL_2G | MTW_TX_BAND_SEL_5G |
	    MTW_TX_BAND_UPPER_40M);
	tmp |= (group == 0) ? MTW_TX_BAND_SEL_2G : MTW_TX_BAND_SEL_5G;
	mtw_write(sc, MTW_TX_BAND_CFG, tmp);

	/* select 20 MHz bandwidth */
	mtw_bbp_read(sc, 4, &bbp);
	bbp &= ~0x18;
	bbp |= 0x40;
	mtw_bbp_write(sc, 4, bbp);

	/* calibrate BBP */
	mtw_bbp_write(sc, 69, 0x12);
	mtw_bbp_write(sc, 91, 0x07);
	mtw_bbp_write(sc, 195, 0x23);
	mtw_bbp_write(sc, 196, 0x17);
	mtw_bbp_write(sc, 195, 0x24);
	mtw_bbp_write(sc, 196, 0x06);
	mtw_bbp_write(sc, 195, 0x81);
	mtw_bbp_write(sc, 196, 0x12);
	mtw_bbp_write(sc, 195, 0x83);
	mtw_bbp_write(sc, 196, 0x17);
	mtw_rf_write(sc, 5, 8, 0x00);
	mtw_mcu_calibrate(sc, 0x6, 0x10001);

	/* set initial AGC value */
	mt7601_set_agc(sc, 0x14);
}

void
mt7601_set_agc(struct mtw_softc *sc, uint8_t agc)
{
	uint8_t bbp;

	mtw_bbp_write(sc, 66, agc);
	mtw_bbp_write(sc, 195, 0x87);
	bbp = (agc & 0xf0) | 0x08;
	mtw_bbp_write(sc, 196, bbp);
}

void
mt7601_set_chan(struct mtw_softc *sc, u_int chan)
{
	uint32_t tmp;
	uint8_t bbp, rf, txpow1;
	int i;

	/* find the settings for this channel */
	for (i = 0; mt7601_rf_chan[i].chan != chan; i++)
		;

	mtw_rf_write(sc, 0, 17, mt7601_rf_chan[i].r17);
	mtw_rf_write(sc, 0, 18, mt7601_rf_chan[i].r18);
	mtw_rf_write(sc, 0, 19, mt7601_rf_chan[i].r19);
	mtw_rf_write(sc, 0, 20, mt7601_rf_chan[i].r20);

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];

	/* Tx automatic level control */
	mtw_read(sc, MTW_TX_ALC_CFG0, &tmp);
	tmp &= ~0x3f3f;
	tmp |= (txpow1 & 0x3f);
	mtw_write(sc, MTW_TX_ALC_CFG0, tmp);

	/* LNA */
	mtw_bbp_write(sc, 62, 0x37 - sc->lna[0]);
	mtw_bbp_write(sc, 63, 0x37 - sc->lna[0]);
	mtw_bbp_write(sc, 64, 0x37 - sc->lna[0]);

	/* VCO calibration */
	mtw_rf_write(sc, 0, 4, 0x0a);
	mtw_rf_write(sc, 0, 5, 0x20);
	mtw_rf_read(sc, 0, 4, &rf);
	mtw_rf_write(sc, 0, 4, rf | 0x80);

	/* select 20 MHz bandwidth */
	mtw_bbp_read(sc, 4, &bbp);
	bbp &= ~0x18;
	bbp |= 0x40;
	mtw_bbp_write(sc, 4, bbp);
	mtw_bbp_write(sc, 178, 0xff);
}

int
mtw_set_chan(struct mtw_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	u_int chan, group;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return EINVAL;

	/* determine channel group */
	if (chan <= 14)
		group = 0;
	else if (chan <= 64)
		group = 1;
	else if (chan <= 128)
		group = 2;
	else
		group = 3;

	if (group != sc->sc_chan_group || !sc->sc_bw_calibrated)
		mtw_select_chan_group(sc, group);

	sc->sc_chan_group = group;

	/* chipset specific */
	if (sc->mac_ver == 0x7601)
		mt7601_set_chan(sc, chan);

	DELAY(1000);
	return 0;
}

void
mtw_enable_tsf_sync(struct mtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	mtw_read(sc, MTW_BCN_TIME_CFG, &tmp);
	tmp &= ~0x1fffff;
	tmp |= ic->ic_bss->ni_intval * 16;
	tmp |= MTW_TSF_TIMER_EN | MTW_TBTT_TIMER_EN;

	/* local TSF is always updated with remote TSF on beacon reception */
	tmp |= 1 << MTW_TSF_SYNC_MODE_SHIFT;
	mtw_write(sc, MTW_BCN_TIME_CFG, tmp);
}

void
mtw_abort_tsf_sync(struct mtw_softc *sc)
{
	uint32_t tmp;

	mtw_read(sc, MTW_BCN_TIME_CFG, &tmp);
	tmp &= ~(MTW_BCN_TX_EN | MTW_TSF_TIMER_EN | MTW_TBTT_TIMER_EN);
	mtw_write(sc, MTW_BCN_TIME_CFG, tmp);
}

void
mtw_enable_mrr(struct mtw_softc *sc)
{
#define CCK(mcs)	(mcs)
#define OFDM(mcs)	(1 << 3 | (mcs))
	mtw_write(sc, MTW_LG_FBK_CFG0,
	    OFDM(6) << 28 |	/* 54->48 */
	    OFDM(5) << 24 |	/* 48->36 */
	    OFDM(4) << 20 |	/* 36->24 */
	    OFDM(3) << 16 |	/* 24->18 */
	    OFDM(2) << 12 |	/* 18->12 */
	    OFDM(1) <<  8 |	/* 12-> 9 */
	    OFDM(0) <<  4 |	/*  9-> 6 */
	    OFDM(0));		/*  6-> 6 */

	mtw_write(sc, MTW_LG_FBK_CFG1,
	    CCK(2) << 12 |	/* 11->5.5 */
	    CCK(1) <<  8 |	/* 5.5-> 2 */
	    CCK(0) <<  4 |	/*   2-> 1 */
	    CCK(0));		/*   1-> 1 */
#undef OFDM
#undef CCK
}

void
mtw_set_txrts(struct mtw_softc *sc)
{
	uint32_t tmp;

	/* set RTS threshold */
	mtw_read(sc, MTW_TX_RTS_CFG, &tmp);
	tmp &= ~0xffff00;
	tmp |= 0x1000 << MTW_RTS_THRES_SHIFT;
	mtw_write(sc, MTW_TX_RTS_CFG, tmp);
}

void
mtw_set_txpreamble(struct mtw_softc *sc)
{
	uint32_t tmp;

	mtw_read(sc, MTW_AUTO_RSP_CFG, &tmp);
	if (sc->sc_ic.ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= MTW_CCK_SHORT_EN;
	else
		tmp &= ~MTW_CCK_SHORT_EN;
	mtw_write(sc, MTW_AUTO_RSP_CFG, tmp);
}

void
mtw_set_basicrates(struct mtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* set basic rates mask */
	if (ic->ic_curmode == IEEE80211_MODE_11B)
		mtw_write(sc, MTW_LEGACY_BASIC_RATE, 0x003);
	else if (ic->ic_curmode == IEEE80211_MODE_11A)
		mtw_write(sc, MTW_LEGACY_BASIC_RATE, 0x150);
	else	/* 11g */
		mtw_write(sc, MTW_LEGACY_BASIC_RATE, 0x17f);
}

void
mtw_set_leds(struct mtw_softc *sc, uint16_t which)
{
	struct mtw_mcu_cmd_8 cmd;

	cmd.func = htole32(0x1);
	cmd.val = htole32(which);
	mtw_mcu_cmd(sc, 16, &cmd, sizeof(struct mtw_mcu_cmd_8));
}

void
mtw_set_bssid(struct mtw_softc *sc, const uint8_t *bssid)
{
	mtw_write(sc, MTW_MAC_BSSID_DW0,
	    bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24);
	mtw_write(sc, MTW_MAC_BSSID_DW1,
	    bssid[4] | bssid[5] << 8);
}

void
mtw_set_macaddr(struct mtw_softc *sc, const uint8_t *addr)
{
	mtw_write(sc, MTW_MAC_ADDR_DW0,
	    addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	mtw_write(sc, MTW_MAC_ADDR_DW1,
	    addr[4] | addr[5] << 8 | 0xff << 16);
}

#if NBPFILTER > 0
int8_t
mtw_rssi2dbm(struct mtw_softc *sc, uint8_t rssi, uint8_t rxchain)
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

int
mt7601_bbp_init(struct mtw_softc *sc)
{
	uint8_t bbp;
	int i, error, ntries;

	/* wait for BBP to wake up */
	for (ntries = 0; ntries < 20; ntries++) {
		if ((error = mtw_bbp_read(sc, 0, &bbp)) != 0)
			return error;
		if (bbp != 0 && bbp != 0xff)
			break;
	}

	if (ntries == 20)
		return ETIMEDOUT;

	mtw_bbp_read(sc, 3, &bbp);
	mtw_bbp_write(sc, 3, 0);
	mtw_bbp_read(sc, 105, &bbp);
	mtw_bbp_write(sc, 105, 0);

	/* initialize BBP registers to default values */
	for (i = 0; i < nitems(mt7601_def_bbp); i++) {
		if ((error = mtw_bbp_write(sc, mt7601_def_bbp[i].reg,
		    mt7601_def_bbp[i].val)) != 0)
			return error;
	}

	sc->sc_bw_calibrated = 0;

	return 0;
}

int
mt7601_rf_init(struct mtw_softc *sc)
{
	int i, error;

	/* RF bank 0 */
	for (i = 0; i < nitems(mt7601_rf_bank0); i++) {
		error = mtw_rf_write(sc, 0, mt7601_rf_bank0[i].reg,
		    mt7601_rf_bank0[i].val);
		if (error != 0)
			return error;
	}
	/* RF bank 4 */
	for (i = 0; i < nitems(mt7601_rf_bank4); i++) {
		error = mtw_rf_write(sc, 4, mt7601_rf_bank4[i].reg,
		    mt7601_rf_bank4[i].val);
		if (error != 0)
			return error;
	}
	/* RF bank 5 */
	for (i = 0; i < nitems(mt7601_rf_bank5); i++) {
		error = mtw_rf_write(sc, 5, mt7601_rf_bank5[i].reg,
		    mt7601_rf_bank5[i].val);
		if (error != 0)
			return error;
	}
	return 0;
}

int
mt7601_rf_setup(struct mtw_softc *sc)
{
	uint32_t tmp;
	uint8_t rf;
	int error;

	if (sc->sc_rf_calibrated)
		return 0;

	/* init RF registers */
	if ((error = mt7601_rf_init(sc)) != 0)
		return error;

	/* init frequency offset */
	mtw_rf_write(sc, 0, 12, sc->rf_freq_offset);
	mtw_rf_read(sc, 0, 12, &rf);

	/* read temperature */
	mt7601_rf_temperature(sc, &rf);
	sc->bbp_temp = rf;
	DPRINTF(("BBP temp 0x%x ", rf));

	mtw_rf_read(sc, 0, 7, &rf);
	if ((error = mtw_mcu_calibrate(sc, 0x1, 0)) != 0)
		return error;
	usbd_delay_ms(sc->sc_udev, 100);
	mtw_rf_read(sc, 0, 7, &rf);

	/* Calibrate VCO RF 0/4 */
	mtw_rf_write(sc, 0, 4, 0x0a);
	mtw_rf_write(sc, 0, 4, 0x20);
	mtw_rf_read(sc, 0, 4, &rf);
	mtw_rf_write(sc, 0, 4, rf | 0x80);

	if ((error = mtw_mcu_calibrate(sc, 0x9, 0)) != 0)
		return error;
	if ((error = mt7601_rxdc_cal(sc)) != 0)
		return error;
	if ((error = mtw_mcu_calibrate(sc, 0x6, 1)) != 0)
		return error;
	if ((error = mtw_mcu_calibrate(sc, 0x6, 0)) != 0)
		return error;
	if ((error = mtw_mcu_calibrate(sc, 0x4, 0)) != 0)
		return error;
	if ((error = mtw_mcu_calibrate(sc, 0x5, 0)) != 0)
		return error;

	mtw_read(sc, MTW_LDO_CFG0, &tmp);
	tmp &= ~(1 << 4);
	tmp |= (1 << 2);
	mtw_write(sc, MTW_LDO_CFG0, tmp);

	if ((error = mtw_mcu_calibrate(sc, 0x8, 0)) != 0)
		return error;
	if ((error = mt7601_rxdc_cal(sc)) != 0)
		return error;

	sc->sc_rf_calibrated = 1;
	return 0;
}

int
mt7601_rf_temperature(struct mtw_softc *sc, int8_t *val)
{
	uint32_t rfb, rfs;
	uint8_t bbp;
	int ntries;

	mtw_read(sc, MTW_RF_BYPASS0, &rfb);
	mtw_read(sc, MTW_RF_SETTING0, &rfs);
	mtw_write(sc, MTW_RF_BYPASS0, 0);
	mtw_write(sc, MTW_RF_SETTING0, 0x10);
	mtw_write(sc, MTW_RF_BYPASS0, 0x10);

	mtw_bbp_read(sc, 47, &bbp);
	bbp &= ~0x7f;
	bbp |= 0x10;
	mtw_bbp_write(sc, 47, bbp);

	mtw_bbp_write(sc, 22, 0x40);

	for (ntries = 0; ntries < 10; ntries++) {
		mtw_bbp_read(sc, 47, &bbp);
		if ((bbp & 0x10) == 0)
			break;
	}
	if (ntries == 10)
		return ETIMEDOUT;

	mt7601_r49_read(sc, MT7601_R47_TEMP, val);

	mtw_bbp_write(sc, 22, 0);

	mtw_bbp_read(sc, 21, &bbp);
	bbp |= 0x02;
	mtw_bbp_write(sc, 21, bbp);
	bbp &= ~0x02;
	mtw_bbp_write(sc, 21, bbp);

	mtw_write(sc, MTW_RF_BYPASS0, 0);
	mtw_write(sc, MTW_RF_SETTING0, rfs);
	mtw_write(sc, MTW_RF_BYPASS0, rfb);
	return 0;
}

int
mt7601_r49_read(struct mtw_softc *sc, uint8_t flag, int8_t *val)
{
	uint8_t bbp;

	mtw_bbp_read(sc, 47, &bbp);
	bbp = 0x90;
	mtw_bbp_write(sc, 47, bbp);
	bbp &= ~0x0f;
	bbp |= flag;
	mtw_bbp_write(sc, 47, bbp);
	return mtw_bbp_read(sc, 49, val);
}

int
mt7601_rxdc_cal(struct mtw_softc *sc)
{
	uint32_t tmp;
	uint8_t bbp;
	int ntries;

	mtw_read(sc, MTW_MAC_SYS_CTRL, &tmp);
	mtw_write(sc, MTW_MAC_SYS_CTRL, MTW_MAC_RX_EN);
	mtw_bbp_write(sc, 158, 0x8d);
	mtw_bbp_write(sc, 159, 0xfc);
	mtw_bbp_write(sc, 158, 0x8c);
	mtw_bbp_write(sc, 159, 0x4c);

	for (ntries = 0; ntries < 20; ntries++) {
		DELAY(300);
		mtw_bbp_write(sc, 158, 0x8c);
		mtw_bbp_read(sc, 159, &bbp);
		if (bbp == 0x0c)
			break;
	}

	if (ntries == 20)
		return ETIMEDOUT;

	mtw_write(sc, MTW_MAC_SYS_CTRL, 0);
	mtw_bbp_write(sc, 158, 0x8d);
	mtw_bbp_write(sc, 159, 0xe0);
	mtw_write(sc, MTW_MAC_SYS_CTRL, tmp);
	return 0;
}

int
mtw_wlan_enable(struct mtw_softc *sc, int enable)
{
	uint32_t tmp;
	int error = 0;

	if (enable) {
		mtw_read(sc, MTW_WLAN_CTRL, &tmp);
		if (sc->asic_ver == 0x7612)
			tmp &= ~0xfffff000;
			
		tmp &= ~MTW_WLAN_CLK_EN;
		tmp |= MTW_WLAN_EN;
		mtw_write(sc, MTW_WLAN_CTRL, tmp);
		usbd_delay_ms(sc->sc_udev, 2);
		
		tmp |= MTW_WLAN_CLK_EN;
		if (sc->asic_ver == 0x7612) {
			tmp |= (MTW_WLAN_RESET | MTW_WLAN_RESET_RF);
		}
		mtw_write(sc, MTW_WLAN_CTRL, tmp);
		usbd_delay_ms(sc->sc_udev, 2);

		mtw_read(sc, MTW_OSC_CTRL, &tmp);
		tmp |= MTW_OSC_EN;
		mtw_write(sc, MTW_OSC_CTRL, tmp);
		tmp |= MTW_OSC_CAL_REQ;
		mtw_write(sc, MTW_OSC_CTRL, tmp);
	} else {
		mtw_read(sc, MTW_WLAN_CTRL, &tmp);
		tmp &= ~(MTW_WLAN_CLK_EN | MTW_WLAN_EN);
		mtw_write(sc, MTW_WLAN_CTRL, tmp);

		mtw_read(sc, MTW_OSC_CTRL, &tmp);
		tmp &= ~MTW_OSC_EN;
		mtw_write(sc, MTW_OSC_CTRL, tmp);
	}
	return error;
}

int
mtw_txrx_enable(struct mtw_softc *sc)
{
	uint32_t tmp;
	int error, ntries;

	mtw_write(sc, MTW_MAC_SYS_CTRL, MTW_MAC_TX_EN);
	for (ntries = 0; ntries < 200; ntries++) {
		if ((error = mtw_read(sc, MTW_WPDMA_GLO_CFG, &tmp)) != 0)
			return error;
		if ((tmp & (MTW_TX_DMA_BUSY | MTW_RX_DMA_BUSY)) == 0)
			break;
		DELAY(1000);
	}
	if (ntries == 200)
		return ETIMEDOUT;

	DELAY(50);

	tmp |= MTW_RX_DMA_EN | MTW_TX_DMA_EN | MTW_TX_WB_DDONE;
	mtw_write(sc, MTW_WPDMA_GLO_CFG, tmp);

	/* enable Rx bulk aggregation (set timeout and limit) */
	tmp = MTW_USB_TX_EN | MTW_USB_RX_EN | MTW_USB_RX_AGG_EN |
	   MTW_USB_RX_AGG_TO(128) | MTW_USB_RX_AGG_LMT(2);
	mtw_write(sc, MTW_USB_DMA_CFG, tmp);

	/* set Rx filter */
	tmp = MTW_DROP_CRC_ERR | MTW_DROP_PHY_ERR;
	if (sc->sc_ic.ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= MTW_DROP_UC_NOME | MTW_DROP_DUPL |
		    MTW_DROP_CTS | MTW_DROP_BA | MTW_DROP_ACK |
		    MTW_DROP_VER_ERR | MTW_DROP_CTRL_RSV |
		    MTW_DROP_CFACK | MTW_DROP_CFEND;
		if (sc->sc_ic.ic_opmode == IEEE80211_M_STA)
			tmp |= MTW_DROP_RTS | MTW_DROP_PSPOLL;
	}
	mtw_write(sc, MTW_RX_FILTR_CFG, tmp);

	mtw_write(sc, MTW_MAC_SYS_CTRL,
	    MTW_MAC_RX_EN | MTW_MAC_TX_EN);
	return 0;
}

int
mtw_init(struct ifnet *ifp)
{
	struct mtw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	int i, error, ridx, ntries, qid;

	if (usbd_is_dying(sc->sc_udev))
		return ENXIO;

	/* init Tx rings (4 EDCAs, 1 HCCA, 1 MGMT) */
	for (qid = 0; qid < MTW_TXQ_COUNT; qid++) {
		if ((error = mtw_alloc_tx_ring(sc, qid)) != 0)
			goto fail;
	}

	/* init Rx ring */
	if ((error = mtw_alloc_rx_ring(sc, 0)) != 0)
		goto fail;

	/* init host command ring */
	sc->cmdq.cur = sc->cmdq.next = sc->cmdq.queued = 0;

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_WPDMA_GLO_CFG, &tmp)) != 0)
			goto fail;
		if ((tmp & (MTW_TX_DMA_BUSY | MTW_RX_DMA_BUSY)) == 0)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for DMA engine\n",
		    sc->sc_dev.dv_xname);
		error = ETIMEDOUT;
		goto fail;
	}
	tmp &= 0xff0;
	tmp |= MTW_TX_WB_DDONE;
	mtw_write(sc, MTW_WPDMA_GLO_CFG, tmp);

	/* reset MAC and baseband */
	mtw_write(sc, MTW_MAC_SYS_CTRL, MTW_BBP_HRST | MTW_MAC_SRST);
	mtw_write(sc, MTW_USB_DMA_CFG, 0);
	mtw_write(sc, MTW_MAC_SYS_CTRL, 0);

	/* init MAC values */
	if (sc->mac_ver == 0x7601) {
		for (i = 0; i < nitems(mt7601_def_mac); i++)
			mtw_write(sc, mt7601_def_mac[i].reg,
			    mt7601_def_mac[i].val);
	}

	/* wait while MAC is busy */
	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_MAC_STATUS_REG, &tmp)) != 0)
			goto fail;
		if (!(tmp & (MTW_RX_STATUS_BUSY | MTW_TX_STATUS_BUSY)))
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		error = ETIMEDOUT;
		goto fail;
	}

	/* set MAC address */
	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	mtw_set_macaddr(sc, ic->ic_myaddr);

	/* clear WCID attribute table */
	mtw_set_region_4(sc, MTW_WCID_ATTR(0), 1, 8 * 32);

	mtw_write(sc, 0x1648, 0x00830083);
	mtw_read(sc, MTW_FCE_L2_STUFF, &tmp);
	tmp &= ~MTW_L2S_WR_MPDU_LEN_EN;
	mtw_write(sc, MTW_FCE_L2_STUFF, tmp);

	/* RTS config */
	mtw_set_txrts(sc);

	/* clear Host to MCU mailbox */
	mtw_write(sc, MTW_BBP_CSR, 0);
	mtw_write(sc, MTW_H2M_MAILBOX, 0);

	/* clear RX WCID search table */
	mtw_set_region_4(sc, MTW_WCID_ENTRY(0), 0xffffffff, 512);
	
	/* abort TSF synchronization */
	mtw_abort_tsf_sync(sc);

	mtw_read(sc, MTW_US_CYC_CNT, &tmp);
	tmp = (tmp & ~0xff);
	if (sc->mac_ver == 0x7601)
		tmp |= 0x1e;
	mtw_write(sc, MTW_US_CYC_CNT, tmp);

	/* clear shared key table */
	mtw_set_region_4(sc, MTW_SKEY(0, 0), 0, 8 * 32);

	/* clear IV/EIV table */
	mtw_set_region_4(sc, MTW_IVEIV(0), 0, 8 * 32);

	/* clear shared key mode */
	mtw_write(sc, MTW_SKEY_MODE_0_7, 0);
	mtw_write(sc, MTW_SKEY_MODE_8_15, 0);

	/* txop truncation */
	mtw_write(sc, MTW_TXOP_CTRL_CFG, 0x0000583f);

	/* init Tx power for all Tx rates */
	for (ridx = 0; ridx < 5; ridx++) {
		if (sc->txpow20mhz[ridx] == 0xffffffff)
			continue;
		mtw_write(sc, MTW_TX_PWR_CFG(ridx), sc->txpow20mhz[ridx]);
	}
	mtw_write(sc, MTW_TX_PWR_CFG7, 0);
	mtw_write(sc, MTW_TX_PWR_CFG9, 0);

	mtw_read(sc, MTW_CMB_CTRL, &tmp);
	tmp &= ~(1 << 18 | 1 << 14);
	mtw_write(sc, MTW_CMB_CTRL, tmp);

	/* clear USB DMA */
	mtw_write(sc, MTW_USB_DMA_CFG, MTW_USB_TX_EN | MTW_USB_RX_EN |
	    MTW_USB_RX_AGG_EN | MTW_USB_TX_CLEAR | MTW_USB_TXOP_HALT |
	    MTW_USB_RX_WL_DROP);
	usbd_delay_ms(sc->sc_udev, 50);
	mtw_read(sc, MTW_USB_DMA_CFG, &tmp);
	tmp &= ~(MTW_USB_TX_CLEAR | MTW_USB_TXOP_HALT |
	    MTW_USB_RX_WL_DROP);
	mtw_write(sc, MTW_USB_DMA_CFG, tmp);

	/* enable radio */
	mtw_mcu_radio(sc, 0x31, 0);

	/* init RF registers */
	if (sc->mac_ver == 0x7601)
		mt7601_rf_init(sc);

	/* init baseband registers */
	if (sc->mac_ver == 0x7601)
		error = mt7601_bbp_init(sc);

	if (error != 0) {
		printf("%s: could not initialize BBP\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	/* setup and calibrate RF */
	if (sc->mac_ver == 0x7601)
		error = mt7601_rf_setup(sc);

	if (error != 0) {
		printf("%s: could not initialize RF\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	/* select default channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	mtw_set_chan(sc, ic->ic_ibss_chan);

	for (i = 0; i < MTW_RX_RING_COUNT; i++) {
		struct mtw_rx_data *data = &sc->rxq[MTW_RXQ_WLAN].data[i];

		usbd_setup_xfer(data->xfer, sc->rxq[MTW_RXQ_WLAN].pipeh,
		    data, data->buf,
		    MTW_MAX_RXSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, mtw_rxeof);
		error = usbd_transfer(data->xfer);
		if (error != 0 && error != USBD_IN_PROGRESS)
			goto fail;
	}

	if ((error = mtw_txrx_enable(sc)) != 0)
		goto fail;

	/* init LEDs */
	mtw_set_leds(sc, MTW_LED_MODE_ON);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (ic->ic_flags & IEEE80211_F_WEPON) {
		/* install WEP keys */
		for (i = 0; i < IEEE80211_WEP_NKID; i++) {
			if (ic->ic_nw_keys[i].k_cipher != IEEE80211_CIPHER_NONE)
				(void)mtw_set_key(ic, NULL, &ic->ic_nw_keys[i]);
		}
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	if (error != 0)
fail:	    mtw_stop(ifp, 1);
	return error;
}

void
mtw_stop(struct ifnet *ifp, int disable)
{
	struct mtw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	int s, ntries, error, qid;

	if (ifp->if_flags & IFF_RUNNING)
		mtw_set_leds(sc, MTW_LED_MODE_ON);

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_del(&sc->scan_to);
	timeout_del(&sc->calib_to);

	s = splusb();	
	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	/* wait for all queued asynchronous commands to complete */
	usb_wait_task(sc->sc_udev, &sc->sc_task);
	splx(s);

	/* Disable Tx/Rx DMA. */
	mtw_read(sc, MTW_WPDMA_GLO_CFG, &tmp);
	tmp &= ~(MTW_RX_DMA_EN | MTW_TX_DMA_EN);
	mtw_write(sc, MTW_WPDMA_GLO_CFG, tmp);
	mtw_usb_dma_write(sc, 0);

	for (ntries = 0; ntries < 100; ntries++) {
		if (mtw_read(sc, MTW_WPDMA_GLO_CFG, &tmp) != 0)
			break;
		if ((tmp & (MTW_TX_DMA_BUSY | MTW_RX_DMA_BUSY)) == 0)
			break;
		DELAY(10);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for DMA engine\n",
		    sc->sc_dev.dv_xname);
	}

	/* stop MAC Tx/Rx */
	mtw_read(sc, MTW_MAC_SYS_CTRL, &tmp);
	tmp &= ~(MTW_MAC_RX_EN | MTW_MAC_TX_EN);
	mtw_write(sc, MTW_MAC_SYS_CTRL, tmp);

	/* disable RTS retry */
	mtw_read(sc, MTW_TX_RTS_CFG, &tmp);
	tmp &= ~0xff;
	mtw_write(sc, MTW_TX_RTS_CFG, tmp);

	/* US_CYC_CFG */
	mtw_read(sc, MTW_US_CYC_CNT, &tmp);
	tmp = (tmp & ~0xff);
	mtw_write(sc, MTW_US_CYC_CNT, tmp);

	/* stop PBF */
	mtw_read(sc, MTW_PBF_CFG, &tmp);
	tmp &= ~0x3;
	mtw_write(sc, MTW_PBF_CFG, tmp);

	/* wait for pending Tx to complete */
	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_TXRXQ_PCNT, &tmp)) != 0)
			break;
		if ((tmp & MTW_TX2Q_PCNT_MASK) == 0)
			break;
	}
	DELAY(1000);

	/* delete keys */
	for (qid = 0; qid < 4; qid++) {
		mtw_read(sc, MTW_SKEY_MODE_0_7, &tmp);
		tmp &= ~(0xf << qid * 4);
		mtw_write(sc, MTW_SKEY_MODE_0_7, tmp);
	}

	if (disable) {
		/* disable radio */
		error = mtw_mcu_radio(sc, 0x30, 0x1);
		usbd_delay_ms(sc->sc_udev, 10);
	}

	/* free Tx and Rx rings */
	sc->qfullmsk = 0;
	for (qid = 0; qid < MTW_TXQ_COUNT; qid++)
		mtw_free_tx_ring(sc, qid);
	mtw_free_rx_ring(sc, 0);
}

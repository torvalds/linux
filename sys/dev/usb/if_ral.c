/*	$OpenBSD: if_ral.c,v 1.150 2024/05/23 03:21:08 jsg Exp $	*/

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
 * Ralink Technology RT2500USB chipset driver
 * http://www.ralinktech.com.tw/
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
#include <net80211/ieee80211_radiotap.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_ralreg.h>
#include <dev/usb/if_ralvar.h>

#ifdef URAL_DEBUG
#define DPRINTF(x)	do { if (ural_debug) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (ural_debug >= (n)) printf x; } while (0)
int ural_debug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

/* various supported device vendors/products */
static const struct usb_devno ural_devs[] = {
	{ USB_VENDOR_ASUS,		USB_PRODUCT_ASUS_RT2570 },
	{ USB_VENDOR_ASUS,		USB_PRODUCT_ASUS_RT2570_2 },
	{ USB_VENDOR_BELKIN,		USB_PRODUCT_BELKIN_F5D7050 },
	{ USB_VENDOR_CISCOLINKSYS,	USB_PRODUCT_CISCOLINKSYS_WUSB54G },
	{ USB_VENDOR_CISCOLINKSYS,	USB_PRODUCT_CISCOLINKSYS_WUSB54GP },
	{ USB_VENDOR_CISCOLINKSYS,	USB_PRODUCT_CISCOLINKSYS_HU200TS },
	{ USB_VENDOR_CONCEPTRONIC2,	USB_PRODUCT_CONCEPTRONIC2_C54RU },
	{ USB_VENDOR_DLINK,		USB_PRODUCT_DLINK_RT2570 },
	{ USB_VENDOR_GIGABYTE,		USB_PRODUCT_GIGABYTE_GNWBKG },
	{ USB_VENDOR_GUILLEMOT,		USB_PRODUCT_GUILLEMOT_HWGUSB254 },
	{ USB_VENDOR_MELCO,		USB_PRODUCT_MELCO_KG54 },
	{ USB_VENDOR_MELCO,		USB_PRODUCT_MELCO_KG54AI },
	{ USB_VENDOR_MELCO,		USB_PRODUCT_MELCO_KG54YB },
	{ USB_VENDOR_MELCO,		USB_PRODUCT_MELCO_NINWIFI },
	{ USB_VENDOR_MSI,		USB_PRODUCT_MSI_RT2570 },
	{ USB_VENDOR_MSI,		USB_PRODUCT_MSI_RT2570_2 },
	{ USB_VENDOR_MSI,		USB_PRODUCT_MSI_RT2570_3 },
	{ USB_VENDOR_NOVATECH,		USB_PRODUCT_NOVATECH_NV902W },
	{ USB_VENDOR_RALINK,		USB_PRODUCT_RALINK_RT2570 },
	{ USB_VENDOR_RALINK,		USB_PRODUCT_RALINK_RT2570_2 },
	{ USB_VENDOR_RALINK,		USB_PRODUCT_RALINK_RT2570_3 },
	{ USB_VENDOR_SPHAIRON,		USB_PRODUCT_SPHAIRON_UB801R },
	{ USB_VENDOR_SURECOM,		USB_PRODUCT_SURECOM_RT2570 },
	{ USB_VENDOR_VTECH,		USB_PRODUCT_VTECH_RT2570 },
	{ USB_VENDOR_ZINWELL,		USB_PRODUCT_ZINWELL_RT2570 }
};

int		ural_alloc_tx_list(struct ural_softc *);
void		ural_free_tx_list(struct ural_softc *);
int		ural_alloc_rx_list(struct ural_softc *);
void		ural_free_rx_list(struct ural_softc *);
int		ural_media_change(struct ifnet *);
void		ural_next_scan(void *);
void		ural_task(void *);
int		ural_newstate(struct ieee80211com *, enum ieee80211_state,
		    int);
void		ural_txeof(struct usbd_xfer *, void *, usbd_status);
void		ural_rxeof(struct usbd_xfer *, void *, usbd_status);
#if NBPFILTER > 0
uint8_t		ural_rxrate(const struct ural_rx_desc *);
#endif
int		ural_ack_rate(struct ieee80211com *, int);
uint16_t	ural_txtime(int, int, uint32_t);
uint8_t		ural_plcp_signal(int);
void		ural_setup_tx_desc(struct ural_softc *, struct ural_tx_desc *,
		    uint32_t, int, int);
#ifndef IEEE80211_STA_ONLY
int		ural_tx_bcn(struct ural_softc *, struct mbuf *,
		    struct ieee80211_node *);
#endif
int		ural_tx_data(struct ural_softc *, struct mbuf *,
		    struct ieee80211_node *);
void		ural_start(struct ifnet *);
void		ural_watchdog(struct ifnet *);
int		ural_ioctl(struct ifnet *, u_long, caddr_t);
void		ural_eeprom_read(struct ural_softc *, uint16_t, void *, int);
uint16_t	ural_read(struct ural_softc *, uint16_t);
void		ural_read_multi(struct ural_softc *, uint16_t, void *, int);
void		ural_write(struct ural_softc *, uint16_t, uint16_t);
void		ural_write_multi(struct ural_softc *, uint16_t, void *, int);
void		ural_bbp_write(struct ural_softc *, uint8_t, uint8_t);
uint8_t		ural_bbp_read(struct ural_softc *, uint8_t);
void		ural_rf_write(struct ural_softc *, uint8_t, uint32_t);
void		ural_set_chan(struct ural_softc *, struct ieee80211_channel *);
void		ural_disable_rf_tune(struct ural_softc *);
void		ural_enable_tsf_sync(struct ural_softc *);
void		ural_update_slot(struct ural_softc *);
void		ural_set_txpreamble(struct ural_softc *);
void		ural_set_basicrates(struct ural_softc *);
void		ural_set_bssid(struct ural_softc *, const uint8_t *);
void		ural_set_macaddr(struct ural_softc *, const uint8_t *);
void		ural_update_promisc(struct ural_softc *);
const char	*ural_get_rf(int);
void		ural_read_eeprom(struct ural_softc *);
int		ural_bbp_init(struct ural_softc *);
void		ural_set_txantenna(struct ural_softc *, int);
void		ural_set_rxantenna(struct ural_softc *, int);
int		ural_init(struct ifnet *);
void		ural_stop(struct ifnet *, int);
void		ural_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);
void		ural_amrr_start(struct ural_softc *, struct ieee80211_node *);
void		ural_amrr_timeout(void *);
void		ural_amrr_update(struct usbd_xfer *, void *,
		    usbd_status status);

static const struct {
	uint16_t	reg;
	uint16_t	val;
} ural_def_mac[] = {
	RAL_DEF_MAC
};

static const struct {
	uint8_t	reg;
	uint8_t	val;
} ural_def_bbp[] = {
	RAL_DEF_BBP
};

static const uint32_t ural_rf2522_r2[] =    RAL_RF2522_R2;
static const uint32_t ural_rf2523_r2[] =    RAL_RF2523_R2;
static const uint32_t ural_rf2524_r2[] =    RAL_RF2524_R2;
static const uint32_t ural_rf2525_r2[] =    RAL_RF2525_R2;
static const uint32_t ural_rf2525_hi_r2[] = RAL_RF2525_HI_R2;
static const uint32_t ural_rf2525e_r2[] =   RAL_RF2525E_R2;
static const uint32_t ural_rf2526_hi_r2[] = RAL_RF2526_HI_R2;
static const uint32_t ural_rf2526_r2[] =    RAL_RF2526_R2;

int ural_match(struct device *, void *, void *);
void ural_attach(struct device *, struct device *, void *);
int ural_detach(struct device *, int);

struct cfdriver ural_cd = {
	NULL, "ural", DV_IFNET
};

const struct cfattach ural_ca = {
	sizeof(struct ural_softc), ural_match, ural_attach, ural_detach
};

int
ural_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->configno != RAL_CONFIG_NO || uaa->ifaceno != RAL_IFACE_NO)
		return UMATCH_NONE;

	return (usb_lookup(ural_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
ural_attach(struct device *parent, struct device *self, void *aux)
{
	struct ural_softc *sc = (struct ural_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i;

	sc->sc_udev = uaa->device;
	sc->sc_iface = uaa->iface;

	/*
	 * Find endpoints.
	 */
	id = usbd_get_interface_descriptor(sc->sc_iface);

	sc->sc_rx_no = sc->sc_tx_no = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for iface %d\n",
			    sc->sc_dev.dv_xname, i);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			sc->sc_rx_no = ed->bEndpointAddress;
		else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			sc->sc_tx_no = ed->bEndpointAddress;
	}
	if (sc->sc_rx_no == -1 || sc->sc_tx_no == -1) {
		printf("%s: missing endpoint\n", sc->sc_dev.dv_xname);
		return;
	}

	usb_init_task(&sc->sc_task, ural_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->scan_to, ural_next_scan, sc);

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 10;
	timeout_set(&sc->amrr_to, ural_amrr_timeout, sc);

	/* retrieve RT2570 rev. no */
	sc->asic_rev = ural_read(sc, RAL_MAC_CSR0);

	/* retrieve MAC address and various other things from EEPROM */
	ural_read_eeprom(sc);

	printf("%s: MAC/BBP RT%04x (rev 0x%02x), RF %s, address %s\n",
	    sc->sc_dev.dv_xname, sc->macbbp_rev, sc->asic_rev,
	    ural_get_rf(sc->rf_rev), ether_sprintf(ic->ic_myaddr));

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
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
	ifp->if_ioctl = ural_ioctl;
	ifp->if_start = ural_start;
	ifp->if_watchdog = ural_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_newassoc = ural_newassoc;

	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = ural_newstate;
	ieee80211_media_init(ifp, ural_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + 64);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(RAL_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(RAL_TX_RADIOTAP_PRESENT);
#endif
}

int
ural_detach(struct device *self, int flags)
{
	struct ural_softc *sc = (struct ural_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s;

	s = splusb();

	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
	if (timeout_initialized(&sc->amrr_to))
		timeout_del(&sc->amrr_to);

	usb_rem_wait_task(sc->sc_udev, &sc->sc_task);

	usbd_ref_wait(sc->sc_udev);

	if (ifp->if_softc != NULL) {
		ieee80211_ifdetach(ifp);	/* free all nodes */
		if_detach(ifp);
	}

	if (sc->amrr_xfer != NULL) {
		usbd_free_xfer(sc->amrr_xfer);
		sc->amrr_xfer = NULL;
	}

	if (sc->sc_rx_pipeh != NULL)
		usbd_close_pipe(sc->sc_rx_pipeh);

	if (sc->sc_tx_pipeh != NULL)
		usbd_close_pipe(sc->sc_tx_pipeh);

	ural_free_rx_list(sc);
	ural_free_tx_list(sc);

	splx(s);

	return 0;
}

int
ural_alloc_tx_list(struct ural_softc *sc)
{
	int i, error;

	sc->tx_cur = sc->tx_queued = 0;

	for (i = 0; i < RAL_TX_LIST_COUNT; i++) {
		struct ural_tx_data *data = &sc->tx_data[i];

		data->sc = sc;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate tx xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		data->buf = usbd_alloc_buffer(data->xfer,
		    RAL_TX_DESC_SIZE + IEEE80211_MAX_LEN);
		if (data->buf == NULL) {
			printf("%s: could not allocate tx buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
	}

	return 0;

fail:	ural_free_tx_list(sc);
	return error;
}

void
ural_free_tx_list(struct ural_softc *sc)
{
	int i;

	for (i = 0; i < RAL_TX_LIST_COUNT; i++) {
		struct ural_tx_data *data = &sc->tx_data[i];

		if (data->xfer != NULL) {
			usbd_free_xfer(data->xfer);
			data->xfer = NULL;
		}
		/*
		 * The node has already been freed at that point so don't call
		 * ieee80211_release_node() here.
		 */
		data->ni = NULL;
	}
}

int
ural_alloc_rx_list(struct ural_softc *sc)
{
	int i, error;

	for (i = 0; i < RAL_RX_LIST_COUNT; i++) {
		struct ural_rx_data *data = &sc->rx_data[i];

		data->sc = sc;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate rx xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		if (usbd_alloc_buffer(data->xfer, MCLBYTES) == NULL) {
			printf("%s: could not allocate rx buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
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
		data->buf = mtod(data->m, uint8_t *);
	}

	return 0;

fail:	ural_free_rx_list(sc);
	return error;
}

void
ural_free_rx_list(struct ural_softc *sc)
{
	int i;

	for (i = 0; i < RAL_RX_LIST_COUNT; i++) {
		struct ural_rx_data *data = &sc->rx_data[i];

		if (data->xfer != NULL) {
			usbd_free_xfer(data->xfer);
			data->xfer = NULL;
		}
		if (data->m != NULL) {
			m_freem(data->m);
			data->m = NULL;
		}
	}
}

int
ural_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		error = ural_init(ifp);

	return error;
}

/*
 * This function is called periodically (every 200ms) during scanning to
 * switch from one channel to another.
 */
void
ural_next_scan(void *arg)
{
	struct ural_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);

	usbd_ref_decr(sc->sc_udev);
}

void
ural_task(void *arg)
{
	struct ural_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state ostate;
	struct ieee80211_node *ni;

	if (usbd_is_dying(sc->sc_udev))
		return;

	ostate = ic->ic_state;

	switch (sc->sc_state) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* abort TSF synchronization */
			ural_write(sc, RAL_TXRX_CSR19, 0);

			/* force tx led to stop blinking */
			ural_write(sc, RAL_MAC_CSR20, 0);
		}
		break;

	case IEEE80211_S_SCAN:
		ural_set_chan(sc, ic->ic_bss->ni_chan);
		if (!usbd_is_dying(sc->sc_udev))
			timeout_add_msec(&sc->scan_to, 200);
		break;

	case IEEE80211_S_AUTH:
		ural_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_ASSOC:
		ural_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_RUN:
		ural_set_chan(sc, ic->ic_bss->ni_chan);

		ni = ic->ic_bss;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			ural_update_slot(sc);
			ural_set_txpreamble(sc);
			ural_set_basicrates(sc);
			ural_set_bssid(sc, ni->ni_bssid);
		}

#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS) {
			struct mbuf *m = ieee80211_beacon_alloc(ic, ni);
			if (m == NULL) {
				printf("%s: could not allocate beacon\n",
				    sc->sc_dev.dv_xname);
				return;
			}

			if (ural_tx_bcn(sc, m, ni) != 0) {
				m_freem(m);
				printf("%s: could not transmit beacon\n",
				    sc->sc_dev.dv_xname);
				return;
			}

			/* beacon is no longer needed */
			m_freem(m);
		}
#endif

		/* make tx led blink on tx (controlled by ASIC) */
		ural_write(sc, RAL_MAC_CSR20, 1);

		if (ic->ic_opmode != IEEE80211_M_MONITOR)
			ural_enable_tsf_sync(sc);

		if (ic->ic_opmode == IEEE80211_M_STA) {
			/* fake a join to init the tx rate */
			ural_newassoc(ic, ic->ic_bss, 1);

			/* enable automatic rate control in STA mode */
			if (ic->ic_fixed_rate == -1)
				ural_amrr_start(sc, ic->ic_bss);
		}

		break;
	}

	sc->sc_newstate(ic, sc->sc_state, sc->sc_arg);
}

int
ural_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ural_softc *sc = ic->ic_if.if_softc;

	usb_rem_task(sc->sc_udev, &sc->sc_task);
	timeout_del(&sc->scan_to);
	timeout_del(&sc->amrr_to);

	/* do it in a process context */
	sc->sc_state = nstate;
	sc->sc_arg = arg;
	usb_add_task(sc->sc_udev, &sc->sc_task);
	return 0;
}

/* quickly determine if a given rate is CCK or OFDM */
#define RAL_RATE_IS_OFDM(rate) ((rate) >= 12 && (rate) != 22)

#define RAL_ACK_SIZE	14	/* 10 + 4(FCS) */
#define RAL_CTS_SIZE	14	/* 10 + 4(FCS) */

#define RAL_SIFS		10	/* us */

#define RAL_RXTX_TURNAROUND	5	/* us */

void
ural_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ural_tx_data *data = priv;
	struct ural_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		printf("%s: could not transmit buffer: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(status));

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_tx_pipeh);

		ifp->if_oerrors++;
		return;
	}

	s = splnet();

	ieee80211_release_node(ic, data->ni);
	data->ni = NULL;

	sc->tx_queued--;

	DPRINTFN(10, ("tx done\n"));

	sc->sc_tx_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);
	ural_start(ifp);

	splx(s);
}

void
ural_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ural_rx_data *data = priv;
	struct ural_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	const struct ural_rx_desc *desc;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct mbuf *mnew, *m;
	int s, len;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_rx_pipeh);
		goto skip;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	if (len < RAL_RX_DESC_SIZE + IEEE80211_MIN_LEN) {
		DPRINTF(("%s: xfer too short %d\n", sc->sc_dev.dv_xname,
		    len));
		ifp->if_ierrors++;
		goto skip;
	}

	/* rx descriptor is located at the end */
	desc = (struct ural_rx_desc *)(data->buf + len - RAL_RX_DESC_SIZE);

	if (letoh32(desc->flags) & (RAL_RX_PHY_ERROR | RAL_RX_CRC_ERROR)) {
		/*
		 * This should not happen since we did not request to receive
		 * those frames when we filled RAL_TXRX_CSR2.
		 */
		DPRINTFN(5, ("PHY or CRC error\n"));
		ifp->if_ierrors++;
		goto skip;
	}

	MGETHDR(mnew, M_DONTWAIT, MT_DATA);
	if (mnew == NULL) {
		printf("%s: could not allocate rx mbuf\n",
		    sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		goto skip;
	}
	MCLGET(mnew, M_DONTWAIT);
	if (!(mnew->m_flags & M_EXT)) {
		printf("%s: could not allocate rx mbuf cluster\n",
		    sc->sc_dev.dv_xname);
		m_freem(mnew);
		ifp->if_ierrors++;
		goto skip;
	}
	m = data->m;
	data->m = mnew;
	data->buf = mtod(data->m, uint8_t *);

	/* finalize mbuf */
	m->m_pkthdr.len = m->m_len = (letoh32(desc->flags) >> 16) & 0xfff;

	s = splnet();

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct ural_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = IEEE80211_RADIOTAP_F_FCS;
		tap->wr_rate = ural_rxrate(desc);
		tap->wr_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		tap->wr_antenna = sc->rx_ant;
		tap->wr_antsignal = desc->rssi;

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_rxtap_len;
		mb.m_next = m;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_IN);
	}
#endif
	m_adj(m, -IEEE80211_CRC_LEN);	/* trim FCS */

	wh = mtod(m, struct ieee80211_frame *);
	ni = ieee80211_find_rxnode(ic, wh);

	/* send the frame to the 802.11 layer */
	memset(&rxi, 0, sizeof(rxi));
	rxi.rxi_rssi = desc->rssi;
	ieee80211_input(ifp, m, ni, &rxi);

	/* node is no longer needed */
	ieee80211_release_node(ic, ni);

	splx(s);

	DPRINTFN(15, ("rx done\n"));

skip:	/* setup a new transfer */
	usbd_setup_xfer(xfer, sc->sc_rx_pipeh, data, data->buf, MCLBYTES,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, ural_rxeof);
	(void)usbd_transfer(xfer);
}

/*
 * This function is only used by the Rx radiotap code. It returns the rate at
 * which a given frame was received.
 */
#if NBPFILTER > 0
uint8_t
ural_rxrate(const struct ural_rx_desc *desc)
{
	if (letoh32(desc->flags) & RAL_RX_OFDM) {
		/* reverse function of ural_plcp_signal */
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
ural_ack_rate(struct ieee80211com *ic, int rate)
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
ural_txtime(int len, int rate, uint32_t flags)
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
ural_plcp_signal(int rate)
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
ural_setup_tx_desc(struct ural_softc *sc, struct ural_tx_desc *desc,
    uint32_t flags, int len, int rate)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t plcp_length;
	int remainder;

	desc->flags = htole32(flags);
	desc->flags |= htole32(len << 16);

	desc->wme = htole16(
	    RAL_AIFSN(2) |
	    RAL_LOGCWMIN(3) |
	    RAL_LOGCWMAX(5));

	/* setup PLCP fields */
	desc->plcp_signal  = ural_plcp_signal(rate);
	desc->plcp_service = 4;

	len += IEEE80211_CRC_LEN;
	if (RAL_RATE_IS_OFDM(rate)) {
		desc->flags |= htole32(RAL_TX_OFDM);

		plcp_length = len & 0xfff;
		desc->plcp_length_hi = plcp_length >> 6;
		desc->plcp_length_lo = plcp_length & 0x3f;
	} else {
		plcp_length = (16 * len + rate - 1) / rate;
		if (rate == 22) {
			remainder = (16 * len) % 22;
			if (remainder != 0 && remainder < 7)
				desc->plcp_service |= RAL_PLCP_LENGEXT;
		}
		desc->plcp_length_hi = plcp_length >> 8;
		desc->plcp_length_lo = plcp_length & 0xff;

		if (rate != 2 && (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			desc->plcp_signal |= 0x08;
	}

	desc->iv = 0;
	desc->eiv = 0;
}

#define RAL_TX_TIMEOUT	5000

#ifndef IEEE80211_STA_ONLY
int
ural_tx_bcn(struct ural_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ural_tx_desc *desc;
	struct usbd_xfer *xfer;
	usbd_status error;
	uint8_t cmd = 0;
	uint8_t *buf;
	int xferlen, rate = 2;

	xfer = usbd_alloc_xfer(sc->sc_udev);
	if (xfer == NULL)
		return ENOMEM;

	/* xfer length needs to be a multiple of two! */
	xferlen = (RAL_TX_DESC_SIZE + m0->m_pkthdr.len + 1) & ~1;

	buf = usbd_alloc_buffer(xfer, xferlen);
	if (buf == NULL) {
		usbd_free_xfer(xfer);
		return ENOMEM;
	}

	usbd_setup_xfer(xfer, sc->sc_tx_pipeh, NULL, &cmd, sizeof cmd,
	    USBD_FORCE_SHORT_XFER | USBD_SYNCHRONOUS, RAL_TX_TIMEOUT, NULL);

	error = usbd_transfer(xfer);
	if (error != 0) {
		usbd_free_xfer(xfer);
		return error;
	}

	desc = (struct ural_tx_desc *)buf;

	m_copydata(m0, 0, m0->m_pkthdr.len, buf + RAL_TX_DESC_SIZE);
	ural_setup_tx_desc(sc, desc, RAL_TX_IFS_NEWBACKOFF | RAL_TX_TIMESTAMP,
	    m0->m_pkthdr.len, rate);

	DPRINTFN(10, ("sending beacon frame len=%u rate=%u xfer len=%u\n",
	    m0->m_pkthdr.len, rate, xferlen));

	usbd_setup_xfer(xfer, sc->sc_tx_pipeh, NULL, buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY | USBD_SYNCHRONOUS,
	    RAL_TX_TIMEOUT, NULL);

	error = usbd_transfer(xfer);
	usbd_free_xfer(xfer);

	return error;
}
#endif

int
ural_tx_data(struct ural_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ural_tx_desc *desc;
	struct ural_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	uint32_t flags = RAL_TX_NEWSEQ;
	uint16_t dur;
	usbd_status error;
	int rate, xferlen, pktlen, needrts = 0, needcts = 0;

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
		uint16_t dur;

		protrate = 2;
		ackrate  = ural_ack_rate(ic, rate);

		dur = ural_txtime(pktlen, rate, ic->ic_flags) +
		      ural_txtime(RAL_ACK_SIZE, ackrate, ic->ic_flags) +
		      2 * RAL_SIFS;
		if (needrts) {
			dur += ural_txtime(RAL_CTS_SIZE, ural_ack_rate(ic,
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

		data = &sc->tx_data[sc->tx_cur];
		desc = (struct ural_tx_desc *)data->buf;

		/* avoid multiple free() of the same node for each fragment */
		data->ni = ieee80211_ref_node(ni);

		m_copydata(mprot, 0, mprot->m_pkthdr.len,
		    data->buf + RAL_TX_DESC_SIZE);
		ural_setup_tx_desc(sc, desc,
		    (needrts ? RAL_TX_NEED_ACK : 0) | RAL_TX_RETRY(7),
		    mprot->m_pkthdr.len, protrate);

		/* no roundup necessary here */
		xferlen = RAL_TX_DESC_SIZE + mprot->m_pkthdr.len;

		/* XXX may want to pass the protection frame to BPF */

		/* mbuf is no longer needed */
		m_freem(mprot);

		usbd_setup_xfer(data->xfer, sc->sc_tx_pipeh, data, data->buf,
		    xferlen, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
		    RAL_TX_TIMEOUT, ural_txeof);
		error = usbd_transfer(data->xfer);
		if (error != 0 && error != USBD_IN_PROGRESS) {
			m_freem(m0);
			return error;
		}

		sc->tx_queued++;
		sc->tx_cur = (sc->tx_cur + 1) % RAL_TX_LIST_COUNT;

		flags |= RAL_TX_IFS_SIFS;
	}

	data = &sc->tx_data[sc->tx_cur];
	desc = (struct ural_tx_desc *)data->buf;

	data->ni = ni;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RAL_TX_NEED_ACK;
		flags |= RAL_TX_RETRY(7);

		dur = ural_txtime(RAL_ACK_SIZE, ural_ack_rate(ic, rate),
		    ic->ic_flags) + RAL_SIFS;
		*(uint16_t *)wh->i_dur = htole16(dur);

#ifndef IEEE80211_STA_ONLY
		/* tell hardware to set timestamp in probe responses */
		if ((wh->i_fc[0] &
		    (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
		    (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP))
			flags |= RAL_TX_TIMESTAMP;
#endif
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct ural_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		tap->wt_antenna = sc->tx_ant;

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_txtap_len;
		mb.m_next = m0;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_OUT);
	}
#endif

	m_copydata(m0, 0, m0->m_pkthdr.len, data->buf + RAL_TX_DESC_SIZE);
	ural_setup_tx_desc(sc, desc, flags, m0->m_pkthdr.len, rate);

	/* align end on a 2-bytes boundary */
	xferlen = (RAL_TX_DESC_SIZE + m0->m_pkthdr.len + 1) & ~1;

	/*
	 * No space left in the last URB to store the extra 2 bytes, force
	 * sending of another URB.
	 */
	if ((xferlen % 64) == 0)
		xferlen += 2;

	DPRINTFN(10, ("sending frame len=%u rate=%u xfer len=%u\n",
	    m0->m_pkthdr.len, rate, xferlen));

	/* mbuf is no longer needed */
	m_freem(m0);

	usbd_setup_xfer(data->xfer, sc->sc_tx_pipeh, data, data->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, RAL_TX_TIMEOUT, ural_txeof);
	error = usbd_transfer(data->xfer);
	if (error != 0 && error != USBD_IN_PROGRESS)
		return error;

	sc->tx_queued++;
	sc->tx_cur = (sc->tx_cur + 1) % RAL_TX_LIST_COUNT;

	return 0;
}

void
ural_start(struct ifnet *ifp)
{
	struct ural_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m0;

	/*
	 * net80211 may still try to send management frames even if the
	 * IFF_RUNNING flag is not set...
	 */
	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->tx_queued >= RAL_TX_LIST_COUNT - 1) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m0 = mq_dequeue(&ic->ic_mgtq);
		if (m0 != NULL) {
			ni = m0->m_pkthdr.ph_cookie;
#if NBPFILTER > 0
			if (ic->ic_rawbpf != NULL)
				bpf_mtap(ic->ic_rawbpf, m0, BPF_DIRECTION_OUT);
#endif
			if (ural_tx_data(sc, m0, ni) != 0)
				break;

		} else {
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
			if (ural_tx_data(sc, m0, ni) != 0) {
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
ural_watchdog(struct ifnet *ifp)
{
	struct ural_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			/*ural_init(ifp); XXX needs a process context! */
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
ural_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ural_softc *sc = ifp->if_softc;
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
			if (ifp->if_flags & IFF_RUNNING)
				ural_update_promisc(sc);
			else
				ural_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ural_stop(ifp, 1);
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
				ural_set_chan(sc, ic->ic_ibss_chan);
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			ural_init(ifp);
		error = 0;
	}

	splx(s);

	usbd_ref_decr(sc->sc_udev);

	return error;
}

void
ural_eeprom_read(struct ural_softc *sc, uint16_t addr, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RAL_READ_EEPROM;
	USETW(req.wValue, 0);
	USETW(req.wIndex, addr);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		printf("%s: could not read EEPROM: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
	}
}

uint16_t
ural_read(struct ural_softc *sc, uint16_t reg)
{
	usb_device_request_t req;
	usbd_status error;
	uint16_t val;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RAL_READ_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, sizeof (uint16_t));

	error = usbd_do_request(sc->sc_udev, &req, &val);
	if (error != 0) {
		printf("%s: could not read MAC register: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		return 0;
	}
	return letoh16(val);
}

void
ural_read_multi(struct ural_softc *sc, uint16_t reg, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RAL_READ_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		printf("%s: could not read MAC register: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
	}
}

void
ural_write(struct ural_softc *sc, uint16_t reg, uint16_t val)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RAL_WRITE_MAC;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);

	error = usbd_do_request(sc->sc_udev, &req, NULL);
	if (error != 0) {
		printf("%s: could not write MAC register: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
	}
}

void
ural_write_multi(struct ural_softc *sc, uint16_t reg, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RAL_WRITE_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		printf("%s: could not write MAC register: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
	}
}

void
ural_bbp_write(struct ural_softc *sc, uint8_t reg, uint8_t val)
{
	uint16_t tmp;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(ural_read(sc, RAL_PHY_CSR8) & RAL_BBP_BUSY))
			break;
	}
	if (ntries == 5) {
		printf("%s: could not write to BBP\n", sc->sc_dev.dv_xname);
		return;
	}

	tmp = reg << 8 | val;
	ural_write(sc, RAL_PHY_CSR7, tmp);
}

uint8_t
ural_bbp_read(struct ural_softc *sc, uint8_t reg)
{
	uint16_t val;
	int ntries;

	val = RAL_BBP_WRITE | reg << 8;
	ural_write(sc, RAL_PHY_CSR7, val);

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(ural_read(sc, RAL_PHY_CSR8) & RAL_BBP_BUSY))
			break;
	}
	if (ntries == 5) {
		printf("%s: could not read BBP\n", sc->sc_dev.dv_xname);
		return 0;
	}
	return ural_read(sc, RAL_PHY_CSR7) & 0xff;
}

void
ural_rf_write(struct ural_softc *sc, uint8_t reg, uint32_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(ural_read(sc, RAL_PHY_CSR10) & RAL_RF_LOBUSY))
			break;
	}
	if (ntries == 5) {
		printf("%s: could not write to RF\n", sc->sc_dev.dv_xname);
		return;
	}

	tmp = RAL_RF_BUSY | RAL_RF_20BIT | (val & 0xfffff) << 2 | (reg & 0x3);
	ural_write(sc, RAL_PHY_CSR9,  tmp & 0xffff);
	ural_write(sc, RAL_PHY_CSR10, tmp >> 16);

	/* remember last written value in sc */
	sc->rf_regs[reg] = val;

	DPRINTFN(15, ("RF R[%u] <- 0x%05x\n", reg & 0x3, val & 0xfffff));
}

void
ural_set_chan(struct ural_softc *sc, struct ieee80211_channel *c)
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
	case RAL_RF_2522:
		ural_rf_write(sc, RAL_RF1, 0x00814);
		ural_rf_write(sc, RAL_RF2, ural_rf2522_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x00040);
		break;

	case RAL_RF_2523:
		ural_rf_write(sc, RAL_RF1, 0x08804);
		ural_rf_write(sc, RAL_RF2, ural_rf2523_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x38044);
		ural_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RAL_RF_2524:
		ural_rf_write(sc, RAL_RF1, 0x0c808);
		ural_rf_write(sc, RAL_RF2, ural_rf2524_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x00040);
		ural_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RAL_RF_2525:
		ural_rf_write(sc, RAL_RF1, 0x08808);
		ural_rf_write(sc, RAL_RF2, ural_rf2525_hi_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		ural_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);

		ural_rf_write(sc, RAL_RF1, 0x08808);
		ural_rf_write(sc, RAL_RF2, ural_rf2525_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		ural_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00280 : 0x00286);
		break;

	case RAL_RF_2525E:
		ural_rf_write(sc, RAL_RF1, 0x08808);
		ural_rf_write(sc, RAL_RF2, ural_rf2525e_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		ural_rf_write(sc, RAL_RF4, (chan == 14) ? 0x00286 : 0x00282);
		break;

	case RAL_RF_2526:
		ural_rf_write(sc, RAL_RF2, ural_rf2526_hi_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF4, (chan & 1) ? 0x00386 : 0x00381);
		ural_rf_write(sc, RAL_RF1, 0x08804);

		ural_rf_write(sc, RAL_RF2, ural_rf2526_r2[chan - 1]);
		ural_rf_write(sc, RAL_RF3, power << 7 | 0x18044);
		ural_rf_write(sc, RAL_RF4, (chan & 1) ? 0x00386 : 0x00381);
		break;
	}

	if (ic->ic_opmode != IEEE80211_M_MONITOR &&
	    ic->ic_state != IEEE80211_S_SCAN) {
		/* set Japan filter bit for channel 14 */
		tmp = ural_bbp_read(sc, 70);

		tmp &= ~RAL_JAPAN_FILTER;
		if (chan == 14)
			tmp |= RAL_JAPAN_FILTER;

		ural_bbp_write(sc, 70, tmp);

		/* clear CRC errors */
		ural_read(sc, RAL_STA_CSR0);

		DELAY(1000); /* RF needs a 1ms delay here */
		ural_disable_rf_tune(sc);
	}
}

/*
 * Disable RF auto-tuning.
 */
void
ural_disable_rf_tune(struct ural_softc *sc)
{
	uint32_t tmp;

	if (sc->rf_rev != RAL_RF_2523) {
		tmp = sc->rf_regs[RAL_RF1] & ~RAL_RF1_AUTOTUNE;
		ural_rf_write(sc, RAL_RF1, tmp);
	}

	tmp = sc->rf_regs[RAL_RF3] & ~RAL_RF3_AUTOTUNE;
	ural_rf_write(sc, RAL_RF3, tmp);

	DPRINTFN(2, ("disabling RF autotune\n"));
}

/*
 * Refer to IEEE Std 802.11-1999 pp. 123 for more information on TSF
 * synchronization.
 */
void
ural_enable_tsf_sync(struct ural_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t logcwmin, preload, tmp;

	/* first, disable TSF synchronization */
	ural_write(sc, RAL_TXRX_CSR19, 0);

	tmp = (16 * ic->ic_bss->ni_intval) << 4;
	ural_write(sc, RAL_TXRX_CSR18, tmp);

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		logcwmin = 2;
		preload = 320;
	} else
#endif
	{
		logcwmin = 0;
		preload = 6;
	}
	tmp = logcwmin << 12 | preload;
	ural_write(sc, RAL_TXRX_CSR20, tmp);

	/* finally, enable TSF synchronization */
	tmp = RAL_ENABLE_TSF | RAL_ENABLE_TBCN;
	if (ic->ic_opmode == IEEE80211_M_STA)
		tmp |= RAL_ENABLE_TSF_SYNC(1);
#ifndef IEEE80211_STA_ONLY
	else
		tmp |= RAL_ENABLE_TSF_SYNC(2) | RAL_ENABLE_BEACON_GENERATOR;
#endif
	ural_write(sc, RAL_TXRX_CSR19, tmp);

	DPRINTF(("enabling TSF synchronization\n"));
}

void
ural_update_slot(struct ural_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t slottime, sifs, eifs;

	slottime = (ic->ic_flags & IEEE80211_F_SHSLOT) ?
	    IEEE80211_DUR_DS_SHSLOT : IEEE80211_DUR_DS_SLOT;

	/*
	 * These settings may sound a bit inconsistent but this is what the
	 * reference driver does.
	 */
	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		sifs = 16 - RAL_RXTX_TURNAROUND;
		eifs = 364;
	} else {
		sifs = 10 - RAL_RXTX_TURNAROUND;
		eifs = 64;
	}

	ural_write(sc, RAL_MAC_CSR10, slottime);
	ural_write(sc, RAL_MAC_CSR11, sifs);
	ural_write(sc, RAL_MAC_CSR12, eifs);
}

void
ural_set_txpreamble(struct ural_softc *sc)
{
	uint16_t tmp;

	tmp = ural_read(sc, RAL_TXRX_CSR10);

	tmp &= ~RAL_SHORT_PREAMBLE;
	if (sc->sc_ic.ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= RAL_SHORT_PREAMBLE;

	ural_write(sc, RAL_TXRX_CSR10, tmp);
}

void
ural_set_basicrates(struct ural_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* update basic rate set */
	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		/* 11b basic rates: 1, 2Mbps */
		ural_write(sc, RAL_TXRX_CSR11, 0x3);
	} else {
		/* 11b/g basic rates: 1, 2, 5.5, 11Mbps */
		ural_write(sc, RAL_TXRX_CSR11, 0xf);
	}
}

void
ural_set_bssid(struct ural_softc *sc, const uint8_t *bssid)
{
	uint16_t tmp;

	tmp = bssid[0] | bssid[1] << 8;
	ural_write(sc, RAL_MAC_CSR5, tmp);

	tmp = bssid[2] | bssid[3] << 8;
	ural_write(sc, RAL_MAC_CSR6, tmp);

	tmp = bssid[4] | bssid[5] << 8;
	ural_write(sc, RAL_MAC_CSR7, tmp);

	DPRINTF(("setting BSSID to %s\n", ether_sprintf((uint8_t *)bssid)));
}

void
ural_set_macaddr(struct ural_softc *sc, const uint8_t *addr)
{
	uint16_t tmp;

	tmp = addr[0] | addr[1] << 8;
	ural_write(sc, RAL_MAC_CSR2, tmp);

	tmp = addr[2] | addr[3] << 8;
	ural_write(sc, RAL_MAC_CSR3, tmp);

	tmp = addr[4] | addr[5] << 8;
	ural_write(sc, RAL_MAC_CSR4, tmp);

	DPRINTF(("setting MAC address to %s\n",
	    ether_sprintf((uint8_t *)addr)));
}

void
ural_update_promisc(struct ural_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint16_t tmp;

	tmp = ural_read(sc, RAL_TXRX_CSR2);

	tmp &= ~RAL_DROP_NOT_TO_ME;
	if (!(ifp->if_flags & IFF_PROMISC))
		tmp |= RAL_DROP_NOT_TO_ME;

	ural_write(sc, RAL_TXRX_CSR2, tmp);

	DPRINTF(("%s promiscuous mode\n", (ifp->if_flags & IFF_PROMISC) ?
	    "entering" : "leaving"));
}

const char *
ural_get_rf(int rev)
{
	switch (rev) {
	case RAL_RF_2522:	return "RT2522";
	case RAL_RF_2523:	return "RT2523";
	case RAL_RF_2524:	return "RT2524";
	case RAL_RF_2525:	return "RT2525";
	case RAL_RF_2525E:	return "RT2525e";
	case RAL_RF_2526:	return "RT2526";
	case RAL_RF_5222:	return "RT5222";
	default:		return "unknown";
	}
}

void
ural_read_eeprom(struct ural_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t val;

	/* retrieve MAC/BBP type */
	ural_eeprom_read(sc, RAL_EEPROM_MACBBP, &val, 2);
	sc->macbbp_rev = letoh16(val);

	ural_eeprom_read(sc, RAL_EEPROM_CONFIG0, &val, 2);
	val = letoh16(val);
	sc->rf_rev =   (val >> 11) & 0x7;
	sc->hw_radio = (val >> 10) & 0x1;
	sc->led_mode = (val >> 6)  & 0x7;
	sc->rx_ant =   (val >> 4)  & 0x3;
	sc->tx_ant =   (val >> 2)  & 0x3;
	sc->nb_ant =   val & 0x3;

	/* read MAC address */
	ural_eeprom_read(sc, RAL_EEPROM_ADDRESS, ic->ic_myaddr, 6);

	/* read default values for BBP registers */
	ural_eeprom_read(sc, RAL_EEPROM_BBP_BASE, sc->bbp_prom, 2 * 16);

	/* read Tx power for all b/g channels */
	ural_eeprom_read(sc, RAL_EEPROM_TXPOWER, sc->txpow, 14);
}

int
ural_bbp_init(struct ural_softc *sc)
{
	int i, ntries;

	/* wait for BBP to be ready */
	for (ntries = 0; ntries < 100; ntries++) {
		if (ural_bbp_read(sc, RAL_BBP_VERSION) != 0)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for BBP\n", sc->sc_dev.dv_xname);
		return EIO;
	}

	/* initialize BBP registers to default values */
	for (i = 0; i < nitems(ural_def_bbp); i++)
		ural_bbp_write(sc, ural_def_bbp[i].reg, ural_def_bbp[i].val);

#if 0
	/* initialize BBP registers to values stored in EEPROM */
	for (i = 0; i < 16; i++) {
		if (sc->bbp_prom[i].reg == 0xff)
			continue;
		ural_bbp_write(sc, sc->bbp_prom[i].reg, sc->bbp_prom[i].val);
	}
#endif

	return 0;
}

void
ural_set_txantenna(struct ural_softc *sc, int antenna)
{
	uint16_t tmp;
	uint8_t tx;

	tx = ural_bbp_read(sc, RAL_BBP_TX) & ~RAL_BBP_ANTMASK;
	if (antenna == 1)
		tx |= RAL_BBP_ANTA;
	else if (antenna == 2)
		tx |= RAL_BBP_ANTB;
	else
		tx |= RAL_BBP_DIVERSITY;

	/* need to force I/Q flip for RF 2525e, 2526 and 5222 */
	if (sc->rf_rev == RAL_RF_2525E || sc->rf_rev == RAL_RF_2526 ||
	    sc->rf_rev == RAL_RF_5222)
		tx |= RAL_BBP_FLIPIQ;

	ural_bbp_write(sc, RAL_BBP_TX, tx);

	/* update flags in PHY_CSR5 and PHY_CSR6 too */
	tmp = ural_read(sc, RAL_PHY_CSR5) & ~0x7;
	ural_write(sc, RAL_PHY_CSR5, tmp | (tx & 0x7));

	tmp = ural_read(sc, RAL_PHY_CSR6) & ~0x7;
	ural_write(sc, RAL_PHY_CSR6, tmp | (tx & 0x7));
}

void
ural_set_rxantenna(struct ural_softc *sc, int antenna)
{
	uint8_t rx;

	rx = ural_bbp_read(sc, RAL_BBP_RX) & ~RAL_BBP_ANTMASK;
	if (antenna == 1)
		rx |= RAL_BBP_ANTA;
	else if (antenna == 2)
		rx |= RAL_BBP_ANTB;
	else
		rx |= RAL_BBP_DIVERSITY;

	/* need to force no I/Q flip for RF 2525e and 2526 */
	if (sc->rf_rev == RAL_RF_2525E || sc->rf_rev == RAL_RF_2526)
		rx &= ~RAL_BBP_FLIPIQ;

	ural_bbp_write(sc, RAL_BBP_RX, rx);
}

int
ural_init(struct ifnet *ifp)
{
	struct ural_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t tmp;
	usbd_status error;
	int i, ntries;

	ural_stop(ifp, 0);

	/* initialize MAC registers to default values */
	for (i = 0; i < nitems(ural_def_mac); i++)
		ural_write(sc, ural_def_mac[i].reg, ural_def_mac[i].val);

	/* wait for BBP and RF to wake up (this can take a long time!) */
	for (ntries = 0; ntries < 100; ntries++) {
		tmp = ural_read(sc, RAL_MAC_CSR17);
		if ((tmp & (RAL_BBP_AWAKE | RAL_RF_AWAKE)) ==
		    (RAL_BBP_AWAKE | RAL_RF_AWAKE))
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for BBP/RF to wakeup\n",
		    sc->sc_dev.dv_xname);
		error = EIO;
		goto fail;
	}

	/* we're ready! */
	ural_write(sc, RAL_MAC_CSR1, RAL_HOST_READY);

	/* set basic rate set (will be updated later) */
	ural_write(sc, RAL_TXRX_CSR11, 0x153);

	error = ural_bbp_init(sc);
	if (error != 0)
		goto fail;

	/* set default BSS channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	ural_set_chan(sc, ic->ic_bss->ni_chan);

	/* clear statistic registers (STA_CSR0 to STA_CSR10) */
	ural_read_multi(sc, RAL_STA_CSR0, sc->sta, sizeof sc->sta);

	/* set default sensitivity */
	ural_bbp_write(sc, 17, 0x48);

	ural_set_txantenna(sc, 1);
	ural_set_rxantenna(sc, 1);

	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	ural_set_macaddr(sc, ic->ic_myaddr);

	/*
	 * Copy WEP keys into adapter's memory (SEC_CSR0 to SEC_CSR31).
	 */
	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		struct ieee80211_key *k = &ic->ic_nw_keys[i];
		ural_write_multi(sc, RAL_SEC_CSR0 + i * IEEE80211_KEYBUF_SIZE,
		    k->k_key, IEEE80211_KEYBUF_SIZE);
	}

	/*
	 * Allocate xfer for AMRR statistics requests.
	 */
	sc->amrr_xfer = usbd_alloc_xfer(sc->sc_udev);
	if (sc->amrr_xfer == NULL) {
		printf("%s: could not allocate AMRR xfer\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/*
	 * Open Tx and Rx USB bulk pipes.
	 */
	error = usbd_open_pipe(sc->sc_iface, sc->sc_tx_no, USBD_EXCLUSIVE_USE,
	    &sc->sc_tx_pipeh);
	if (error != 0) {
		printf("%s: could not open Tx pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}
	error = usbd_open_pipe(sc->sc_iface, sc->sc_rx_no, USBD_EXCLUSIVE_USE,
	    &sc->sc_rx_pipeh);
	if (error != 0) {
		printf("%s: could not open Rx pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	/*
	 * Allocate Tx and Rx xfer queues.
	 */
	error = ural_alloc_tx_list(sc);
	if (error != 0) {
		printf("%s: could not allocate Tx list\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	error = ural_alloc_rx_list(sc);
	if (error != 0) {
		printf("%s: could not allocate Rx list\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/*
	 * Start up the receive pipe.
	 */
	for (i = 0; i < RAL_RX_LIST_COUNT; i++) {
		struct ural_rx_data *data = &sc->rx_data[i];

		usbd_setup_xfer(data->xfer, sc->sc_rx_pipeh, data, data->buf,
		    MCLBYTES, USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, ural_rxeof);
		error = usbd_transfer(data->xfer);
		if (error != 0 && error != USBD_IN_PROGRESS) {
			printf("%s: could not queue Rx transfer\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}

	/* kick Rx */
	tmp = RAL_DROP_PHY_ERROR | RAL_DROP_CRC_ERROR;
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RAL_DROP_CTL | RAL_DROP_VERSION_ERROR;
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode != IEEE80211_M_HOSTAP)
#endif
			tmp |= RAL_DROP_TODS;
		if (!(ifp->if_flags & IFF_PROMISC))
			tmp |= RAL_DROP_NOT_TO_ME;
	}
	ural_write(sc, RAL_TXRX_CSR2, tmp);

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return 0;

fail:	ural_stop(ifp, 1);
	return error;
}

void
ural_stop(struct ifnet *ifp, int disable)
{
	struct ural_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);	/* free all nodes */

	/* disable Rx */
	ural_write(sc, RAL_TXRX_CSR2, RAL_DISABLE_RX);

	/* reset ASIC and BBP (but won't reset MAC registers!) */
	ural_write(sc, RAL_MAC_CSR1, RAL_RESET_ASIC | RAL_RESET_BBP);
	ural_write(sc, RAL_MAC_CSR1, 0);

	if (sc->amrr_xfer != NULL) {
		usbd_free_xfer(sc->amrr_xfer);
		sc->amrr_xfer = NULL;
	}
	if (sc->sc_rx_pipeh != NULL) {
		usbd_close_pipe(sc->sc_rx_pipeh);
		sc->sc_rx_pipeh = NULL;
	}
	if (sc->sc_tx_pipeh != NULL) {
		usbd_close_pipe(sc->sc_tx_pipeh);
		sc->sc_tx_pipeh = NULL;
	}

	ural_free_rx_list(sc);
	ural_free_tx_list(sc);
}

void
ural_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	/* start with lowest Tx rate */
	ni->ni_txrate = 0;
}

void
ural_amrr_start(struct ural_softc *sc, struct ieee80211_node *ni)
{
	int i;

	/* clear statistic registers (STA_CSR0 to STA_CSR10) */
	ural_read_multi(sc, RAL_STA_CSR0, sc->sta, sizeof sc->sta);

	ieee80211_amrr_node_init(&sc->amrr, &sc->amn);

	/* set rate to some reasonable initial value */
	for (i = ni->ni_rates.rs_nrates - 1;
	     i > 0 && (ni->ni_rates.rs_rates[i] & IEEE80211_RATE_VAL) > 72;
	     i--);
	ni->ni_txrate = i;

	if (!usbd_is_dying(sc->sc_udev))
		timeout_add_sec(&sc->amrr_to, 1);
}

void
ural_amrr_timeout(void *arg)
{
	struct ural_softc *sc = arg;
	usb_device_request_t req;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);

	s = splusb();

	/*
	 * Asynchronously read statistic registers (cleared by read).
	 */
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RAL_READ_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, RAL_STA_CSR0);
	USETW(req.wLength, sizeof sc->sta);

	usbd_setup_default_xfer(sc->amrr_xfer, sc->sc_udev, sc,
	    USBD_DEFAULT_TIMEOUT, &req, sc->sta, sizeof sc->sta, 0,
	    ural_amrr_update);
	(void)usbd_transfer(sc->amrr_xfer);

	splx(s);

	usbd_ref_decr(sc->sc_udev);
}

void
ural_amrr_update(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct ural_softc *sc = (struct ural_softc *)priv;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (status != USBD_NORMAL_COMPLETION) {
		printf("%s: could not retrieve Tx statistics - cancelling "
		    "automatic rate control\n", sc->sc_dev.dv_xname);
		return;
	}

	/* count TX retry-fail as Tx errors */
	ifp->if_oerrors += letoh16(sc->sta[9]);

	sc->amn.amn_retrycnt =
	    letoh16(sc->sta[7]) +	/* TX one-retry ok count */
	    letoh16(sc->sta[8]) +	/* TX more-retry ok count */
	    letoh16(sc->sta[9]);	/* TX retry-fail count */

	sc->amn.amn_txcnt =
	    sc->amn.amn_retrycnt +
	    letoh16(sc->sta[6]);	/* TX no-retry ok count */

	ieee80211_amrr_choose(&sc->amrr, sc->sc_ic.ic_bss, &sc->amn);

	if (!usbd_is_dying(sc->sc_udev))
		timeout_add_sec(&sc->amrr_to, 1);
}

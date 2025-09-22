/*	$OpenBSD: if_rum.c,v 1.129 2024/05/23 03:21:08 jsg Exp $	*/

/*-
 * Copyright (c) 2005-2007 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2006 Niall O'Higgins <niallo@openbsd.org>
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
 * Ralink Technology RT2501USB/RT2601USB chipset driver
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

#include <dev/usb/if_rumreg.h>
#include <dev/usb/if_rumvar.h>

#ifdef RUM_DEBUG
#define DPRINTF(x)	do { if (rum_debug) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (rum_debug >= (n)) printf x; } while (0)
int rum_debug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

/* various supported device vendors/products */
static const struct usb_devno rum_devs[] = {
	{ USB_VENDOR_ABOCOM,		USB_PRODUCT_ABOCOM_HWU54DM },
	{ USB_VENDOR_ABOCOM,		USB_PRODUCT_ABOCOM_RT2573_2 },
	{ USB_VENDOR_ABOCOM,		USB_PRODUCT_ABOCOM_RT2573_3 },
	{ USB_VENDOR_ABOCOM,		USB_PRODUCT_ABOCOM_RT2573_4 },
	{ USB_VENDOR_ABOCOM,		USB_PRODUCT_ABOCOM_WUG2700 },
	{ USB_VENDOR_AMIT,		USB_PRODUCT_AMIT_CGWLUSB2GO },
	{ USB_VENDOR_ASUS,		USB_PRODUCT_ASUS_RT2573_1 },
	{ USB_VENDOR_ASUS,		USB_PRODUCT_ASUS_RT2573_2 },
	{ USB_VENDOR_BELKIN,		USB_PRODUCT_BELKIN_F5D7050A },
	{ USB_VENDOR_BELKIN,		USB_PRODUCT_BELKIN_F5D9050V3 },
	{ USB_VENDOR_BELKIN,		USB_PRODUCT_BELKIN_F5D9050C },
	{ USB_VENDOR_CISCOLINKSYS,	USB_PRODUCT_CISCOLINKSYS_WUSB200 },
	{ USB_VENDOR_CISCOLINKSYS,	USB_PRODUCT_CISCOLINKSYS_WUSB54GC },
	{ USB_VENDOR_CISCOLINKSYS,	USB_PRODUCT_CISCOLINKSYS_WUSB54GR },
	{ USB_VENDOR_CONCEPTRONIC2,	USB_PRODUCT_CONCEPTRONIC2_C54RU2 },
	{ USB_VENDOR_CONCEPTRONIC2,	USB_PRODUCT_CONCEPTRONIC2_RT2573 },
	{ USB_VENDOR_COREGA,		USB_PRODUCT_COREGA_CGWLUSB2GL },
	{ USB_VENDOR_COREGA,		USB_PRODUCT_COREGA_CGWLUSB2GPX },
	{ USB_VENDOR_DICKSMITH,		USB_PRODUCT_DICKSMITH_CWD854F },
	{ USB_VENDOR_DICKSMITH,		USB_PRODUCT_DICKSMITH_RT2573 },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWA111 },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWA110 },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWLG122C1 },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_WUA1340 },
	{ USB_VENDOR_EDIMAX,		USB_PRODUCT_EDIMAX_EW7318 },
	{ USB_VENDOR_EDIMAX,		USB_PRODUCT_EDIMAX_EW7618 },
	{ USB_VENDOR_GIGABYTE,		USB_PRODUCT_GIGABYTE_GNWB01GS },
	{ USB_VENDOR_GIGABYTE,		USB_PRODUCT_GIGABYTE_GNWI05GS },
	{ USB_VENDOR_GIGASET,		USB_PRODUCT_GIGASET_RT2573 },
	{ USB_VENDOR_GOODWAY,		USB_PRODUCT_GOODWAY_RT2573 },
	{ USB_VENDOR_GUILLEMOT,		USB_PRODUCT_GUILLEMOT_HWGUSB254LB },
	{ USB_VENDOR_GUILLEMOT,		USB_PRODUCT_GUILLEMOT_HWGUSB254V2AP },
	{ USB_VENDOR_HUAWEI3COM,	USB_PRODUCT_HUAWEI3COM_WUB320G },
	{ USB_VENDOR_MELCO,		USB_PRODUCT_MELCO_G54HP },
	{ USB_VENDOR_MELCO,		USB_PRODUCT_MELCO_SG54HP },
	{ USB_VENDOR_MELCO,		USB_PRODUCT_MELCO_SG54HG },
	{ USB_VENDOR_MSI,		USB_PRODUCT_MSI_RT2573_1 },
	{ USB_VENDOR_MSI,		USB_PRODUCT_MSI_RT2573_2 },
	{ USB_VENDOR_MSI,		USB_PRODUCT_MSI_RT2573_3 },
	{ USB_VENDOR_MSI,		USB_PRODUCT_MSI_RT2573_4 },
	{ USB_VENDOR_NOVATECH,		USB_PRODUCT_NOVATECH_RT2573 },
	{ USB_VENDOR_PLANEX2,		USB_PRODUCT_PLANEX2_GWUS54HP },
	{ USB_VENDOR_PLANEX2,		USB_PRODUCT_PLANEX2_GWUS54MINI2 },
	{ USB_VENDOR_PLANEX2,		USB_PRODUCT_PLANEX2_GWUSMM },
	{ USB_VENDOR_QCOM,		USB_PRODUCT_QCOM_RT2573 },
	{ USB_VENDOR_QCOM,		USB_PRODUCT_QCOM_RT2573_2 },
	{ USB_VENDOR_QCOM,		USB_PRODUCT_QCOM_RT2573_3 },
	{ USB_VENDOR_RALINK,		USB_PRODUCT_RALINK_RT2573 },
	{ USB_VENDOR_RALINK,		USB_PRODUCT_RALINK_RT2573_2 },
	{ USB_VENDOR_RALINK,		USB_PRODUCT_RALINK_RT2671 },
	{ USB_VENDOR_SITECOMEU,		USB_PRODUCT_SITECOMEU_WL113R2 },
	{ USB_VENDOR_SITECOMEU,		USB_PRODUCT_SITECOMEU_WL172 },
	{ USB_VENDOR_SURECOM,		USB_PRODUCT_SURECOM_RT2573 },
	{ USB_VENDOR_SPARKLAN,		USB_PRODUCT_SPARKLAN_RT2573 },
	{ USB_VENDOR_ZYXEL,		USB_PRODUCT_ZYXEL_RT2573 }
};

void		rum_attachhook(struct device *);
int		rum_alloc_tx_list(struct rum_softc *);
void		rum_free_tx_list(struct rum_softc *);
int		rum_alloc_rx_list(struct rum_softc *);
void		rum_free_rx_list(struct rum_softc *);
int		rum_media_change(struct ifnet *);
void		rum_next_scan(void *);
void		rum_task(void *);
int		rum_newstate(struct ieee80211com *, enum ieee80211_state, int);
void		rum_txeof(struct usbd_xfer *, void *, usbd_status);
void		rum_rxeof(struct usbd_xfer *, void *, usbd_status);
#if NBPFILTER > 0
uint8_t		rum_rxrate(const struct rum_rx_desc *);
#endif
int		rum_ack_rate(struct ieee80211com *, int);
uint16_t	rum_txtime(int, int, uint32_t);
uint8_t		rum_plcp_signal(int);
void		rum_setup_tx_desc(struct rum_softc *, struct rum_tx_desc *,
		    uint32_t, uint16_t, int, int);
int		rum_tx_data(struct rum_softc *, struct mbuf *,
		    struct ieee80211_node *);
void		rum_start(struct ifnet *);
void		rum_watchdog(struct ifnet *);
int		rum_ioctl(struct ifnet *, u_long, caddr_t);
void		rum_eeprom_read(struct rum_softc *, uint16_t, void *, int);
uint32_t	rum_read(struct rum_softc *, uint16_t);
void		rum_read_multi(struct rum_softc *, uint16_t, void *, int);
void		rum_write(struct rum_softc *, uint16_t, uint32_t);
void		rum_write_multi(struct rum_softc *, uint16_t, void *, size_t);
void		rum_bbp_write(struct rum_softc *, uint8_t, uint8_t);
uint8_t		rum_bbp_read(struct rum_softc *, uint8_t);
void		rum_rf_write(struct rum_softc *, uint8_t, uint32_t);
void		rum_select_antenna(struct rum_softc *);
void		rum_enable_mrr(struct rum_softc *);
void		rum_set_txpreamble(struct rum_softc *);
void		rum_set_basicrates(struct rum_softc *);
void		rum_select_band(struct rum_softc *,
		    struct ieee80211_channel *);
void		rum_set_chan(struct rum_softc *, struct ieee80211_channel *);
void		rum_enable_tsf_sync(struct rum_softc *);
void		rum_update_slot(struct rum_softc *);
void		rum_set_bssid(struct rum_softc *, const uint8_t *);
void		rum_set_macaddr(struct rum_softc *, const uint8_t *);
void		rum_update_promisc(struct rum_softc *);
const char	*rum_get_rf(int);
void		rum_read_eeprom(struct rum_softc *);
int		rum_bbp_init(struct rum_softc *);
int		rum_init(struct ifnet *);
void		rum_stop(struct ifnet *, int);
int		rum_load_microcode(struct rum_softc *, const u_char *, size_t);
#ifndef IEEE80211_STA_ONLY
int		rum_prepare_beacon(struct rum_softc *);
#endif
void		rum_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);
void		rum_amrr_start(struct rum_softc *, struct ieee80211_node *);
void		rum_amrr_timeout(void *);
void		rum_amrr_update(struct usbd_xfer *, void *,
		    usbd_status status);

static const struct {
	uint32_t	reg;
	uint32_t	val;
} rum_def_mac[] = {
	RT2573_DEF_MAC
};

static const struct {
	uint8_t	reg;
	uint8_t	val;
} rum_def_bbp[] = {
	RT2573_DEF_BBP
};

static const struct rfprog {
	uint8_t		chan;
	uint32_t	r1, r2, r3, r4;
}  rum_rf5226[] = {
	RT2573_RF5226
}, rum_rf5225[] = {
	RT2573_RF5225
};

int rum_match(struct device *, void *, void *);
void rum_attach(struct device *, struct device *, void *);
int rum_detach(struct device *, int);

struct cfdriver rum_cd = {
	NULL, "rum", DV_IFNET
};

const struct cfattach rum_ca = {
	sizeof(struct rum_softc), rum_match, rum_attach, rum_detach
};

int
rum_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return UMATCH_NONE;

	return (usb_lookup(rum_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE;
}

void
rum_attachhook(struct device *self)
{
	struct rum_softc *sc = (struct rum_softc *)self;
	const char *name = "rum-rt2573";
	u_char *ucode;
	size_t size;
	int error;

	if ((error = loadfirmware(name, &ucode, &size)) != 0) {
		printf("%s: failed loadfirmware of file %s (error %d)\n",
		    sc->sc_dev.dv_xname, name, error);
		return;
	}

	if (rum_load_microcode(sc, ucode, size) != 0) {
		printf("%s: could not load 8051 microcode\n",
		    sc->sc_dev.dv_xname);
	}

	free(ucode, M_DEVBUF, size);
}

void
rum_attach(struct device *parent, struct device *self, void *aux)
{
	struct rum_softc *sc = (struct rum_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i, ntries;
	uint32_t tmp;

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

	usb_init_task(&sc->sc_task, rum_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->scan_to, rum_next_scan, sc);

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 10;
	timeout_set(&sc->amrr_to, rum_amrr_timeout, sc);

	/* retrieve RT2573 rev. no */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((tmp = rum_read(sc, RT2573_MAC_CSR0)) != 0)
			break;
		DELAY(1000);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for chip to settle\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/* retrieve MAC address and various other things from EEPROM */
	rum_read_eeprom(sc);

	printf("%s: MAC/BBP RT%04x (rev 0x%05x), RF %s, address %s\n",
	    sc->sc_dev.dv_xname, sc->macbbp_rev, tmp,
	    rum_get_rf(sc->rf_rev), ether_sprintf(ic->ic_myaddr));

	config_mountroot(self, rum_attachhook);

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

	if (sc->rf_rev == RT2573_RF_5225 || sc->rf_rev == RT2573_RF_5226) {
		/* set supported .11a rates */
		ic->ic_sup_rates[IEEE80211_MODE_11A] =
		    ieee80211_std_rateset_11a;

		/* set supported .11a channels */
		for (i = 34; i <= 46; i += 4) {
			ic->ic_channels[i].ic_freq =
			    ieee80211_ieee2mhz(i, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[i].ic_flags = IEEE80211_CHAN_A;
		}
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
	ifp->if_ioctl = rum_ioctl;
	ifp->if_start = rum_start;
	ifp->if_watchdog = rum_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_newassoc = rum_newassoc;

	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = rum_newstate;
	ieee80211_media_init(ifp, rum_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(RT2573_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(RT2573_TX_RADIOTAP_PRESENT);
#endif
}

int
rum_detach(struct device *self, int flags)
{
	struct rum_softc *sc = (struct rum_softc *)self;
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

	rum_free_rx_list(sc);
	rum_free_tx_list(sc);

	splx(s);

	return 0;
}

int
rum_alloc_tx_list(struct rum_softc *sc)
{
	int i, error;

	sc->tx_cur = sc->tx_queued = 0;

	for (i = 0; i < RUM_TX_LIST_COUNT; i++) {
		struct rum_tx_data *data = &sc->tx_data[i];

		data->sc = sc;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate tx xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		data->buf = usbd_alloc_buffer(data->xfer,
		    RT2573_TX_DESC_SIZE + IEEE80211_MAX_LEN);
		if (data->buf == NULL) {
			printf("%s: could not allocate tx buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		/* clean Tx descriptor */
		bzero(data->buf, RT2573_TX_DESC_SIZE);
	}

	return 0;

fail:	rum_free_tx_list(sc);
	return error;
}

void
rum_free_tx_list(struct rum_softc *sc)
{
	int i;

	for (i = 0; i < RUM_TX_LIST_COUNT; i++) {
		struct rum_tx_data *data = &sc->tx_data[i];

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
rum_alloc_rx_list(struct rum_softc *sc)
{
	int i, error;

	for (i = 0; i < RUM_RX_LIST_COUNT; i++) {
		struct rum_rx_data *data = &sc->rx_data[i];

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

fail:	rum_free_rx_list(sc);
	return error;
}

void
rum_free_rx_list(struct rum_softc *sc)
{
	int i;

	for (i = 0; i < RUM_RX_LIST_COUNT; i++) {
		struct rum_rx_data *data = &sc->rx_data[i];

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
rum_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		error = rum_init(ifp);

	return error;
}

/*
 * This function is called periodically (every 200ms) during scanning to
 * switch from one channel to another.
 */
void
rum_next_scan(void *arg)
{
	struct rum_softc *sc = arg;
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
rum_task(void *arg)
{
	struct rum_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state ostate;
	struct ieee80211_node *ni;
	uint32_t tmp;

	if (usbd_is_dying(sc->sc_udev))
		return;

	ostate = ic->ic_state;

	switch (sc->sc_state) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* abort TSF synchronization */
			tmp = rum_read(sc, RT2573_TXRX_CSR9);
			rum_write(sc, RT2573_TXRX_CSR9, tmp & ~0x00ffffff);
		}
		break;

	case IEEE80211_S_SCAN:
		rum_set_chan(sc, ic->ic_bss->ni_chan);
		if (!usbd_is_dying(sc->sc_udev))
			timeout_add_msec(&sc->scan_to, 200);
		break;

	case IEEE80211_S_AUTH:
		rum_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_ASSOC:
		rum_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_RUN:
		rum_set_chan(sc, ic->ic_bss->ni_chan);

		ni = ic->ic_bss;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			rum_update_slot(sc);
			rum_enable_mrr(sc);
			rum_set_txpreamble(sc);
			rum_set_basicrates(sc);
			rum_set_bssid(sc, ni->ni_bssid);
		}

#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS)
			rum_prepare_beacon(sc);
#endif

		if (ic->ic_opmode != IEEE80211_M_MONITOR)
			rum_enable_tsf_sync(sc);

		if (ic->ic_opmode == IEEE80211_M_STA) {
			/* fake a join to init the tx rate */
			rum_newassoc(ic, ic->ic_bss, 1);

			/* enable automatic rate control in STA mode */
			if (ic->ic_fixed_rate == -1)
				rum_amrr_start(sc, ni);
		}
		break;
	}

	sc->sc_newstate(ic, sc->sc_state, sc->sc_arg);
}

int
rum_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct rum_softc *sc = ic->ic_if.if_softc;

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
#define RUM_RATE_IS_OFDM(rate)	((rate) >= 12 && (rate) != 22)

#define RUM_ACK_SIZE	14	/* 10 + 4(FCS) */
#define RUM_CTS_SIZE	14	/* 10 + 4(FCS) */

void
rum_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct rum_tx_data *data = priv;
	struct rum_softc *sc = data->sc;
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
	rum_start(ifp);

	splx(s);
}

void
rum_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct rum_rx_data *data = priv;
	struct rum_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	const struct rum_rx_desc *desc;
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

	if (len < RT2573_RX_DESC_SIZE + sizeof (struct ieee80211_frame_min)) {
		DPRINTF(("%s: xfer too short %d\n", sc->sc_dev.dv_xname,
		    len));
		ifp->if_ierrors++;
		goto skip;
	}

	desc = (const struct rum_rx_desc *)data->buf;

	if (letoh32(desc->flags) & RT2573_RX_CRC_ERROR) {
		/*
		 * This should not happen since we did not request to receive
		 * those frames when we filled RT2573_TXRX_CSR0.
		 */
		DPRINTFN(5, ("CRC error\n"));
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
	m->m_data = (caddr_t)(desc + 1);
	m->m_pkthdr.len = m->m_len = (letoh32(desc->flags) >> 16) & 0xfff;

	s = splnet();

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct rum_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		tap->wr_rate = rum_rxrate(desc);
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
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, rum_rxeof);
	(void)usbd_transfer(xfer);
}

/*
 * This function is only used by the Rx radiotap code. It returns the rate at
 * which a given frame was received.
 */
#if NBPFILTER > 0
uint8_t
rum_rxrate(const struct rum_rx_desc *desc)
{
	if (letoh32(desc->flags) & RT2573_RX_OFDM) {
		/* reverse function of rum_plcp_signal */
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
rum_ack_rate(struct ieee80211com *ic, int rate)
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
rum_txtime(int len, int rate, uint32_t flags)
{
	uint16_t txtime;

	if (RUM_RATE_IS_OFDM(rate)) {
		/* IEEE Std 802.11a-1999, pp. 37 */
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
rum_plcp_signal(int rate)
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
rum_setup_tx_desc(struct rum_softc *sc, struct rum_tx_desc *desc,
    uint32_t flags, uint16_t xflags, int len, int rate)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t plcp_length;
	int remainder;

	desc->flags = htole32(flags);
	desc->flags |= htole32(RT2573_TX_VALID);
	desc->flags |= htole32(len << 16);

	desc->xflags = htole16(xflags);

	desc->wme = htole16(
	    RT2573_QID(0) |
	    RT2573_AIFSN(2) |
	    RT2573_LOGCWMIN(4) |
	    RT2573_LOGCWMAX(10));

	/* setup PLCP fields */
	desc->plcp_signal  = rum_plcp_signal(rate);
	desc->plcp_service = 4;

	len += IEEE80211_CRC_LEN;
	if (RUM_RATE_IS_OFDM(rate)) {
		desc->flags |= htole32(RT2573_TX_OFDM);

		plcp_length = len & 0xfff;
		desc->plcp_length_hi = plcp_length >> 6;
		desc->plcp_length_lo = plcp_length & 0x3f;
	} else {
		plcp_length = (16 * len + rate - 1) / rate;
		if (rate == 22) {
			remainder = (16 * len) % 22;
			if (remainder != 0 && remainder < 7)
				desc->plcp_service |= RT2573_PLCP_LENGEXT;
		}
		desc->plcp_length_hi = plcp_length >> 8;
		desc->plcp_length_lo = plcp_length & 0xff;

		if (rate != 2 && (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			desc->plcp_signal |= 0x08;
	}
}

#define RUM_TX_TIMEOUT	5000

int
rum_tx_data(struct rum_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rum_tx_desc *desc;
	struct rum_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	uint32_t flags = 0;
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
		    RUM_RATE_IS_OFDM(rate)) {
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

		protrate = IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ? 12 : 2;
		ackrate  = rum_ack_rate(ic, rate);

		dur = rum_txtime(pktlen, rate, ic->ic_flags) +
		      rum_txtime(RUM_ACK_SIZE, ackrate, ic->ic_flags) +
		      2 * sc->sifs;
		if (needrts) {
			dur += rum_txtime(RUM_CTS_SIZE, rum_ack_rate(ic,
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

		data = &sc->tx_data[sc->tx_cur];
		desc = (struct rum_tx_desc *)data->buf;

		/* avoid multiple free() of the same node for each fragment */
		data->ni = ieee80211_ref_node(ni);

		m_copydata(mprot, 0, mprot->m_pkthdr.len,
		    data->buf + RT2573_TX_DESC_SIZE);
		rum_setup_tx_desc(sc, desc,
		    (needrts ? RT2573_TX_NEED_ACK : 0) | RT2573_TX_MORE_FRAG,
		    0, mprot->m_pkthdr.len, protrate);

		/* no roundup necessary here */
		xferlen = RT2573_TX_DESC_SIZE + mprot->m_pkthdr.len;

		/* XXX may want to pass the protection frame to BPF */

		/* mbuf is no longer needed */
		m_freem(mprot);

		usbd_setup_xfer(data->xfer, sc->sc_tx_pipeh, data, data->buf,
		    xferlen, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
		    RUM_TX_TIMEOUT, rum_txeof);
		error = usbd_transfer(data->xfer);
		if (error != 0 && error != USBD_IN_PROGRESS) {
			m_freem(m0);
			return error;
		}

		sc->tx_queued++;
		sc->tx_cur = (sc->tx_cur + 1) % RUM_TX_LIST_COUNT;

		flags |= RT2573_TX_LONG_RETRY | RT2573_TX_IFS_SIFS;
	}

	data = &sc->tx_data[sc->tx_cur];
	desc = (struct rum_tx_desc *)data->buf;

	data->ni = ni;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2573_TX_NEED_ACK;

		dur = rum_txtime(RUM_ACK_SIZE, rum_ack_rate(ic, rate),
		    ic->ic_flags) + sc->sifs;
		*(uint16_t *)wh->i_dur = htole16(dur);

#ifndef IEEE80211_STA_ONLY
		/* tell hardware to set timestamp in probe responses */
		if ((wh->i_fc[0] &
		    (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
		    (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP))
			flags |= RT2573_TX_TIMESTAMP;
#endif
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct rum_tx_radiotap_header *tap = &sc->sc_txtap;

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

	m_copydata(m0, 0, m0->m_pkthdr.len, data->buf + RT2573_TX_DESC_SIZE);
	rum_setup_tx_desc(sc, desc, flags, 0, m0->m_pkthdr.len, rate);

	/* align end on a 4-bytes boundary */
	xferlen = (RT2573_TX_DESC_SIZE + m0->m_pkthdr.len + 3) & ~3;

	/*
	 * No space left in the last URB to store the extra 4 bytes, force
	 * sending of another URB.
	 */
	if ((xferlen % 64) == 0)
		xferlen += 4;

	DPRINTFN(10, ("sending frame len=%u rate=%u xfer len=%u\n",
	    m0->m_pkthdr.len + RT2573_TX_DESC_SIZE, rate, xferlen));

	/* mbuf is no longer needed */
	m_freem(m0);

	usbd_setup_xfer(data->xfer, sc->sc_tx_pipeh, data, data->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, RUM_TX_TIMEOUT, rum_txeof);
	error = usbd_transfer(data->xfer);
	if (error != 0 && error != USBD_IN_PROGRESS)
		return error;

	sc->tx_queued++;
	sc->tx_cur = (sc->tx_cur + 1) % RUM_TX_LIST_COUNT;

	return 0;
}

void
rum_start(struct ifnet *ifp)
{
	struct rum_softc *sc = ifp->if_softc;
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
		if (sc->tx_queued >= RUM_TX_LIST_COUNT - 1) {
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
			if (rum_tx_data(sc, m0, ni) != 0)
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
			if (rum_tx_data(sc, m0, ni) != 0) {
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
rum_watchdog(struct ifnet *ifp)
{
	struct rum_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			/*rum_init(ifp); XXX needs a process context! */
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
rum_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct rum_softc *sc = ifp->if_softc;
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
				rum_update_promisc(sc);
			else
				rum_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				rum_stop(ifp, 1);
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
				rum_set_chan(sc, ic->ic_ibss_chan);
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			rum_init(ifp);
		error = 0;
	}

	splx(s);

	usbd_ref_decr(sc->sc_udev);

	return error;
}

void
rum_eeprom_read(struct rum_softc *sc, uint16_t addr, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2573_READ_EEPROM;
	USETW(req.wValue, 0);
	USETW(req.wIndex, addr);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		printf("%s: could not read EEPROM: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
	}
}

uint32_t
rum_read(struct rum_softc *sc, uint16_t reg)
{
	uint32_t val;

	rum_read_multi(sc, reg, &val, sizeof val);

	return letoh32(val);
}

void
rum_read_multi(struct rum_softc *sc, uint16_t reg, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2573_READ_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);

	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != 0) {
		printf("%s: could not multi read MAC register: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
	}
}

void
rum_write(struct rum_softc *sc, uint16_t reg, uint32_t val)
{
	uint32_t tmp = htole32(val);

	rum_write_multi(sc, reg, &tmp, sizeof tmp);
}

void
rum_write_multi(struct rum_softc *sc, uint16_t reg, void *buf, size_t len)
{
	usb_device_request_t req;
	usbd_status error;
	int offset;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2573_WRITE_MULTI_MAC;
	USETW(req.wValue, 0);

	/* write at most 64 bytes at a time */
	for (offset = 0; offset < len; offset += 64) {
		USETW(req.wIndex, reg + offset);
		USETW(req.wLength, MIN(len - offset, 64));

		error = usbd_do_request(sc->sc_udev, &req, buf + offset);
		if (error != 0) {
			printf("%s: could not multi write MAC register: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(error));
		}
	}
}

void
rum_bbp_write(struct rum_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR3) & RT2573_BBP_BUSY))
			break;
	}
	if (ntries == 5) {
		printf("%s: could not write to BBP\n", sc->sc_dev.dv_xname);
		return;
	}

	tmp = RT2573_BBP_BUSY | (reg & 0x7f) << 8 | val;
	rum_write(sc, RT2573_PHY_CSR3, tmp);
}

uint8_t
rum_bbp_read(struct rum_softc *sc, uint8_t reg)
{
	uint32_t val;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR3) & RT2573_BBP_BUSY))
			break;
	}
	if (ntries == 5) {
		printf("%s: could not read BBP\n", sc->sc_dev.dv_xname);
		return 0;
	}

	val = RT2573_BBP_BUSY | RT2573_BBP_READ | reg << 8;
	rum_write(sc, RT2573_PHY_CSR3, val);

	for (ntries = 0; ntries < 100; ntries++) {
		val = rum_read(sc, RT2573_PHY_CSR3);
		if (!(val & RT2573_BBP_BUSY))
			return val & 0xff;
		DELAY(1);
	}

	printf("%s: could not read BBP\n", sc->sc_dev.dv_xname);
	return 0;
}

void
rum_rf_write(struct rum_softc *sc, uint8_t reg, uint32_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 5; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR4) & RT2573_RF_BUSY))
			break;
	}
	if (ntries == 5) {
		printf("%s: could not write to RF\n", sc->sc_dev.dv_xname);
		return;
	}

	tmp = RT2573_RF_BUSY | RT2573_RF_20BIT | (val & 0xfffff) << 2 |
	    (reg & 3);
	rum_write(sc, RT2573_PHY_CSR4, tmp);

	/* remember last written value in sc */
	sc->rf_regs[reg] = val;

	DPRINTFN(15, ("RF R[%u] <- 0x%05x\n", reg & 3, val & 0xfffff));
}

void
rum_select_antenna(struct rum_softc *sc)
{
	uint8_t bbp4, bbp77;
	uint32_t tmp;

	bbp4  = rum_bbp_read(sc, 4);
	bbp77 = rum_bbp_read(sc, 77);

	/* TBD */

	/* make sure Rx is disabled before switching antenna */
	tmp = rum_read(sc, RT2573_TXRX_CSR0);
	rum_write(sc, RT2573_TXRX_CSR0, tmp | RT2573_DISABLE_RX);

	rum_bbp_write(sc,  4, bbp4);
	rum_bbp_write(sc, 77, bbp77);

	rum_write(sc, RT2573_TXRX_CSR0, tmp);
}

/*
 * Enable multi-rate retries for frames sent at OFDM rates.
 * In 802.11b/g mode, allow fallback to CCK rates.
 */
void
rum_enable_mrr(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	tmp = rum_read(sc, RT2573_TXRX_CSR4);

	tmp &= ~RT2573_MRR_CCK_FALLBACK;
	if (!IEEE80211_IS_CHAN_5GHZ(ic->ic_bss->ni_chan))
		tmp |= RT2573_MRR_CCK_FALLBACK;
	tmp |= RT2573_MRR_ENABLED;

	rum_write(sc, RT2573_TXRX_CSR4, tmp);
}

void
rum_set_txpreamble(struct rum_softc *sc)
{
	uint32_t tmp;

	tmp = rum_read(sc, RT2573_TXRX_CSR4);

	tmp &= ~RT2573_SHORT_PREAMBLE;
	if (sc->sc_ic.ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= RT2573_SHORT_PREAMBLE;

	rum_write(sc, RT2573_TXRX_CSR4, tmp);
}

void
rum_set_basicrates(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* update basic rate set */
	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		/* 11b basic rates: 1, 2Mbps */
		rum_write(sc, RT2573_TXRX_CSR5, 0x3);
	} else if (ic->ic_curmode == IEEE80211_MODE_11A) {
		/* 11a basic rates: 6, 12, 24Mbps */
		rum_write(sc, RT2573_TXRX_CSR5, 0x150);
	} else {
		/* 11b/g basic rates: 1, 2, 5.5, 11Mbps */
		rum_write(sc, RT2573_TXRX_CSR5, 0xf);
	}
}

/*
 * Reprogram MAC/BBP to switch to a new band.  Values taken from the reference
 * driver.
 */
void
rum_select_band(struct rum_softc *sc, struct ieee80211_channel *c)
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
	rum_bbp_write(sc,  17, bbp17);
	rum_bbp_write(sc,  96, bbp96);
	rum_bbp_write(sc, 104, bbp104);

	if ((IEEE80211_IS_CHAN_2GHZ(c) && sc->ext_2ghz_lna) ||
	    (IEEE80211_IS_CHAN_5GHZ(c) && sc->ext_5ghz_lna)) {
		rum_bbp_write(sc, 75, 0x80);
		rum_bbp_write(sc, 86, 0x80);
		rum_bbp_write(sc, 88, 0x80);
	}

	rum_bbp_write(sc, 35, bbp35);
	rum_bbp_write(sc, 97, bbp97);
	rum_bbp_write(sc, 98, bbp98);

	tmp = rum_read(sc, RT2573_PHY_CSR0);
	tmp &= ~(RT2573_PA_PE_2GHZ | RT2573_PA_PE_5GHZ);
	if (IEEE80211_IS_CHAN_2GHZ(c))
		tmp |= RT2573_PA_PE_2GHZ;
	else
		tmp |= RT2573_PA_PE_5GHZ;
	rum_write(sc, RT2573_PHY_CSR0, tmp);

	/* 802.11a uses a 16 microseconds short interframe space */
	sc->sifs = IEEE80211_IS_CHAN_5GHZ(c) ? 16 : 10;
}

void
rum_set_chan(struct rum_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct rfprog *rfprog;
	uint8_t bbp3, bbp94 = RT2573_BBPR94_DEFAULT;
	int8_t power;
	u_int i, chan;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return;

	/* select the appropriate RF settings based on what EEPROM says */
	rfprog = (sc->rf_rev == RT2573_RF_5225 ||
		  sc->rf_rev == RT2573_RF_2527) ? rum_rf5225 : rum_rf5226;

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
		rum_select_band(sc, c);
		rum_select_antenna(sc);
	}
	sc->sc_curchan = c;

	rum_rf_write(sc, RT2573_RF1, rfprog[i].r1);
	rum_rf_write(sc, RT2573_RF2, rfprog[i].r2);
	rum_rf_write(sc, RT2573_RF3, rfprog[i].r3 | power << 7);
	rum_rf_write(sc, RT2573_RF4, rfprog[i].r4 | sc->rffreq << 10);

	rum_rf_write(sc, RT2573_RF1, rfprog[i].r1);
	rum_rf_write(sc, RT2573_RF2, rfprog[i].r2);
	rum_rf_write(sc, RT2573_RF3, rfprog[i].r3 | power << 7 | 1);
	rum_rf_write(sc, RT2573_RF4, rfprog[i].r4 | sc->rffreq << 10);

	rum_rf_write(sc, RT2573_RF1, rfprog[i].r1);
	rum_rf_write(sc, RT2573_RF2, rfprog[i].r2);
	rum_rf_write(sc, RT2573_RF3, rfprog[i].r3 | power << 7);
	rum_rf_write(sc, RT2573_RF4, rfprog[i].r4 | sc->rffreq << 10);

	DELAY(10);

	/* enable smart mode for MIMO-capable RFs */
	bbp3 = rum_bbp_read(sc, 3);

	bbp3 &= ~RT2573_SMART_MODE;
	if (sc->rf_rev == RT2573_RF_5225 || sc->rf_rev == RT2573_RF_2527)
		bbp3 |= RT2573_SMART_MODE;

	rum_bbp_write(sc, 3, bbp3);

	if (bbp94 != RT2573_BBPR94_DEFAULT)
		rum_bbp_write(sc, 94, bbp94);
}

/*
 * Enable TSF synchronization and tell h/w to start sending beacons for IBSS
 * and HostAP operating modes.
 */
void
rum_enable_tsf_sync(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode != IEEE80211_M_STA) {
		/*
		 * Change default 16ms TBTT adjustment to 8ms.
		 * Must be done before enabling beacon generation.
		 */
		rum_write(sc, RT2573_TXRX_CSR10, 1 << 12 | 8);
	}
#endif

	tmp = rum_read(sc, RT2573_TXRX_CSR9) & 0xff000000;

	/* set beacon interval (in 1/16ms unit) */
	tmp |= ic->ic_bss->ni_intval * 16;

	tmp |= RT2573_TSF_TICKING | RT2573_ENABLE_TBTT;
	if (ic->ic_opmode == IEEE80211_M_STA)
		tmp |= RT2573_TSF_MODE(1);
#ifndef IEEE80211_STA_ONLY
	else
		tmp |= RT2573_TSF_MODE(2) | RT2573_GENERATE_BEACON;
#endif
	rum_write(sc, RT2573_TXRX_CSR9, tmp);
}

void
rum_update_slot(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t slottime;
	uint32_t tmp;

	slottime = (ic->ic_flags & IEEE80211_F_SHSLOT) ?
	    IEEE80211_DUR_DS_SHSLOT : IEEE80211_DUR_DS_SLOT;

	tmp = rum_read(sc, RT2573_MAC_CSR9);
	tmp = (tmp & ~0xff) | slottime;
	rum_write(sc, RT2573_MAC_CSR9, tmp);

	DPRINTF(("setting slot time to %uus\n", slottime));
}

void
rum_set_bssid(struct rum_softc *sc, const uint8_t *bssid)
{
	uint32_t tmp;

	tmp = bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24;
	rum_write(sc, RT2573_MAC_CSR4, tmp);

	tmp = bssid[4] | bssid[5] << 8 | RT2573_ONE_BSSID << 16;
	rum_write(sc, RT2573_MAC_CSR5, tmp);
}

void
rum_set_macaddr(struct rum_softc *sc, const uint8_t *addr)
{
	uint32_t tmp;

	tmp = addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24;
	rum_write(sc, RT2573_MAC_CSR2, tmp);

	tmp = addr[4] | addr[5] << 8 | 0xff << 16;
	rum_write(sc, RT2573_MAC_CSR3, tmp);
}

void
rum_update_promisc(struct rum_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t tmp;

	tmp = rum_read(sc, RT2573_TXRX_CSR0);

	tmp &= ~RT2573_DROP_NOT_TO_ME;
	if (!(ifp->if_flags & IFF_PROMISC))
		tmp |= RT2573_DROP_NOT_TO_ME;

	rum_write(sc, RT2573_TXRX_CSR0, tmp);

	DPRINTF(("%s promiscuous mode\n", (ifp->if_flags & IFF_PROMISC) ?
	    "entering" : "leaving"));
}

const char *
rum_get_rf(int rev)
{
	switch (rev) {
	case RT2573_RF_2527:	return "RT2527 (MIMO XR)";
	case RT2573_RF_2528:	return "RT2528";
	case RT2573_RF_5225:	return "RT5225 (MIMO XR)";
	case RT2573_RF_5226:	return "RT5226";
	default:		return "unknown";
	}
}

void
rum_read_eeprom(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t val;
#ifdef RUM_DEBUG
	int i;
#endif

	/* read MAC/BBP type */
	rum_eeprom_read(sc, RT2573_EEPROM_MACBBP, &val, 2);
	sc->macbbp_rev = letoh16(val);

	/* read MAC address */
	rum_eeprom_read(sc, RT2573_EEPROM_ADDRESS, ic->ic_myaddr, 6);

	rum_eeprom_read(sc, RT2573_EEPROM_ANTENNA, &val, 2);
	val = letoh16(val);
	sc->rf_rev =   (val >> 11) & 0x1f;
	sc->hw_radio = (val >> 10) & 0x1;
	sc->rx_ant =   (val >> 4)  & 0x3;
	sc->tx_ant =   (val >> 2)  & 0x3;
	sc->nb_ant =   val & 0x3;

	DPRINTF(("RF revision=%d\n", sc->rf_rev));

	rum_eeprom_read(sc, RT2573_EEPROM_CONFIG2, &val, 2);
	val = letoh16(val);
	sc->ext_5ghz_lna = (val >> 6) & 0x1;
	sc->ext_2ghz_lna = (val >> 4) & 0x1;

	DPRINTF(("External 2GHz LNA=%d\nExternal 5GHz LNA=%d\n",
	    sc->ext_2ghz_lna, sc->ext_5ghz_lna));

	rum_eeprom_read(sc, RT2573_EEPROM_RSSI_2GHZ_OFFSET, &val, 2);
	val = letoh16(val);
	if ((val & 0xff) != 0xff)
		sc->rssi_2ghz_corr = (int8_t)(val & 0xff);	/* signed */

	rum_eeprom_read(sc, RT2573_EEPROM_RSSI_5GHZ_OFFSET, &val, 2);
	val = letoh16(val);
	if ((val & 0xff) != 0xff)
		sc->rssi_5ghz_corr = (int8_t)(val & 0xff);	/* signed */

	DPRINTF(("RSSI 2GHz corr=%d\nRSSI 5GHz corr=%d\n",
	    sc->rssi_2ghz_corr, sc->rssi_5ghz_corr));

	rum_eeprom_read(sc, RT2573_EEPROM_FREQ_OFFSET, &val, 2);
	val = letoh16(val);
	if ((val & 0xff) != 0xff)
		sc->rffreq = val & 0xff;

	DPRINTF(("RF freq=%d\n", sc->rffreq));

	/* read Tx power for all a/b/g channels */
	rum_eeprom_read(sc, RT2573_EEPROM_TXPOWER, sc->txpow, 14);
	/* XXX default Tx power for 802.11a channels */
	memset(sc->txpow + 14, 24, sizeof (sc->txpow) - 14);
#ifdef RUM_DEBUG
	for (i = 0; i < 14; i++)
		DPRINTF(("Channel=%d Tx power=%d\n", i + 1,  sc->txpow[i]));
#endif

	/* read default values for BBP registers */
	rum_eeprom_read(sc, RT2573_EEPROM_BBP_BASE, sc->bbp_prom, 2 * 16);
#ifdef RUM_DEBUG
	for (i = 0; i < 14; i++) {
		if (sc->bbp_prom[i].reg == 0 || sc->bbp_prom[i].reg == 0xff)
			continue;
		DPRINTF(("BBP R%d=%02x\n", sc->bbp_prom[i].reg,
		    sc->bbp_prom[i].val));
	}
#endif
}

int
rum_bbp_init(struct rum_softc *sc)
{
	int i, ntries;

	/* wait for BBP to be ready */
	for (ntries = 0; ntries < 100; ntries++) {
		const uint8_t val = rum_bbp_read(sc, 0);
		if (val != 0 && val != 0xff)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for BBP\n",
		    sc->sc_dev.dv_xname);
		return EIO;
	}

	/* initialize BBP registers to default values */
	for (i = 0; i < nitems(rum_def_bbp); i++)
		rum_bbp_write(sc, rum_def_bbp[i].reg, rum_def_bbp[i].val);

	/* write vendor-specific BBP values (from EEPROM) */
	for (i = 0; i < 16; i++) {
		if (sc->bbp_prom[i].reg == 0 || sc->bbp_prom[i].reg == 0xff)
			continue;
		rum_bbp_write(sc, sc->bbp_prom[i].reg, sc->bbp_prom[i].val);
	}

	return 0;
}

int
rum_init(struct ifnet *ifp)
{
	struct rum_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	usbd_status error;
	int i, ntries;

	rum_stop(ifp, 0);

	/* initialize MAC registers to default values */
	for (i = 0; i < nitems(rum_def_mac); i++)
		rum_write(sc, rum_def_mac[i].reg, rum_def_mac[i].val);

	/* set host ready */
	rum_write(sc, RT2573_MAC_CSR1, 3);
	rum_write(sc, RT2573_MAC_CSR1, 0);

	/* wait for BBP/RF to wakeup */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (rum_read(sc, RT2573_MAC_CSR12) & 8)
			break;
		rum_write(sc, RT2573_MAC_CSR12, 4);	/* force wakeup */
		DELAY(1000);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for BBP/RF to wakeup\n",
		    sc->sc_dev.dv_xname);
		error = ENODEV;
		goto fail;
	}

	if ((error = rum_bbp_init(sc)) != 0)
		goto fail;

	/* select default channel */
	sc->sc_curchan = ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	rum_select_band(sc, sc->sc_curchan);
	rum_select_antenna(sc);
	rum_set_chan(sc, sc->sc_curchan);

	/* clear STA registers */
	rum_read_multi(sc, RT2573_STA_CSR0, sc->sta, sizeof sc->sta);

	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	rum_set_macaddr(sc, ic->ic_myaddr);

	/* initialize ASIC */
	rum_write(sc, RT2573_MAC_CSR1, 4);

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
	error = rum_alloc_tx_list(sc);
	if (error != 0) {
		printf("%s: could not allocate Tx list\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	error = rum_alloc_rx_list(sc);
	if (error != 0) {
		printf("%s: could not allocate Rx list\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/*
	 * Start up the receive pipe.
	 */
	for (i = 0; i < RUM_RX_LIST_COUNT; i++) {
		struct rum_rx_data *data = &sc->rx_data[i];

		usbd_setup_xfer(data->xfer, sc->sc_rx_pipeh, data, data->buf,
		    MCLBYTES, USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, rum_rxeof);
		error = usbd_transfer(data->xfer);
		if (error != 0 && error != USBD_IN_PROGRESS) {
			printf("%s: could not queue Rx transfer\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}

	/* update Rx filter */
	tmp = rum_read(sc, RT2573_TXRX_CSR0) & 0xffff;

	tmp |= RT2573_DROP_PHY_ERROR | RT2573_DROP_CRC_ERROR;
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RT2573_DROP_CTL | RT2573_DROP_VER_ERROR |
		       RT2573_DROP_ACKCTS;
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode != IEEE80211_M_HOSTAP)
#endif
			tmp |= RT2573_DROP_TODS;
		if (!(ifp->if_flags & IFF_PROMISC))
			tmp |= RT2573_DROP_NOT_TO_ME;
	}
	rum_write(sc, RT2573_TXRX_CSR0, tmp);

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return 0;

fail:	rum_stop(ifp, 1);
	return error;
}

void
rum_stop(struct ifnet *ifp, int disable)
{
	struct rum_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);	/* free all nodes */

	/* disable Rx */
	tmp = rum_read(sc, RT2573_TXRX_CSR0);
	rum_write(sc, RT2573_TXRX_CSR0, tmp | RT2573_DISABLE_RX);

	/* reset ASIC */
	rum_write(sc, RT2573_MAC_CSR1, 3);
	rum_write(sc, RT2573_MAC_CSR1, 0);

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

	rum_free_rx_list(sc);
	rum_free_tx_list(sc);
}

int
rum_load_microcode(struct rum_softc *sc, const u_char *ucode, size_t size)
{
	usb_device_request_t req;
	uint16_t reg = RT2573_MCU_CODE_BASE;
	usbd_status error;

	/* copy firmware image into NIC */
	for (; size >= 4; reg += 4, ucode += 4, size -= 4)
		rum_write(sc, reg, UGETDW(ucode));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2573_MCU_CNTL;
	USETW(req.wValue, RT2573_MCU_RUN);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	error = usbd_do_request(sc->sc_udev, &req, NULL);
	if (error != 0) {
		printf("%s: could not run firmware: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
	}
	return error;
}

#ifndef IEEE80211_STA_ONLY
int
rum_prepare_beacon(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct rum_tx_desc desc;
	struct mbuf *m0;
	int rate;

	m0 = ieee80211_beacon_alloc(ic, ic->ic_bss);
	if (m0 == NULL) {
		printf("%s: could not allocate beacon frame\n",
		    sc->sc_dev.dv_xname);
		return ENOBUFS;
	}

	/* send beacons at the lowest available rate */
	rate = IEEE80211_IS_CHAN_5GHZ(ic->ic_bss->ni_chan) ? 12 : 2;

	rum_setup_tx_desc(sc, &desc, RT2573_TX_TIMESTAMP, RT2573_TX_HWSEQ,
	    m0->m_pkthdr.len, rate);

	/* copy the first 24 bytes of Tx descriptor into NIC memory */
	rum_write_multi(sc, RT2573_HW_BEACON_BASE0, (uint8_t *)&desc, 24);

	/* copy beacon header and payload into NIC memory */
	rum_write_multi(sc, RT2573_HW_BEACON_BASE0 + 24, mtod(m0, uint8_t *),
	    m0->m_pkthdr.len);

	m_freem(m0);

	return 0;
}
#endif

void
rum_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	/* start with lowest Tx rate */
	ni->ni_txrate = 0;
}

void
rum_amrr_start(struct rum_softc *sc, struct ieee80211_node *ni)
{
	int i;

	/* clear statistic registers (STA_CSR0 to STA_CSR5) */
	rum_read_multi(sc, RT2573_STA_CSR0, sc->sta, sizeof sc->sta);

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
rum_amrr_timeout(void *arg)
{
	struct rum_softc *sc = arg;
	usb_device_request_t req;

	if (usbd_is_dying(sc->sc_udev))
		return;

	/*
	 * Asynchronously read statistic registers (cleared by read).
	 */
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2573_READ_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, RT2573_STA_CSR0);
	USETW(req.wLength, sizeof sc->sta);

	usbd_setup_default_xfer(sc->amrr_xfer, sc->sc_udev, sc,
	    USBD_DEFAULT_TIMEOUT, &req, sc->sta, sizeof sc->sta, 0,
	    rum_amrr_update);
	(void)usbd_transfer(sc->amrr_xfer);
}

void
rum_amrr_update(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct rum_softc *sc = (struct rum_softc *)priv;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (status != USBD_NORMAL_COMPLETION) {
		printf("%s: could not retrieve Tx statistics - cancelling "
		    "automatic rate control\n", sc->sc_dev.dv_xname);
		return;
	}

	/* count TX retry-fail as Tx errors */
	ifp->if_oerrors += letoh32(sc->sta[5]) >> 16;

	sc->amn.amn_retrycnt =
	    (letoh32(sc->sta[4]) >> 16) +	/* TX one-retry ok count */
	    (letoh32(sc->sta[5]) & 0xffff) +	/* TX more-retry ok count */
	    (letoh32(sc->sta[5]) >> 16);	/* TX retry-fail count */

	sc->amn.amn_txcnt =
	    sc->amn.amn_retrycnt +
	    (letoh32(sc->sta[4]) & 0xffff);	/* TX no-retry ok count */

	ieee80211_amrr_choose(&sc->amrr, sc->sc_ic.ic_bss, &sc->amn);

	if (!usbd_is_dying(sc->sc_udev))
		timeout_add_sec(&sc->amrr_to, 1);
}

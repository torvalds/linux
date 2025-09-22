/*	$OpenBSD: if_zyd.c,v 1.129 2024/05/23 03:21:09 jsg Exp $	*/

/*-
 * Copyright (c) 2006 by Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2006 by Florian Stoehr <ich@florian-stoehr.de>
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
 * ZyDAS ZD1211/ZD1211B USB WLAN driver.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/timeout.h>
#include <sys/device.h>
#include <sys/endian.h>

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
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_zydreg.h>

#ifdef ZYD_DEBUG
#define DPRINTF(x)	do { if (zyddebug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (zyddebug > (n)) printf x; } while (0)
int zyddebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

static const struct zyd_phy_pair zyd_def_phy[] = ZYD_DEF_PHY;
static const struct zyd_phy_pair zyd_def_phyB[] = ZYD_DEF_PHYB;

/* various supported device vendors/products */
#define ZYD_ZD1211_DEV(v, p)	\
	{ { USB_VENDOR_##v, USB_PRODUCT_##v##_##p }, ZYD_ZD1211 }
#define ZYD_ZD1211B_DEV(v, p)	\
	{ { USB_VENDOR_##v, USB_PRODUCT_##v##_##p }, ZYD_ZD1211B }
static const struct zyd_type {
	struct usb_devno	dev;
	uint8_t			rev;
#define ZYD_ZD1211	0
#define ZYD_ZD1211B	1
} zyd_devs[] = {
	ZYD_ZD1211_DEV(3COM2,		3CRUSB10075),
	ZYD_ZD1211_DEV(ABOCOM,		WL54),
	ZYD_ZD1211_DEV(ASUS,		WL159G),
	ZYD_ZD1211_DEV(CYBERTAN,	TG54USB),
	ZYD_ZD1211_DEV(DRAYTEK,		VIGOR550),
	ZYD_ZD1211_DEV(PLANEX2,		GWUS54GD),
	ZYD_ZD1211_DEV(PLANEX2,		GWUS54GZL),
	ZYD_ZD1211_DEV(PLANEX3,		GWUS54GZ),
	ZYD_ZD1211_DEV(PLANEX3,		GWUS54MINI),
	ZYD_ZD1211_DEV(SAGEM,		XG760A),
	ZYD_ZD1211_DEV(SENAO,		NUB8301),
	ZYD_ZD1211_DEV(SITECOMEU,	WL113),
	ZYD_ZD1211_DEV(SWEEX,		ZD1211),
	ZYD_ZD1211_DEV(TEKRAM,		QUICKWLAN),
	ZYD_ZD1211_DEV(TEKRAM,		ZD1211_1),
	ZYD_ZD1211_DEV(TEKRAM,		ZD1211_2),
	ZYD_ZD1211_DEV(TWINMOS,		G240),
	ZYD_ZD1211_DEV(UMEDIA,		ALL0298V2),
	ZYD_ZD1211_DEV(UMEDIA,		TEW429UB_A),
	ZYD_ZD1211_DEV(UMEDIA,		TEW429UB),
	ZYD_ZD1211_DEV(UNKNOWN2,	NW3100),
	ZYD_ZD1211_DEV(WISTRONNEWEB,	UR055G),
	ZYD_ZD1211_DEV(ZCOM,		ZD1211),
	ZYD_ZD1211_DEV(ZYDAS,		ALL0298),
	ZYD_ZD1211_DEV(ZYDAS,		ZD1211),
	ZYD_ZD1211_DEV(ZYXEL,		AG225H),
	ZYD_ZD1211_DEV(ZYXEL,		G200V2),
	ZYD_ZD1211_DEV(ZYXEL,		G202),
	ZYD_ZD1211_DEV(ZYXEL,		G220),
	ZYD_ZD1211_DEV(ZYXEL,		G220F),

	ZYD_ZD1211B_DEV(ACCTON,		SMCWUSBG),
	ZYD_ZD1211B_DEV(ACCTON,		WN4501H_LF_IR),
	ZYD_ZD1211B_DEV(ACCTON,		WUS201),
	ZYD_ZD1211B_DEV(ACCTON,		ZD1211B),
	ZYD_ZD1211B_DEV(ASUS,		A9T_WIFI),
	ZYD_ZD1211B_DEV(BELKIN,		F5D7050C),
	ZYD_ZD1211B_DEV(BELKIN,		ZD1211B),
	ZYD_ZD1211B_DEV(BEWAN,		BWIFI_USB54AR),
	ZYD_ZD1211B_DEV(CISCOLINKSYS,	WUSBF54G),
	ZYD_ZD1211B_DEV(CYBERTAN,	ZD1211B),
	ZYD_ZD1211B_DEV(FIBERLINE,	WL430U),
	ZYD_ZD1211B_DEV(MELCO,		KG54L),
	ZYD_ZD1211B_DEV(PHILIPS,	SNU5600),
	ZYD_ZD1211B_DEV(PHILIPS,	SNU5630NS05),
	ZYD_ZD1211B_DEV(PLANEX2,	GW_US54GXS),
	ZYD_ZD1211B_DEV(PLANEX4,	GWUS54ZGL),
	ZYD_ZD1211B_DEV(PLANEX4,	ZD1211B),
	ZYD_ZD1211B_DEV(SAGEM,		XG76NA),
	ZYD_ZD1211B_DEV(SITECOMEU,	WL603),
	ZYD_ZD1211B_DEV(SITECOMEU,	ZD1211B),
	ZYD_ZD1211B_DEV(UMEDIA,		TEW429UBC1),
	ZYD_ZD1211B_DEV(UNKNOWN2,	ZD1211B),
	ZYD_ZD1211B_DEV(UNKNOWN3,	ZD1211B),
	ZYD_ZD1211B_DEV(SONY,		IFU_WLM2),
	ZYD_ZD1211B_DEV(USR,		USR5423),
	ZYD_ZD1211B_DEV(VTECH,		ZD1211B),
	ZYD_ZD1211B_DEV(ZCOM,		ZD1211B),
	ZYD_ZD1211B_DEV(ZYDAS,		ZD1211B),
	ZYD_ZD1211B_DEV(ZYDAS,		ZD1211B_2),
	ZYD_ZD1211B_DEV(ZYXEL,		AG220),
	ZYD_ZD1211B_DEV(ZYXEL,		AG225HV2),
	ZYD_ZD1211B_DEV(ZYXEL,		G220V2),
	ZYD_ZD1211B_DEV(ZYXEL,		M202)
};
#define zyd_lookup(v, p)	\
	((const struct zyd_type *)usb_lookup(zyd_devs, v, p))

int zyd_match(struct device *, void *, void *);
void zyd_attach(struct device *, struct device *, void *);
int zyd_detach(struct device *, int);

struct cfdriver zyd_cd = {
	NULL, "zyd", DV_IFNET
};

const struct cfattach zyd_ca = {
	sizeof(struct zyd_softc), zyd_match, zyd_attach, zyd_detach
};

void		zyd_attachhook(struct device *);
int		zyd_complete_attach(struct zyd_softc *);
int		zyd_open_pipes(struct zyd_softc *);
void		zyd_close_pipes(struct zyd_softc *);
int		zyd_alloc_tx_list(struct zyd_softc *);
void		zyd_free_tx_list(struct zyd_softc *);
int		zyd_alloc_rx_list(struct zyd_softc *);
void		zyd_free_rx_list(struct zyd_softc *);
struct		ieee80211_node *zyd_node_alloc(struct ieee80211com *);
int		zyd_media_change(struct ifnet *);
void		zyd_next_scan(void *);
void		zyd_task(void *);
int		zyd_newstate(struct ieee80211com *, enum ieee80211_state, int);
int		zyd_cmd_read(struct zyd_softc *, const void *, size_t, int);
int		zyd_read16(struct zyd_softc *, uint16_t, uint16_t *);
int		zyd_read32(struct zyd_softc *, uint16_t, uint32_t *);
int		zyd_cmd_write(struct zyd_softc *, u_int16_t, const void *, int);
int		zyd_write16(struct zyd_softc *, uint16_t, uint16_t);
int		zyd_write32(struct zyd_softc *, uint16_t, uint32_t);
int		zyd_rfwrite(struct zyd_softc *, uint32_t);
void		zyd_lock_phy(struct zyd_softc *);
void		zyd_unlock_phy(struct zyd_softc *);
int		zyd_rfmd_init(struct zyd_rf *);
int		zyd_rfmd_switch_radio(struct zyd_rf *, int);
int		zyd_rfmd_set_channel(struct zyd_rf *, uint8_t);
int		zyd_al2230_init(struct zyd_rf *);
int		zyd_al2230_switch_radio(struct zyd_rf *, int);
int		zyd_al2230_set_channel(struct zyd_rf *, uint8_t);
int		zyd_al2230_init_b(struct zyd_rf *);
int		zyd_al7230B_init(struct zyd_rf *);
int		zyd_al7230B_switch_radio(struct zyd_rf *, int);
int		zyd_al7230B_set_channel(struct zyd_rf *, uint8_t);
int		zyd_al2210_init(struct zyd_rf *);
int		zyd_al2210_switch_radio(struct zyd_rf *, int);
int		zyd_al2210_set_channel(struct zyd_rf *, uint8_t);
int		zyd_gct_init(struct zyd_rf *);
int		zyd_gct_switch_radio(struct zyd_rf *, int);
int		zyd_gct_set_channel(struct zyd_rf *, uint8_t);
int		zyd_maxim_init(struct zyd_rf *);
int		zyd_maxim_switch_radio(struct zyd_rf *, int);
int		zyd_maxim_set_channel(struct zyd_rf *, uint8_t);
int		zyd_maxim2_init(struct zyd_rf *);
int		zyd_maxim2_switch_radio(struct zyd_rf *, int);
int		zyd_maxim2_set_channel(struct zyd_rf *, uint8_t);
int		zyd_rf_attach(struct zyd_softc *, uint8_t);
const char	*zyd_rf_name(uint8_t);
int		zyd_hw_init(struct zyd_softc *);
int		zyd_read_eeprom(struct zyd_softc *);
void		zyd_set_multi(struct zyd_softc *);
void		zyd_set_macaddr(struct zyd_softc *, const uint8_t *);
void		zyd_set_bssid(struct zyd_softc *, const uint8_t *);
int		zyd_switch_radio(struct zyd_softc *, int);
void		zyd_set_led(struct zyd_softc *, int, int);
int		zyd_set_rxfilter(struct zyd_softc *);
void		zyd_set_chan(struct zyd_softc *, struct ieee80211_channel *);
int		zyd_set_beacon_interval(struct zyd_softc *, int);
uint8_t		zyd_plcp_signal(int);
void		zyd_intr(struct usbd_xfer *, void *, usbd_status);
void		zyd_rx_data(struct zyd_softc *, const uint8_t *, uint16_t,
		    struct mbuf_list *);
void		zyd_rxeof(struct usbd_xfer *, void *, usbd_status);
void		zyd_txeof(struct usbd_xfer *, void *, usbd_status);
int		zyd_tx(struct zyd_softc *, struct mbuf *,
		    struct ieee80211_node *);
void		zyd_start(struct ifnet *);
void		zyd_watchdog(struct ifnet *);
int		zyd_ioctl(struct ifnet *, u_long, caddr_t);
int		zyd_init(struct ifnet *);
void		zyd_stop(struct ifnet *, int);
int		zyd_loadfirmware(struct zyd_softc *, u_char *, size_t);
void		zyd_iter_func(void *, struct ieee80211_node *);
void		zyd_amrr_timeout(void *);
void		zyd_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);

int
zyd_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != ZYD_CONFIG_NO)
		return UMATCH_NONE;

	return (zyd_lookup(uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
zyd_attachhook(struct device *self)
{
	struct zyd_softc *sc = (struct zyd_softc *)self;
	const char *fwname;
	u_char *fw;
	size_t fwsize;
	int error;

	fwname = (sc->mac_rev == ZYD_ZD1211) ? "zd1211" : "zd1211b";
	if ((error = loadfirmware(fwname, &fw, &fwsize)) != 0) {
		printf("%s: error %d, could not read firmware file %s\n",
		    sc->sc_dev.dv_xname, error, fwname);
		return;
	}

	error = zyd_loadfirmware(sc, fw, fwsize);
	free(fw, M_DEVBUF, fwsize);
	if (error != 0) {
		printf("%s: could not load firmware (error=%d)\n",
		    sc->sc_dev.dv_xname, error);
		return;
	}

	/* complete the attach process */
	if (zyd_complete_attach(sc) == 0)
		sc->attached = 1;
}

void
zyd_attach(struct device *parent, struct device *self, void *aux)
{
	struct zyd_softc *sc = (struct zyd_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_device_descriptor_t* ddesc;

	sc->sc_udev = uaa->device;
	sc->sc_iface = uaa->iface;

	sc->mac_rev = zyd_lookup(uaa->vendor, uaa->product)->rev;

	ddesc = usbd_get_device_descriptor(sc->sc_udev);
	if (UGETW(ddesc->bcdDevice) < 0x4330) {
		printf("%s: device version mismatch: 0x%x "
		    "(only >= 43.30 supported)\n", sc->sc_dev.dv_xname,
		    UGETW(ddesc->bcdDevice));
		return;
	}

	config_mountroot(self, zyd_attachhook);
}

int
zyd_complete_attach(struct zyd_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	usbd_status error;
	int i;

	usb_init_task(&sc->sc_task, zyd_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->scan_to, zyd_next_scan, sc);

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 10;
	timeout_set(&sc->amrr_to, zyd_amrr_timeout, sc);

	error = usbd_set_config_no(sc->sc_udev, ZYD_CONFIG_NO, 1);
	if (error != 0) {
		printf("%s: setting config no failed\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = usbd_device2interface_handle(sc->sc_udev, ZYD_IFACE_INDEX,
	    &sc->sc_iface);
	if (error != 0) {
		printf("%s: getting interface handle failed\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	if ((error = zyd_open_pipes(sc)) != 0) {
		printf("%s: could not open pipes\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	if ((error = zyd_read_eeprom(sc)) != 0) {
		printf("%s: could not read EEPROM\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	if ((error = zyd_rf_attach(sc, sc->rf_rev)) != 0) {
		printf("%s: could not attach RF\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	if ((error = zyd_hw_init(sc)) != 0) {
		printf("%s: hardware initialization failed\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	printf("%s: HMAC ZD1211%s, FW %02x.%02x, RF %s, PA %x, address %s\n",
	    sc->sc_dev.dv_xname, (sc->mac_rev == ZYD_ZD1211) ? "": "B",
	    sc->fw_rev >> 8, sc->fw_rev & 0xff, zyd_rf_name(sc->rf_rev),
	    sc->pa_rev, ether_sprintf(ic->ic_myaddr));

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_TXPMGT |	/* tx power management */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
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
	ifp->if_ioctl = zyd_ioctl;
	ifp->if_start = zyd_start;
	ifp->if_watchdog = zyd_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_node_alloc = zyd_node_alloc;
	ic->ic_newassoc = zyd_newassoc;

	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = zyd_newstate;
	ieee80211_media_init(ifp, zyd_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(ZYD_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(ZYD_TX_RADIOTAP_PRESENT);
#endif

fail:	return error;
}

int
zyd_detach(struct device *self, int flags)
{
	struct zyd_softc *sc = (struct zyd_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s;

	s = splusb();

	usb_rem_task(sc->sc_udev, &sc->sc_task);
	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
	if (timeout_initialized(&sc->amrr_to))
		timeout_del(&sc->amrr_to);

	zyd_close_pipes(sc);

	if (!sc->attached) {
		splx(s);
		return 0;
	}

	zyd_free_rx_list(sc);
	zyd_free_tx_list(sc);

	if (ifp->if_softc != NULL) {
		ieee80211_ifdetach(ifp);
		if_detach(ifp);
	}

	sc->attached = 0;

	splx(s);

	return 0;
}

int
zyd_open_pipes(struct zyd_softc *sc)
{
	usb_endpoint_descriptor_t *edesc;
	int isize;
	usbd_status error;

	/* interrupt in */
	edesc = usbd_get_endpoint_descriptor(sc->sc_iface, 0x83);
	if (edesc == NULL)
		return EINVAL;

	isize = UGETW(edesc->wMaxPacketSize);
	if (isize == 0)	/* should not happen */
		return EINVAL;

	sc->ibuf = malloc(isize, M_USBDEV, M_NOWAIT);
	if (sc->ibuf == NULL)
		return ENOMEM;
	sc->ibuflen = isize;
	error = usbd_open_pipe_intr(sc->sc_iface, 0x83, USBD_SHORT_XFER_OK,
	    &sc->zyd_ep[ZYD_ENDPT_IIN], sc, sc->ibuf, isize, zyd_intr,
	    USBD_DEFAULT_INTERVAL);
	if (error != 0) {
		printf("%s: open rx intr pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	/* interrupt out (not necessarily an interrupt pipe) */
	error = usbd_open_pipe(sc->sc_iface, 0x04, USBD_EXCLUSIVE_USE,
	    &sc->zyd_ep[ZYD_ENDPT_IOUT]);
	if (error != 0) {
		printf("%s: open tx intr pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	/* bulk in */
	error = usbd_open_pipe(sc->sc_iface, 0x82, USBD_EXCLUSIVE_USE,
	    &sc->zyd_ep[ZYD_ENDPT_BIN]);
	if (error != 0) {
		printf("%s: open rx pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	/* bulk out */
	error = usbd_open_pipe(sc->sc_iface, 0x01, USBD_EXCLUSIVE_USE,
	    &sc->zyd_ep[ZYD_ENDPT_BOUT]);
	if (error != 0) {
		printf("%s: open tx pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	return 0;

fail:	zyd_close_pipes(sc);
	return error;
}

void
zyd_close_pipes(struct zyd_softc *sc)
{
	int i;

	for (i = 0; i < ZYD_ENDPT_CNT; i++) {
		if (sc->zyd_ep[i] != NULL) {
			usbd_close_pipe(sc->zyd_ep[i]);
			sc->zyd_ep[i] = NULL;
		}
	}
	if (sc->ibuf != NULL) {
		free(sc->ibuf, M_USBDEV, sc->ibuflen);
		sc->ibuf = NULL;
	}
}

int
zyd_alloc_tx_list(struct zyd_softc *sc)
{
	int i, error;

	sc->tx_queued = 0;

	for (i = 0; i < ZYD_TX_LIST_CNT; i++) {
		struct zyd_tx_data *data = &sc->tx_data[i];

		data->sc = sc;	/* backpointer for callbacks */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate tx xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		data->buf = usbd_alloc_buffer(data->xfer, ZYD_MAX_TXBUFSZ);
		if (data->buf == NULL) {
			printf("%s: could not allocate tx buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		/* clear Tx descriptor */
		bzero(data->buf, sizeof (struct zyd_tx_desc));
	}
	return 0;

fail:	zyd_free_tx_list(sc);
	return error;
}

void
zyd_free_tx_list(struct zyd_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int i;

	for (i = 0; i < ZYD_TX_LIST_CNT; i++) {
		struct zyd_tx_data *data = &sc->tx_data[i];

		if (data->xfer != NULL) {
			usbd_free_xfer(data->xfer);
			data->xfer = NULL;
		}
		if (data->ni != NULL) {
			ieee80211_release_node(ic, data->ni);
			data->ni = NULL;
		}
	}
}

int
zyd_alloc_rx_list(struct zyd_softc *sc)
{
	int i, error;

	for (i = 0; i < ZYD_RX_LIST_CNT; i++) {
		struct zyd_rx_data *data = &sc->rx_data[i];

		data->sc = sc;	/* backpointer for callbacks */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate rx xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		data->buf = usbd_alloc_buffer(data->xfer, ZYX_MAX_RXBUFSZ);
		if (data->buf == NULL) {
			printf("%s: could not allocate rx buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
	}
	return 0;

fail:	zyd_free_rx_list(sc);
	return error;
}

void
zyd_free_rx_list(struct zyd_softc *sc)
{
	int i;

	for (i = 0; i < ZYD_RX_LIST_CNT; i++) {
		struct zyd_rx_data *data = &sc->rx_data[i];

		if (data->xfer != NULL) {
			usbd_free_xfer(data->xfer);
			data->xfer = NULL;
		}
	}
}

struct ieee80211_node *
zyd_node_alloc(struct ieee80211com *ic)
{
	return malloc(sizeof (struct zyd_node), M_USBDEV, M_NOWAIT | M_ZERO);
}

int
zyd_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		error = zyd_init(ifp);

	return error;
}

/*
 * This function is called periodically (every 200ms) during scanning to
 * switch from one channel to another.
 */
void
zyd_next_scan(void *arg)
{
	struct zyd_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);
}

void
zyd_task(void *arg)
{
	struct zyd_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state ostate;

	ostate = ic->ic_state;

	switch (sc->sc_state) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* turn link LED off */
			zyd_set_led(sc, ZYD_LED1, 0);

			/* stop data LED from blinking */
			zyd_write32(sc, sc->fwbase + ZYD_FW_LINK_STATUS, 0);
		}
		break;

	case IEEE80211_S_SCAN:
		zyd_set_chan(sc, ic->ic_bss->ni_chan);
		timeout_add_msec(&sc->scan_to, 200);
		break;

	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		zyd_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_RUN:
	{
		struct ieee80211_node *ni = ic->ic_bss;

		zyd_set_chan(sc, ni->ni_chan);

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			/* turn link LED on */
			zyd_set_led(sc, ZYD_LED1, 1);

			/* make data LED blink upon Tx */
			zyd_write32(sc, sc->fwbase + ZYD_FW_LINK_STATUS, 1);

			zyd_set_bssid(sc, ni->ni_bssid);
		}

		if (ic->ic_opmode == IEEE80211_M_STA) {
			/* fake a join to init the tx rate */
			zyd_newassoc(ic, ni, 1);
		}

		/* start automatic rate control timer */
		if (ic->ic_fixed_rate == -1)
			timeout_add_sec(&sc->amrr_to, 1);

		break;
	}
	}

	sc->sc_newstate(ic, sc->sc_state, sc->sc_arg);
}

int
zyd_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct zyd_softc *sc = ic->ic_softc;

	usb_rem_task(sc->sc_udev, &sc->sc_task);
	timeout_del(&sc->scan_to);
	timeout_del(&sc->amrr_to);

	/* do it in a process context */
	sc->sc_state = nstate;
	sc->sc_arg = arg;
	usb_add_task(sc->sc_udev, &sc->sc_task);

	return 0;
}

/*
 * Issue a read command for the specified register (of size regsize)
 * and await a reply of olen bytes in sc->odata.
 */
int
zyd_cmd_read(struct zyd_softc *sc, const void *reg, size_t regsize, int olen)
{
	struct usbd_xfer *xfer;
	struct zyd_cmd cmd;
	usbd_status error;
	int s;

	if ((xfer = usbd_alloc_xfer(sc->sc_udev)) == NULL)
		return ENOMEM;

	bzero(&cmd, sizeof(cmd));
	cmd.code = htole16(ZYD_CMD_IORD);
	bcopy(reg, cmd.data, regsize);

	bzero(sc->odata, sizeof(sc->odata));
	sc->olen = olen;

	usbd_setup_xfer(xfer, sc->zyd_ep[ZYD_ENDPT_IOUT], 0,
	    &cmd, sizeof(cmd.code) + regsize,
	    USBD_FORCE_SHORT_XFER | USBD_SYNCHRONOUS,
	    ZYD_INTR_TIMEOUT, NULL);
	s = splusb();
	sc->odone = 0;
	error = usbd_transfer(xfer);
	splx(s);
	if (error) {
		printf("%s: could not send command: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		usbd_free_xfer(xfer);
		return EIO;
	}

	if (!sc->odone) {
		/* wait for ZYD_NOTIF_IORD interrupt */
		if (tsleep_nsec(sc, PWAIT, "zydcmd",
		    MSEC_TO_NSEC(ZYD_INTR_TIMEOUT)) != 0)
			printf("%s: read command failed\n",
			    sc->sc_dev.dv_xname);
	}
	usbd_free_xfer(xfer);

	return error;
}

int
zyd_read16(struct zyd_softc *sc, uint16_t reg, uint16_t *val)
{
	struct zyd_io *odata;
	int error;

	reg = htole16(reg);
	error = zyd_cmd_read(sc, &reg, sizeof(reg), sizeof(*odata));
	if (error == 0) {
		odata = (struct zyd_io *)sc->odata;
		*val = letoh16(odata[0].val);
	}
	return error;
}

int
zyd_read32(struct zyd_softc *sc, uint16_t reg, uint32_t *val)
{
	struct zyd_io *odata;
	uint16_t regs[2];
	int error;

	regs[0] = htole16(ZYD_REG32_HI(reg));
	regs[1] = htole16(ZYD_REG32_LO(reg));
	error = zyd_cmd_read(sc, regs, sizeof(regs), sizeof(*odata) * 2);
	if (error == 0) {
		odata = (struct zyd_io *)sc->odata;
		*val = letoh16(odata[0].val) << 16 | letoh16(odata[1].val);
	}
	return error;
}

int
zyd_cmd_write(struct zyd_softc *sc, u_int16_t code, const void *data, int len)
{
	struct usbd_xfer *xfer;
	struct zyd_cmd cmd;
	usbd_status error;

	if ((xfer = usbd_alloc_xfer(sc->sc_udev)) == NULL)
		return ENOMEM;

	bzero(&cmd, sizeof(cmd));
	cmd.code = htole16(code);
	bcopy(data, cmd.data, len);

	usbd_setup_xfer(xfer, sc->zyd_ep[ZYD_ENDPT_IOUT], 0,
	    &cmd, sizeof(cmd.code) + len,
	    USBD_FORCE_SHORT_XFER | USBD_SYNCHRONOUS,
	    ZYD_INTR_TIMEOUT, NULL);
	error = usbd_transfer(xfer);
	if (error)
		printf("%s: could not send command: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));

	usbd_free_xfer(xfer);
	return error;
}

int
zyd_write16(struct zyd_softc *sc, uint16_t reg, uint16_t val)
{ 
	struct zyd_io io;

	io.reg = htole16(reg);
	io.val = htole16(val);
	return zyd_cmd_write(sc, ZYD_CMD_IOWR, &io, sizeof(io));
}

int
zyd_write32(struct zyd_softc *sc, uint16_t reg, uint32_t val)
{
	struct zyd_io io[2];

	io[0].reg = htole16(ZYD_REG32_HI(reg));
	io[0].val = htole16(val >> 16);
	io[1].reg = htole16(ZYD_REG32_LO(reg));
	io[1].val = htole16(val & 0xffff);

	return zyd_cmd_write(sc, ZYD_CMD_IOWR, io, sizeof(io));
}

int
zyd_rfwrite(struct zyd_softc *sc, uint32_t val)
{
	struct zyd_rf *rf = &sc->sc_rf;
	struct zyd_rfwrite req;
	uint16_t cr203;
	int i;

	(void)zyd_read16(sc, ZYD_CR203, &cr203);
	cr203 &= ~(ZYD_RF_IF_LE | ZYD_RF_CLK | ZYD_RF_DATA);

	req.code  = htole16(2);
	req.width = htole16(rf->width);
	for (i = 0; i < rf->width; i++) {
		req.bit[i] = htole16(cr203);
		if (val & (1 << (rf->width - 1 - i)))
			req.bit[i] |= htole16(ZYD_RF_DATA);
	}
	return zyd_cmd_write(sc, ZYD_CMD_RFCFG, &req, 4 + 2 * rf->width);
}

void
zyd_lock_phy(struct zyd_softc *sc)
{
	uint32_t tmp;

	(void)zyd_read32(sc, ZYD_MAC_MISC, &tmp);
	tmp &= ~ZYD_UNLOCK_PHY_REGS;
	(void)zyd_write32(sc, ZYD_MAC_MISC, tmp);
}

void
zyd_unlock_phy(struct zyd_softc *sc)
{
	uint32_t tmp;

	(void)zyd_read32(sc, ZYD_MAC_MISC, &tmp);
	tmp |= ZYD_UNLOCK_PHY_REGS;
	(void)zyd_write32(sc, ZYD_MAC_MISC, tmp);
}

/*
 * RFMD RF methods.
 */
int
zyd_rfmd_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_RFMD_PHY;
	static const uint32_t rfini[] = ZYD_RFMD_RF;
	int i, error;

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++) {
		error = zyd_write16(sc, phyini[i].reg, phyini[i].val);
		if (error != 0)
			return error;
	}

	/* init RFMD radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return error;
	}
	return 0;
}

int
zyd_rfmd_switch_radio(struct zyd_rf *rf, int on)
{
	struct zyd_softc *sc = rf->rf_sc;

	(void)zyd_write16(sc, ZYD_CR10, on ? 0x89 : 0x15);
	(void)zyd_write16(sc, ZYD_CR11, on ? 0x00 : 0x81);

	return 0;
}

int
zyd_rfmd_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct {
		uint32_t	r1, r2;
	} rfprog[] = ZYD_RFMD_CHANTABLE;

	(void)zyd_rfwrite(sc, rfprog[chan - 1].r1);
	(void)zyd_rfwrite(sc, rfprog[chan - 1].r2);

	return 0;
}

/*
 * AL2230 RF methods.
 */
int
zyd_al2230_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_AL2230_PHY;
	static const struct zyd_phy_pair phy2230s[] = ZYD_AL2230S_PHY_INIT;
	static const uint32_t rfini[] = ZYD_AL2230_RF;
	int i, error;

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++) {
		error = zyd_write16(sc, phyini[i].reg, phyini[i].val);
		if (error != 0)
			return error;
	}
	if (sc->rf_rev == ZYD_RF_AL2230S) {
		for (i = 0; i < nitems(phy2230s); i++) {
			error = zyd_write16(sc, phy2230s[i].reg,
			    phy2230s[i].val);
			if (error != 0)
				return error;
		}
	}
	/* init AL2230 radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return error;
	}
	return 0;
}

int
zyd_al2230_init_b(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_AL2230_PHY_B;
	static const uint32_t rfini[] = ZYD_AL2230_RF_B;
	int i, error;

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++) {
		error = zyd_write16(sc, phyini[i].reg, phyini[i].val);
		if (error != 0)
			return error;
	}

	/* init AL2230 radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return error;
	}
	return 0;
}

int
zyd_al2230_switch_radio(struct zyd_rf *rf, int on)
{
	struct zyd_softc *sc = rf->rf_sc;
	int on251 = (sc->mac_rev == ZYD_ZD1211) ? 0x3f : 0x7f;

	(void)zyd_write16(sc, ZYD_CR11,  on ? 0x00 : 0x04);
	(void)zyd_write16(sc, ZYD_CR251, on ? on251 : 0x2f);

	return 0;
}

int
zyd_al2230_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct {
		uint32_t	r1, r2, r3;
	} rfprog[] = ZYD_AL2230_CHANTABLE;

	(void)zyd_rfwrite(sc, rfprog[chan - 1].r1);
	(void)zyd_rfwrite(sc, rfprog[chan - 1].r2);
	(void)zyd_rfwrite(sc, rfprog[chan - 1].r3);

	(void)zyd_write16(sc, ZYD_CR138, 0x28);
	(void)zyd_write16(sc, ZYD_CR203, 0x06);

	return 0;
}

/*
 * AL7230B RF methods.
 */
int
zyd_al7230B_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini_1[] = ZYD_AL7230B_PHY_1;
	static const struct zyd_phy_pair phyini_2[] = ZYD_AL7230B_PHY_2;
	static const struct zyd_phy_pair phyini_3[] = ZYD_AL7230B_PHY_3;
	static const uint32_t rfini_1[] = ZYD_AL7230B_RF_1;
	static const uint32_t rfini_2[] = ZYD_AL7230B_RF_2;
	int i, error;

	/* for AL7230B, PHY and RF need to be initialized in "phases" */

	/* init RF-dependent PHY registers, part one */
	for (i = 0; i < nitems(phyini_1); i++) {
		error = zyd_write16(sc, phyini_1[i].reg, phyini_1[i].val);
		if (error != 0)
			return error;
	}
	/* init AL7230B radio, part one */
	for (i = 0; i < nitems(rfini_1); i++) {
		if ((error = zyd_rfwrite(sc, rfini_1[i])) != 0)
			return error;
	}
	/* init RF-dependent PHY registers, part two */
	for (i = 0; i < nitems(phyini_2); i++) {
		error = zyd_write16(sc, phyini_2[i].reg, phyini_2[i].val);
		if (error != 0)
			return error;
	}
	/* init AL7230B radio, part two */
	for (i = 0; i < nitems(rfini_2); i++) {
		if ((error = zyd_rfwrite(sc, rfini_2[i])) != 0)
			return error;
	}
	/* init RF-dependent PHY registers, part three */
	for (i = 0; i < nitems(phyini_3); i++) {
		error = zyd_write16(sc, phyini_3[i].reg, phyini_3[i].val);
		if (error != 0)
			return error;
	}

	return 0;
}

int
zyd_al7230B_switch_radio(struct zyd_rf *rf, int on)
{
	struct zyd_softc *sc = rf->rf_sc;

	(void)zyd_write16(sc, ZYD_CR11,  on ? 0x00 : 0x04);
	(void)zyd_write16(sc, ZYD_CR251, on ? 0x3f : 0x2f);

	return 0;
}

int
zyd_al7230B_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct {
		uint32_t	r1, r2;
	} rfprog[] = ZYD_AL7230B_CHANTABLE;
	static const uint32_t rfsc[] = ZYD_AL7230B_RF_SETCHANNEL;
	int i, error;

	(void)zyd_write16(sc, ZYD_CR240, 0x57);
	(void)zyd_write16(sc, ZYD_CR251, 0x2f);

	for (i = 0; i < nitems(rfsc); i++) {
		if ((error = zyd_rfwrite(sc, rfsc[i])) != 0)
			return error;
	}

	(void)zyd_write16(sc, ZYD_CR128, 0x14);
	(void)zyd_write16(sc, ZYD_CR129, 0x12);
	(void)zyd_write16(sc, ZYD_CR130, 0x10);
	(void)zyd_write16(sc, ZYD_CR38,  0x38);
	(void)zyd_write16(sc, ZYD_CR136, 0xdf);

	(void)zyd_rfwrite(sc, rfprog[chan - 1].r1);
	(void)zyd_rfwrite(sc, rfprog[chan - 1].r2);
	(void)zyd_rfwrite(sc, 0x3c9000);

	(void)zyd_write16(sc, ZYD_CR251, 0x3f);
	(void)zyd_write16(sc, ZYD_CR203, 0x06);
	(void)zyd_write16(sc, ZYD_CR240, 0x08);

	return 0;
}

/*
 * AL2210 RF methods.
 */
int
zyd_al2210_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_AL2210_PHY;
	static const uint32_t rfini[] = ZYD_AL2210_RF;
	uint32_t tmp;
	int i, error;

	(void)zyd_write32(sc, ZYD_CR18, 2);

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++) {
		error = zyd_write16(sc, phyini[i].reg, phyini[i].val);
		if (error != 0)
			return error;
	}
	/* init AL2210 radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return error;
	}
	(void)zyd_write16(sc, ZYD_CR47, 0x1e);
	(void)zyd_read32(sc, ZYD_CR_RADIO_PD, &tmp);
	(void)zyd_write32(sc, ZYD_CR_RADIO_PD, tmp & ~1);
	(void)zyd_write32(sc, ZYD_CR_RADIO_PD, tmp | 1);
	(void)zyd_write32(sc, ZYD_CR_RFCFG, 0x05);
	(void)zyd_write32(sc, ZYD_CR_RFCFG, 0x00);
	(void)zyd_write16(sc, ZYD_CR47, 0x1e);
	(void)zyd_write32(sc, ZYD_CR18, 3);

	return 0;
}

int
zyd_al2210_switch_radio(struct zyd_rf *rf, int on)
{
	/* vendor driver does nothing for this RF chip */

	return 0;
}

int
zyd_al2210_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const uint32_t rfprog[] = ZYD_AL2210_CHANTABLE;
	uint32_t tmp;

	(void)zyd_write32(sc, ZYD_CR18, 2);
	(void)zyd_write16(sc, ZYD_CR47, 0x1e);
	(void)zyd_read32(sc, ZYD_CR_RADIO_PD, &tmp);
	(void)zyd_write32(sc, ZYD_CR_RADIO_PD, tmp & ~1);
	(void)zyd_write32(sc, ZYD_CR_RADIO_PD, tmp | 1);
	(void)zyd_write32(sc, ZYD_CR_RFCFG, 0x05);

	(void)zyd_write32(sc, ZYD_CR_RFCFG, 0x00);
	(void)zyd_write16(sc, ZYD_CR47, 0x1e);

	/* actually set the channel */
	(void)zyd_rfwrite(sc, rfprog[chan - 1]);

	(void)zyd_write32(sc, ZYD_CR18, 3);

	return 0;
}

/*
 * GCT RF methods.
 */
int
zyd_gct_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_GCT_PHY;
	static const uint32_t rfini[] = ZYD_GCT_RF;
	int i, error;

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++) {
		error = zyd_write16(sc, phyini[i].reg, phyini[i].val);
		if (error != 0)
			return error;
	}
	/* init cgt radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return error;
	}
	return 0;
}

int
zyd_gct_switch_radio(struct zyd_rf *rf, int on)
{
	/* vendor driver does nothing for this RF chip */

	return 0;
}

int
zyd_gct_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const uint32_t rfprog[] = ZYD_GCT_CHANTABLE;

	(void)zyd_rfwrite(sc, 0x1c0000);
	(void)zyd_rfwrite(sc, rfprog[chan - 1]);
	(void)zyd_rfwrite(sc, 0x1c0008);

	return 0;
}

/*
 * Maxim RF methods.
 */
int
zyd_maxim_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_MAXIM_PHY;
	static const uint32_t rfini[] = ZYD_MAXIM_RF;
	uint16_t tmp;
	int i, error;

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++) {
		error = zyd_write16(sc, phyini[i].reg, phyini[i].val);
		if (error != 0)
			return error;
	}
	(void)zyd_read16(sc, ZYD_CR203, &tmp);
	(void)zyd_write16(sc, ZYD_CR203, tmp & ~(1 << 4));

	/* init maxim radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return error;
	}
	(void)zyd_read16(sc, ZYD_CR203, &tmp);
	(void)zyd_write16(sc, ZYD_CR203, tmp | (1 << 4));

	return 0;
}

int
zyd_maxim_switch_radio(struct zyd_rf *rf, int on)
{
	/* vendor driver does nothing for this RF chip */

	return 0;
}

int
zyd_maxim_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_MAXIM_PHY;
	static const uint32_t rfini[] = ZYD_MAXIM_RF;
	static const struct {
		uint32_t	r1, r2;
	} rfprog[] = ZYD_MAXIM_CHANTABLE;
	uint16_t tmp;
	int i, error;

	/*
	 * Do the same as we do when initializing it, except for the channel
	 * values coming from the two channel tables.
	 */

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++) {
		error = zyd_write16(sc, phyini[i].reg, phyini[i].val);
		if (error != 0)
			return error;
	}
	(void)zyd_read16(sc, ZYD_CR203, &tmp);
	(void)zyd_write16(sc, ZYD_CR203, tmp & ~(1 << 4));

	/* first two values taken from the chantables */
	(void)zyd_rfwrite(sc, rfprog[chan - 1].r1);
	(void)zyd_rfwrite(sc, rfprog[chan - 1].r2);

	/* init maxim radio - skipping the two first values */
	for (i = 2; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return error;
	}
	(void)zyd_read16(sc, ZYD_CR203, &tmp);
	(void)zyd_write16(sc, ZYD_CR203, tmp | (1 << 4));

	return 0;
}

/*
 * Maxim2 RF methods.
 */
int
zyd_maxim2_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_MAXIM2_PHY;
	static const uint32_t rfini[] = ZYD_MAXIM2_RF;
	uint16_t tmp;
	int i, error;

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++) {
		error = zyd_write16(sc, phyini[i].reg, phyini[i].val);
		if (error != 0)
			return error;
	}
	(void)zyd_read16(sc, ZYD_CR203, &tmp);
	(void)zyd_write16(sc, ZYD_CR203, tmp & ~(1 << 4));

	/* init maxim2 radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return error;
	}
	(void)zyd_read16(sc, ZYD_CR203, &tmp);
	(void)zyd_write16(sc, ZYD_CR203, tmp | (1 << 4));

	return 0;
}

int
zyd_maxim2_switch_radio(struct zyd_rf *rf, int on)
{
	/* vendor driver does nothing for this RF chip */

	return 0;
}

int
zyd_maxim2_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_MAXIM2_PHY;
	static const uint32_t rfini[] = ZYD_MAXIM2_RF;
	static const struct {
		uint32_t	r1, r2;
	} rfprog[] = ZYD_MAXIM2_CHANTABLE;
	uint16_t tmp;
	int i, error;

	/*
	 * Do the same as we do when initializing it, except for the channel
	 * values coming from the two channel tables.
	 */

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++) {
		error = zyd_write16(sc, phyini[i].reg, phyini[i].val);
		if (error != 0)
			return error;
	}
	(void)zyd_read16(sc, ZYD_CR203, &tmp);
	(void)zyd_write16(sc, ZYD_CR203, tmp & ~(1 << 4));

	/* first two values taken from the chantables */
	(void)zyd_rfwrite(sc, rfprog[chan - 1].r1);
	(void)zyd_rfwrite(sc, rfprog[chan - 1].r2);

	/* init maxim2 radio - skipping the two first values */
	for (i = 2; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return error;
	}
	(void)zyd_read16(sc, ZYD_CR203, &tmp);
	(void)zyd_write16(sc, ZYD_CR203, tmp | (1 << 4));

	return 0;
}

int
zyd_rf_attach(struct zyd_softc *sc, uint8_t type)
{
	struct zyd_rf *rf = &sc->sc_rf;

	rf->rf_sc = sc;

	switch (type) {
	case ZYD_RF_RFMD:
		rf->init         = zyd_rfmd_init;
		rf->switch_radio = zyd_rfmd_switch_radio;
		rf->set_channel  = zyd_rfmd_set_channel;
		rf->width        = 24;	/* 24-bit RF values */
		break;
	case ZYD_RF_AL2230:
	case ZYD_RF_AL2230S:
		if (sc->mac_rev == ZYD_ZD1211B)
			rf->init = zyd_al2230_init_b;
		else
			rf->init = zyd_al2230_init;
		rf->switch_radio = zyd_al2230_switch_radio;
		rf->set_channel  = zyd_al2230_set_channel;
		rf->width        = 24;	/* 24-bit RF values */
		break;
	case ZYD_RF_AL7230B:
		rf->init         = zyd_al7230B_init;
		rf->switch_radio = zyd_al7230B_switch_radio;
		rf->set_channel  = zyd_al7230B_set_channel;
		rf->width        = 24;	/* 24-bit RF values */
		break;
	case ZYD_RF_AL2210:
		rf->init         = zyd_al2210_init;
		rf->switch_radio = zyd_al2210_switch_radio;
		rf->set_channel  = zyd_al2210_set_channel;
		rf->width        = 24;	/* 24-bit RF values */
		break;
	case ZYD_RF_GCT:
		rf->init         = zyd_gct_init;
		rf->switch_radio = zyd_gct_switch_radio;
		rf->set_channel  = zyd_gct_set_channel;
		rf->width        = 21;	/* 21-bit RF values */
		break;
	case ZYD_RF_MAXIM_NEW:
		rf->init         = zyd_maxim_init;
		rf->switch_radio = zyd_maxim_switch_radio;
		rf->set_channel  = zyd_maxim_set_channel;
		rf->width        = 18;	/* 18-bit RF values */
		break;
	case ZYD_RF_MAXIM_NEW2:
		rf->init         = zyd_maxim2_init;
		rf->switch_radio = zyd_maxim2_switch_radio;
		rf->set_channel  = zyd_maxim2_set_channel;
		rf->width        = 18;	/* 18-bit RF values */
		break;
	default:
		printf("%s: sorry, radio \"%s\" is not supported yet\n",
		    sc->sc_dev.dv_xname, zyd_rf_name(type));
		return EINVAL;
	}
	return 0;
}

const char *
zyd_rf_name(uint8_t type)
{
	static const char * const zyd_rfs[] = {
		"unknown", "unknown", "UW2451",   "UCHIP",     "AL2230",
		"AL7230B", "THETA",   "AL2210",   "MAXIM_NEW", "GCT",
		"AL2230S", "RALINK",  "INTERSIL", "RFMD",      "MAXIM_NEW2",
		"PHILIPS"
	};
	return zyd_rfs[(type > 15) ? 0 : type];
}

int
zyd_hw_init(struct zyd_softc *sc)
{
	struct zyd_rf *rf = &sc->sc_rf;
	const struct zyd_phy_pair *phyp;
	uint32_t tmp;
	int error;

	/* specify that the plug and play is finished */
	(void)zyd_write32(sc, ZYD_MAC_AFTER_PNP, 1);

	(void)zyd_read16(sc, ZYD_FIRMWARE_BASE_ADDR, &sc->fwbase);
	DPRINTF(("firmware base address=0x%04x\n", sc->fwbase));

	/* retrieve firmware revision number */
	(void)zyd_read16(sc, sc->fwbase + ZYD_FW_FIRMWARE_REV, &sc->fw_rev);

	(void)zyd_write32(sc, ZYD_CR_GPI_EN, 0);
	(void)zyd_write32(sc, ZYD_MAC_CONT_WIN_LIMIT, 0x7f043f);

	/* disable interrupts */
	(void)zyd_write32(sc, ZYD_CR_INTERRUPT, 0);

	/* PHY init */
	zyd_lock_phy(sc);
	phyp = (sc->mac_rev == ZYD_ZD1211B) ? zyd_def_phyB : zyd_def_phy;
	for (; phyp->reg != 0; phyp++) {
		if ((error = zyd_write16(sc, phyp->reg, phyp->val)) != 0)
			goto fail;
	}
	if (sc->fix_cr157) {
		if (zyd_read32(sc, ZYD_EEPROM_PHY_REG, &tmp) == 0)
			(void)zyd_write32(sc, ZYD_CR157, tmp >> 8);
	}
	zyd_unlock_phy(sc);

	/* HMAC init */
	zyd_write32(sc, ZYD_MAC_ACK_EXT, 0x00000020);
	zyd_write32(sc, ZYD_CR_ADDA_MBIAS_WT, 0x30000808);

	if (sc->mac_rev == ZYD_ZD1211) {
		zyd_write32(sc, ZYD_MAC_RETRY, 0x00000002);
	} else {
		zyd_write32(sc, ZYD_MACB_MAX_RETRY, 0x02020202);
		zyd_write32(sc, ZYD_MACB_TXPWR_CTL4, 0x007f003f);
		zyd_write32(sc, ZYD_MACB_TXPWR_CTL3, 0x007f003f);
		zyd_write32(sc, ZYD_MACB_TXPWR_CTL2, 0x003f001f);
		zyd_write32(sc, ZYD_MACB_TXPWR_CTL1, 0x001f000f);
		zyd_write32(sc, ZYD_MACB_AIFS_CTL1, 0x00280028);
		zyd_write32(sc, ZYD_MACB_AIFS_CTL2, 0x008C003c);
		zyd_write32(sc, ZYD_MACB_TXOP, 0x01800824);
	}

	zyd_write32(sc, ZYD_MAC_SNIFFER, 0x00000000);
	zyd_write32(sc, ZYD_MAC_RXFILTER, 0x00000000);
	zyd_write32(sc, ZYD_MAC_GHTBL, 0x00000000);
	zyd_write32(sc, ZYD_MAC_GHTBH, 0x80000000);
	zyd_write32(sc, ZYD_MAC_MISC, 0x000000a4);
	zyd_write32(sc, ZYD_CR_ADDA_PWR_DWN, 0x0000007f);
	zyd_write32(sc, ZYD_MAC_BCNCFG, 0x00f00401);
	zyd_write32(sc, ZYD_MAC_PHY_DELAY2, 0x00000000);
	zyd_write32(sc, ZYD_MAC_ACK_EXT, 0x00000080);
	zyd_write32(sc, ZYD_CR_ADDA_PWR_DWN, 0x00000000);
	zyd_write32(sc, ZYD_MAC_SIFS_ACK_TIME, 0x00000100);
	zyd_write32(sc, ZYD_MAC_DIFS_EIFS_SIFS, 0x0547c032);
	zyd_write32(sc, ZYD_CR_RX_PE_DELAY, 0x00000070);
	zyd_write32(sc, ZYD_CR_PS_CTRL, 0x10000000);
	zyd_write32(sc, ZYD_MAC_RTSCTSRATE, 0x02030203);
	zyd_write32(sc, ZYD_MAC_RX_THRESHOLD, 0x000c0640);
	zyd_write32(sc, ZYD_MAC_BACKOFF_PROTECT, 0x00000114);

	/* RF chip init */
	zyd_lock_phy(sc);
	error = (*rf->init)(rf);
	zyd_unlock_phy(sc);
	if (error != 0) {
		printf("%s: radio initialization failed\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/* init beacon interval to 100ms */
	if ((error = zyd_set_beacon_interval(sc, 100)) != 0)
		goto fail;

fail:	return error;
}

int
zyd_read_eeprom(struct zyd_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	uint16_t val;
	int i;

	/* read MAC address */
	(void)zyd_read32(sc, ZYD_EEPROM_MAC_ADDR_P1, &tmp);
	ic->ic_myaddr[0] = tmp & 0xff;
	ic->ic_myaddr[1] = tmp >>  8;
	ic->ic_myaddr[2] = tmp >> 16;
	ic->ic_myaddr[3] = tmp >> 24;
	(void)zyd_read32(sc, ZYD_EEPROM_MAC_ADDR_P2, &tmp);
	ic->ic_myaddr[4] = tmp & 0xff;
	ic->ic_myaddr[5] = tmp >>  8;

	(void)zyd_read32(sc, ZYD_EEPROM_POD, &tmp);
	sc->rf_rev    = tmp & 0x0f;
	sc->fix_cr47  = (tmp >> 8 ) & 0x01;
	sc->fix_cr157 = (tmp >> 13) & 0x01;
	sc->pa_rev    = (tmp >> 16) & 0x0f;

	/* read regulatory domain (currently unused) */
	(void)zyd_read32(sc, ZYD_EEPROM_SUBID, &tmp);
	sc->regdomain = tmp >> 16;
	DPRINTF(("regulatory domain %x\n", sc->regdomain));

	/* read Tx power calibration tables */
	for (i = 0; i < 7; i++) {
		(void)zyd_read16(sc, ZYD_EEPROM_PWR_CAL + i, &val);
		sc->pwr_cal[i * 2] = val >> 8;
		sc->pwr_cal[i * 2 + 1] = val & 0xff;

		(void)zyd_read16(sc, ZYD_EEPROM_PWR_INT + i, &val);
		sc->pwr_int[i * 2] = val >> 8;
		sc->pwr_int[i * 2 + 1] = val & 0xff;

		(void)zyd_read16(sc, ZYD_EEPROM_36M_CAL + i, &val);
		sc->ofdm36_cal[i * 2] = val >> 8;
		sc->ofdm36_cal[i * 2 + 1] = val & 0xff;

		(void)zyd_read16(sc, ZYD_EEPROM_48M_CAL + i, &val);
		sc->ofdm48_cal[i * 2] = val >> 8;
		sc->ofdm48_cal[i * 2 + 1] = val & 0xff;

		(void)zyd_read16(sc, ZYD_EEPROM_54M_CAL + i, &val);
		sc->ofdm54_cal[i * 2] = val >> 8;
		sc->ofdm54_cal[i * 2 + 1] = val & 0xff;
	}
	return 0;
}

void
zyd_set_multi(struct zyd_softc *sc)
{
	struct arpcom *ac = &sc->sc_ic.ic_ac;
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t lo, hi;
	uint8_t bit;

	if (ac->ac_multirangecnt > 0)
		ifp->if_flags |= IFF_ALLMULTI;

	if ((ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC)) != 0) {
		lo = hi = 0xffffffff;
		goto done;
	}
	lo = hi = 0;
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		bit = enm->enm_addrlo[5] >> 2;
		if (bit < 32)
			lo |= 1 << bit;
		else
			hi |= 1 << (bit - 32);
		ETHER_NEXT_MULTI(step, enm);
	}

done:
	hi |= 1U << 31;	/* make sure the broadcast bit is set */
	zyd_write32(sc, ZYD_MAC_GHTBL, lo);
	zyd_write32(sc, ZYD_MAC_GHTBH, hi);
}

void
zyd_set_macaddr(struct zyd_softc *sc, const uint8_t *addr)
{
	uint32_t tmp;

	tmp = addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0];
	(void)zyd_write32(sc, ZYD_MAC_MACADRL, tmp);

	tmp = addr[5] << 8 | addr[4];
	(void)zyd_write32(sc, ZYD_MAC_MACADRH, tmp);
}

void
zyd_set_bssid(struct zyd_softc *sc, const uint8_t *addr)
{
	uint32_t tmp;

	tmp = addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0];
	(void)zyd_write32(sc, ZYD_MAC_BSSADRL, tmp);

	tmp = addr[5] << 8 | addr[4];
	(void)zyd_write32(sc, ZYD_MAC_BSSADRH, tmp);
}

int
zyd_switch_radio(struct zyd_softc *sc, int on)
{
	struct zyd_rf *rf = &sc->sc_rf;
	int error;

	zyd_lock_phy(sc);
	error = (*rf->switch_radio)(rf, on);
	zyd_unlock_phy(sc);

	return error;
}

void
zyd_set_led(struct zyd_softc *sc, int which, int on)
{
	uint32_t tmp;

	(void)zyd_read32(sc, ZYD_MAC_TX_PE_CONTROL, &tmp);
	tmp &= ~which;
	if (on)
		tmp |= which;
	(void)zyd_write32(sc, ZYD_MAC_TX_PE_CONTROL, tmp);
}

int
zyd_set_rxfilter(struct zyd_softc *sc)
{
	uint32_t rxfilter;

	switch (sc->sc_ic.ic_opmode) {
	case IEEE80211_M_STA:
		rxfilter = ZYD_FILTER_BSS;
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
	case IEEE80211_M_HOSTAP:
		rxfilter = ZYD_FILTER_HOSTAP;
		break;
#endif
	case IEEE80211_M_MONITOR:
		rxfilter = ZYD_FILTER_MONITOR;
		break;
	default:
		/* should not get there */
		return EINVAL;
	}
	return zyd_write32(sc, ZYD_MAC_RXFILTER, rxfilter);
}

void
zyd_set_chan(struct zyd_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct zyd_rf *rf = &sc->sc_rf;
	uint32_t tmp;
	u_int chan;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return;

	zyd_lock_phy(sc);

	(*rf->set_channel)(rf, chan);

	/* update Tx power */
	(void)zyd_write16(sc, ZYD_CR31, sc->pwr_int[chan - 1]);

	if (sc->mac_rev == ZYD_ZD1211B) {
		(void)zyd_write16(sc, ZYD_CR67, sc->ofdm36_cal[chan - 1]);
		(void)zyd_write16(sc, ZYD_CR66, sc->ofdm48_cal[chan - 1]);
		(void)zyd_write16(sc, ZYD_CR65, sc->ofdm54_cal[chan - 1]);

		(void)zyd_write16(sc, ZYD_CR68, sc->pwr_cal[chan - 1]);

		(void)zyd_write16(sc, ZYD_CR69, 0x28);
		(void)zyd_write16(sc, ZYD_CR69, 0x2a);
	}

	if (sc->fix_cr47) {
		/* set CCK baseband gain from EEPROM */
		if (zyd_read32(sc, ZYD_EEPROM_PHY_REG, &tmp) == 0)
			(void)zyd_write16(sc, ZYD_CR47, tmp & 0xff);
	}

	(void)zyd_write32(sc, ZYD_CR_CONFIG_PHILIPS, 0);

	zyd_unlock_phy(sc);
}

int
zyd_set_beacon_interval(struct zyd_softc *sc, int bintval)
{
	/* XXX this is probably broken.. */
	(void)zyd_write32(sc, ZYD_CR_ATIM_WND_PERIOD, bintval - 2);
	(void)zyd_write32(sc, ZYD_CR_PRE_TBTT,        bintval - 1);
	(void)zyd_write32(sc, ZYD_CR_BCN_INTERVAL,    bintval);

	return 0;
}

uint8_t
zyd_plcp_signal(int rate)
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
zyd_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct zyd_softc *sc = (struct zyd_softc *)priv;
	const struct zyd_cmd *cmd;
	uint32_t len;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		if (status == USBD_STALLED) {
			usbd_clear_endpoint_stall_async(
			    sc->zyd_ep[ZYD_ENDPT_IIN]);
		}
		return;
	}

	cmd = (const struct zyd_cmd *)sc->ibuf;

	if (letoh16(cmd->code) == ZYD_NOTIF_RETRYSTATUS) {
		struct zyd_notif_retry *retry =
		    (struct zyd_notif_retry *)cmd->data;
		struct ieee80211com *ic = &sc->sc_ic;
		struct ifnet *ifp = &ic->ic_if;
		struct ieee80211_node *ni;

		DPRINTF(("retry intr: rate=0x%x addr=%s count=%d (0x%x)\n",
		    letoh16(retry->rate), ether_sprintf(retry->macaddr),
		    letoh16(retry->count) & 0xff, letoh16(retry->count)));

		/*
		 * Find the node to which the packet was sent and update its
		 * retry statistics.  In BSS mode, this node is the AP we're
		 * associated to so no lookup is actually needed.
		 */
		if (ic->ic_opmode != IEEE80211_M_STA) {
			ni = ieee80211_find_node(ic, retry->macaddr);
			if (ni == NULL)
				return;	/* just ignore */
		} else
			ni = ic->ic_bss;

		((struct zyd_node *)ni)->amn.amn_retrycnt++;

		if (letoh16(retry->count) & 0x100)
			ifp->if_oerrors++;	/* too many retries */

	} else if (letoh16(cmd->code) == ZYD_NOTIF_IORD) {
		if (letoh16(*(uint16_t *)cmd->data) == ZYD_CR_INTERRUPT)
			return;	/* HMAC interrupt */

		if (!sc->odone) {
			/* copy answer into sc->odata buffer */
			usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);
			bcopy(cmd->data, sc->odata, sc->olen);
			sc->odone = 1;
			wakeup(sc); /* wakeup zyd_cmd_read() */
		}

	} else {
		printf("%s: unknown notification %x\n", sc->sc_dev.dv_xname,
		    letoh16(cmd->code));
	}
}

void
zyd_rx_data(struct zyd_softc *sc, const uint8_t *buf, uint16_t len,
    struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_node *ni;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	const struct zyd_plcphdr *plcp;
	const struct zyd_rx_stat *stat;
	struct mbuf *m;
	int s;

	if (len < ZYD_MIN_FRAGSZ) {
		DPRINTFN(2, ("frame too short (length=%d)\n", len));
		ifp->if_ierrors++;
		return;
	}

	plcp = (const struct zyd_plcphdr *)buf;
	stat = (const struct zyd_rx_stat *)(buf + len - sizeof (*stat));

	if (stat->flags & ZYD_RX_ERROR) {
		DPRINTF(("%s: RX status indicated error (%x)\n",
		    sc->sc_dev.dv_xname, stat->flags));
		ifp->if_ierrors++;
		return;
	}

	/* compute actual frame length */
	len -= (sizeof (*plcp) + sizeof (*stat) + IEEE80211_CRC_LEN);

	if (len > MCLBYTES) {
		DPRINTFN(2, ("frame too large (length=%d)\n", len));
		ifp->if_ierrors++;
		return;
	}

	/* allocate a mbuf to store the frame */
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL) {
		ifp->if_ierrors++;
		return;
	}
	if (len > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if (!(m->m_flags & M_EXT)) {
			ifp->if_ierrors++;
			m_freem(m);
			return;
		}
	}
	bcopy(plcp + 1, mtod(m, caddr_t), len);
	m->m_pkthdr.len = m->m_len = len;

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct zyd_rx_radiotap_header *tap = &sc->sc_rxtap;
		static const uint8_t rates[] = {
			/* reverse function of zyd_plcp_signal() */
			2, 4, 11, 22, 0, 0, 0, 0,
			96, 48, 24, 12, 108, 72, 36, 18
		};

		tap->wr_flags = 0;
		tap->wr_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		tap->wr_rssi = stat->rssi;
		tap->wr_rate = rates[plcp->signal & 0xf];

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
	wh = mtod(m, struct ieee80211_frame *);
	ni = ieee80211_find_rxnode(ic, wh);
	memset(&rxi, 0, sizeof(rxi));
	rxi.rxi_rssi = stat->rssi;
	ieee80211_inputm(ifp, m, ni, &rxi, ml);

	/* node is no longer needed */
	ieee80211_release_node(ic, ni);

	splx(s);
}

void
zyd_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct zyd_rx_data *data = priv;
	struct zyd_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	const struct zyd_rx_desc *desc;
	int len;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall(sc->zyd_ep[ZYD_ENDPT_BIN]);

		goto skip;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	if (len < ZYD_MIN_RXBUFSZ) {
		DPRINTFN(2, ("xfer too short (length=%d)\n", len));
		ifp->if_ierrors++;
		goto skip;
	}

	desc = (const struct zyd_rx_desc *)
	    (data->buf + len - sizeof (struct zyd_rx_desc));

	if (UGETW(desc->tag) == ZYD_TAG_MULTIFRAME) {
		const uint8_t *p = data->buf, *end = p + len;
		int i;

		DPRINTFN(3, ("received multi-frame transfer\n"));

		for (i = 0; i < ZYD_MAX_RXFRAMECNT; i++) {
			const uint16_t len = UGETW(desc->len[i]);

			if (len == 0 || p + len >= end)
				break;

			zyd_rx_data(sc, p, len, &ml);
			/* next frame is aligned on a 32-bit boundary */
			p += (len + 3) & ~3;
		}
	} else {
		DPRINTFN(3, ("received single-frame transfer\n"));

		zyd_rx_data(sc, data->buf, len, &ml);
	}
	if_input(ifp, &ml);

skip:	/* setup a new transfer */
	usbd_setup_xfer(xfer, sc->zyd_ep[ZYD_ENDPT_BIN], data, NULL,
	    ZYX_MAX_RXBUFSZ, USBD_NO_COPY | USBD_SHORT_XFER_OK,
	    USBD_NO_TIMEOUT, zyd_rxeof);
	(void)usbd_transfer(xfer);
}

void
zyd_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct zyd_tx_data *data = priv;
	struct zyd_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		printf("%s: could not transmit buffer: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(status));

		if (status == USBD_STALLED) {
			usbd_clear_endpoint_stall_async(
			    sc->zyd_ep[ZYD_ENDPT_BOUT]);
		}
		ifp->if_oerrors++;
		return;
	}

	s = splnet();

	/* update rate control statistics */
	((struct zyd_node *)data->ni)->amn.amn_txcnt++;

	ieee80211_release_node(ic, data->ni);
	data->ni = NULL;

	sc->tx_queued--;

	sc->tx_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);
	zyd_start(ifp);

	splx(s);
}

int
zyd_tx(struct zyd_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct zyd_tx_desc *desc;
	struct zyd_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	int xferlen, totlen, rate;
	uint16_t pktlen;
	usbd_status error;

	wh = mtod(m, struct ieee80211_frame *);

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_get_txkey(ic, wh, ni);
		if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
			return ENOBUFS;
		wh = mtod(m, struct ieee80211_frame *);
	}

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
	rate &= IEEE80211_RATE_VAL;
	if (rate == 0)	/* XXX should not happen */
		rate = 2;

	data = &sc->tx_data[0];
	desc = (struct zyd_tx_desc *)data->buf;

	data->ni = ni;

	xferlen = sizeof (struct zyd_tx_desc) + m->m_pkthdr.len;
	totlen = m->m_pkthdr.len + IEEE80211_CRC_LEN;

	/* fill Tx descriptor */
	desc->len = htole16(totlen);

	desc->flags = ZYD_TX_FLAG_BACKOFF;
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/* multicast frames are not sent at OFDM rates in 802.11b/g */
		if (totlen > ic->ic_rtsthreshold) {
			desc->flags |= ZYD_TX_FLAG_RTS;
		} else if (ZYD_RATE_IS_OFDM(rate) &&
		    (ic->ic_flags & IEEE80211_F_USEPROT)) {
			if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
				desc->flags |= ZYD_TX_FLAG_CTS_TO_SELF;
			else if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
				desc->flags |= ZYD_TX_FLAG_RTS;
		}
	} else
		desc->flags |= ZYD_TX_FLAG_MULTICAST;

	if ((wh->i_fc[0] &
	    (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
	    (IEEE80211_FC0_TYPE_CTL | IEEE80211_FC0_SUBTYPE_PS_POLL))
		desc->flags |= ZYD_TX_FLAG_TYPE(ZYD_TX_TYPE_PS_POLL);

	desc->phy = zyd_plcp_signal(rate);
	if (ZYD_RATE_IS_OFDM(rate)) {
		desc->phy |= ZYD_TX_PHY_OFDM;
		if (ic->ic_curmode == IEEE80211_MODE_11A)
			desc->phy |= ZYD_TX_PHY_5GHZ;
	} else if (rate != 2 && (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
		desc->phy |= ZYD_TX_PHY_SHPREAMBLE;

	/* actual transmit length (XXX why +10?) */
	pktlen = sizeof (struct zyd_tx_desc) + 10;
	if (sc->mac_rev == ZYD_ZD1211)
		pktlen += totlen;
	desc->pktlen = htole16(pktlen);

	desc->plcp_length = htole16((16 * totlen + rate - 1) / rate);
	desc->plcp_service = 0;
	if (rate == 22) {
		const int remainder = (16 * totlen) % 22;
		if (remainder != 0 && remainder < 7)
			desc->plcp_service |= ZYD_PLCP_LENGEXT;
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct zyd_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_txtap_len;
		mb.m_next = m;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_OUT);
	}
#endif

	m_copydata(m, 0, m->m_pkthdr.len,
	    data->buf + sizeof (struct zyd_tx_desc));

	DPRINTFN(10, ("%s: sending data frame len=%u rate=%u xferlen=%u\n",
	    sc->sc_dev.dv_xname, m->m_pkthdr.len, rate, xferlen));

	m_freem(m);	/* mbuf no longer needed */

	usbd_setup_xfer(data->xfer, sc->zyd_ep[ZYD_ENDPT_BOUT], data,
	    data->buf, xferlen, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    ZYD_TX_TIMEOUT, zyd_txeof);
	error = usbd_transfer(data->xfer);
	if (error != USBD_IN_PROGRESS && error != 0) {
		data->ni = NULL;
		ifp->if_oerrors++;
		return EIO;
	}
	sc->tx_queued++;

	return 0;
}

void
zyd_start(struct ifnet *ifp)
{
	struct zyd_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->tx_queued >= ZYD_TX_LIST_CNT) {
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
		if (zyd_tx(sc, m, ni) != 0) {
			ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			continue;
		}

		sc->tx_timer = 5;
		ifp->if_timer = 1;
	}
}

void
zyd_watchdog(struct ifnet *ifp)
{
	struct zyd_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->tx_timer > 0) {
		if (--sc->tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			/* zyd_init(ifp); XXX needs a process context ? */
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
zyd_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct zyd_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifreq *ifr;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			/*
			 * If only the PROMISC or ALLMULTI flag changes, then
			 * don't do a full re-init of the chip, just update
			 * the Rx filter.
			 */
			if ((ifp->if_flags & IFF_RUNNING) &&
			    ((ifp->if_flags ^ sc->sc_if_flags) &
			     (IFF_ALLMULTI | IFF_PROMISC)) != 0) {
				zyd_set_multi(sc);
			} else {
				if (!(ifp->if_flags & IFF_RUNNING))
					zyd_init(ifp);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				zyd_stop(ifp, 1);
		}
		sc->sc_if_flags = ifp->if_flags;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		ifr = (struct ifreq *)data;
		error = (cmd == SIOCADDMULTI) ?
		    ether_addmulti(ifr, &ic->ic_ac) :
		    ether_delmulti(ifr, &ic->ic_ac);
		if (error == ENETRESET) {
			if (ifp->if_flags & IFF_RUNNING)
				zyd_set_multi(sc);
			error = 0;
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
			zyd_set_chan(sc, ic->ic_ibss_chan);
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_RUNNING | IFF_UP)) ==
		    (IFF_RUNNING | IFF_UP))
			zyd_init(ifp);
		error = 0;
	}

	splx(s);

	return error;
}

int
zyd_init(struct ifnet *ifp)
{
	struct zyd_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int i, error;

	zyd_stop(ifp, 0);

	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	DPRINTF(("setting MAC address to %s\n", ether_sprintf(ic->ic_myaddr)));
	zyd_set_macaddr(sc, ic->ic_myaddr);

	/* we'll do software WEP decryption for now */
	DPRINTF(("setting encryption type\n"));
	error = zyd_write32(sc, ZYD_MAC_ENCRYPTION_TYPE, ZYD_ENC_SNIFFER);
	if (error != 0)
		return error;

	/* promiscuous mode */
	(void)zyd_write32(sc, ZYD_MAC_SNIFFER,
	    (ic->ic_opmode == IEEE80211_M_MONITOR) ? 1 : 0);

	(void)zyd_set_rxfilter(sc);

	/* switch radio transmitter ON */
	(void)zyd_switch_radio(sc, 1);

	/* set basic rates */
	if (ic->ic_curmode == IEEE80211_MODE_11B)
		(void)zyd_write32(sc, ZYD_MAC_BAS_RATE, 0x0003);
	else if (ic->ic_curmode == IEEE80211_MODE_11A)
		(void)zyd_write32(sc, ZYD_MAC_BAS_RATE, 0x1500);
	else	/* assumes 802.11b/g */
		(void)zyd_write32(sc, ZYD_MAC_BAS_RATE, 0x000f);

	/* set mandatory rates */
	if (ic->ic_curmode == IEEE80211_MODE_11B)
		(void)zyd_write32(sc, ZYD_MAC_MAN_RATE, 0x000f);
	else if (ic->ic_curmode == IEEE80211_MODE_11A)
		(void)zyd_write32(sc, ZYD_MAC_MAN_RATE, 0x1500);
	else	/* assumes 802.11b/g */
		(void)zyd_write32(sc, ZYD_MAC_MAN_RATE, 0x150f);

	/* set default BSS channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	zyd_set_chan(sc, ic->ic_bss->ni_chan);

	/* enable interrupts */
	(void)zyd_write32(sc, ZYD_CR_INTERRUPT, ZYD_HWINT_MASK);

	/*
	 * Allocate Tx and Rx xfer queues.
	 */
	if ((error = zyd_alloc_tx_list(sc)) != 0) {
		printf("%s: could not allocate Tx list\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	if ((error = zyd_alloc_rx_list(sc)) != 0) {
		printf("%s: could not allocate Rx list\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/*
	 * Start up the receive pipe.
	 */
	for (i = 0; i < ZYD_RX_LIST_CNT; i++) {
		struct zyd_rx_data *data = &sc->rx_data[i];

		usbd_setup_xfer(data->xfer, sc->zyd_ep[ZYD_ENDPT_BIN], data,
		    NULL, ZYX_MAX_RXBUFSZ, USBD_NO_COPY | USBD_SHORT_XFER_OK,
		    USBD_NO_TIMEOUT, zyd_rxeof);
		error = usbd_transfer(data->xfer);
		if (error != USBD_IN_PROGRESS && error != 0) {
			printf("%s: could not queue Rx transfer\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return 0;

fail:	zyd_stop(ifp, 1);
	return error;
}

void
zyd_stop(struct ifnet *ifp, int disable)
{
	struct zyd_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	sc->tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);	/* free all nodes */

	/* switch radio transmitter OFF */
	(void)zyd_switch_radio(sc, 0);

	/* disable Rx */
	(void)zyd_write32(sc, ZYD_MAC_RXFILTER, 0);

	/* disable interrupts */
	(void)zyd_write32(sc, ZYD_CR_INTERRUPT, 0);

	usbd_abort_pipe(sc->zyd_ep[ZYD_ENDPT_BIN]);
	usbd_abort_pipe(sc->zyd_ep[ZYD_ENDPT_BOUT]);

	zyd_free_rx_list(sc);
	zyd_free_tx_list(sc);
}

int
zyd_loadfirmware(struct zyd_softc *sc, u_char *fw, size_t size)
{
	usb_device_request_t req;
	uint16_t addr;
	uint8_t stat;

	DPRINTF(("firmware size=%zd\n", size));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = ZYD_DOWNLOADREQ;
	USETW(req.wIndex, 0);

	addr = ZYD_FIRMWARE_START_ADDR;
	while (size > 0) {
		const int mlen = min(size, 4096);

		DPRINTF(("loading firmware block: len=%d, addr=0x%x\n", mlen,
		    addr));

		USETW(req.wValue, addr);
		USETW(req.wLength, mlen);
		if (usbd_do_request(sc->sc_udev, &req, fw) != 0)
			return EIO;

		addr += mlen / 2;
		fw   += mlen;
		size -= mlen;
	}

	/* check whether the upload succeeded */
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = ZYD_DOWNLOADSTS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, sizeof stat);
	if (usbd_do_request(sc->sc_udev, &req, &stat) != 0)
		return EIO;

	return (stat & 0x80) ? EIO : 0;
}

void
zyd_iter_func(void *arg, struct ieee80211_node *ni)
{
	struct zyd_softc *sc = arg;
	struct zyd_node *zn = (struct zyd_node *)ni;

	ieee80211_amrr_choose(&sc->amrr, ni, &zn->amn);
}

void
zyd_amrr_timeout(void *arg)
{
	struct zyd_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int s;

	s = splnet();
	if (ic->ic_opmode == IEEE80211_M_STA)
		zyd_iter_func(sc, ic->ic_bss);
	else
		ieee80211_iterate_nodes(ic, zyd_iter_func, sc);
	splx(s);

	timeout_add_sec(&sc->amrr_to, 1);
}

void
zyd_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct zyd_softc *sc = ic->ic_softc;
	int i;

	ieee80211_amrr_node_init(&sc->amrr, &((struct zyd_node *)ni)->amn);

	/* set rate to some reasonable initial value */
	for (i = ni->ni_rates.rs_nrates - 1;
	     i > 0 && (ni->ni_rates.rs_rates[i] & IEEE80211_RATE_VAL) > 72;
	     i--);
	ni->ni_txrate = i;
}

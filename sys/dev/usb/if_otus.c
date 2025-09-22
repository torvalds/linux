/*	$OpenBSD: if_otus.c,v 1.73 2024/05/23 03:21:08 jsg Exp $	*/

/*-
 * Copyright (c) 2009 Damien Bergamini <damien.bergamini@free.fr>
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
 * Driver for Atheros AR9001U chipset.
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
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_otusreg.h>

#ifdef OTUS_DEBUG
#define DPRINTF(x)	do { if (otus_debug) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (otus_debug >= (n)) printf x; } while (0)
int otus_debug = 1;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

static const struct usb_devno otus_devs[] = {
	{ USB_VENDOR_ACCTON,		USB_PRODUCT_ACCTON_WN7512 },
	{ USB_VENDOR_ATHEROS2,		USB_PRODUCT_ATHEROS2_3CRUSBN275 },
	{ USB_VENDOR_ATHEROS2,		USB_PRODUCT_ATHEROS2_TG121N },
	{ USB_VENDOR_ATHEROS2,		USB_PRODUCT_ATHEROS2_AR9170 },
	{ USB_VENDOR_ATHEROS2,		USB_PRODUCT_ATHEROS2_WN612 },
	{ USB_VENDOR_ATHEROS2,		USB_PRODUCT_ATHEROS2_WN821NV2 },
	{ USB_VENDOR_AVM,		USB_PRODUCT_AVM_FRITZWLAN },
	{ USB_VENDOR_CACE,		USB_PRODUCT_CACE_AIRPCAPNX },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWA130D1 },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWA160A1 },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWA160A2 },
	{ USB_VENDOR_IODATA,		USB_PRODUCT_IODATA_WNGDNUS2 },
	{ USB_VENDOR_NEC,		USB_PRODUCT_NEC_WL300NUG },
	{ USB_VENDOR_NETGEAR,		USB_PRODUCT_NETGEAR_WN111V2 },
	{ USB_VENDOR_NETGEAR,		USB_PRODUCT_NETGEAR_WNA1000 },
	{ USB_VENDOR_NETGEAR,		USB_PRODUCT_NETGEAR_WNDA3100 },
	{ USB_VENDOR_PLANEX2,		USB_PRODUCT_PLANEX2_GW_US300 },
	{ USB_VENDOR_WISTRONNEWEB,	USB_PRODUCT_WISTRONNEWEB_O8494 },
	{ USB_VENDOR_WISTRONNEWEB,	USB_PRODUCT_WISTRONNEWEB_WNC0600 },
	{ USB_VENDOR_ZCOM,		USB_PRODUCT_ZCOM_UB81 },
	{ USB_VENDOR_ZCOM,		USB_PRODUCT_ZCOM_UB82 },
	{ USB_VENDOR_ZYDAS,		USB_PRODUCT_ZYDAS_ZD1221 },
	{ USB_VENDOR_ZYXEL,		USB_PRODUCT_ZYXEL_NWD271N }
};

int		otus_match(struct device *, void *, void *);
void		otus_attach(struct device *, struct device *, void *);
int		otus_detach(struct device *, int);
void		otus_attachhook(struct device *);
void		otus_get_chanlist(struct otus_softc *);
int		otus_load_firmware(struct otus_softc *, const char *,
		    uint32_t);
int		otus_open_pipes(struct otus_softc *);
void		otus_close_pipes(struct otus_softc *);
int		otus_alloc_tx_cmd(struct otus_softc *);
void		otus_free_tx_cmd(struct otus_softc *);
int		otus_alloc_tx_data_list(struct otus_softc *);
void		otus_free_tx_data_list(struct otus_softc *);
int		otus_alloc_rx_data_list(struct otus_softc *);
void		otus_free_rx_data_list(struct otus_softc *);
void		otus_next_scan(void *);
void		otus_task(void *);
void		otus_do_async(struct otus_softc *,
		    void (*)(struct otus_softc *, void *), void *, int);
int		otus_newstate(struct ieee80211com *, enum ieee80211_state,
		    int);
void		otus_newstate_cb(struct otus_softc *, void *);
int		otus_cmd(struct otus_softc *, uint8_t, const void *, int,
		    void *);
void		otus_write(struct otus_softc *, uint32_t, uint32_t);
int		otus_write_barrier(struct otus_softc *);
struct		ieee80211_node *otus_node_alloc(struct ieee80211com *);
int		otus_media_change(struct ifnet *);
int		otus_read_eeprom(struct otus_softc *);
void		otus_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);
void		otus_intr(struct usbd_xfer *, void *, usbd_status);
void		otus_cmd_rxeof(struct otus_softc *, uint8_t *, int);
void		otus_sub_rxeof(struct otus_softc *, uint8_t *, int,
		    struct mbuf_list *);
void		otus_rxeof(struct usbd_xfer *, void *, usbd_status);
void		otus_txeof(struct usbd_xfer *, void *, usbd_status);
int		otus_tx(struct otus_softc *, struct mbuf *,
		    struct ieee80211_node *);
void		otus_start(struct ifnet *);
void		otus_watchdog(struct ifnet *);
int		otus_ioctl(struct ifnet *, u_long, caddr_t);
int		otus_set_multi(struct otus_softc *);
void		otus_updateedca(struct ieee80211com *);
void		otus_updateedca_cb(struct otus_softc *, void *);
void		otus_updateslot(struct ieee80211com *);
void		otus_updateslot_cb(struct otus_softc *, void *);
int		otus_init_mac(struct otus_softc *);
uint32_t	otus_phy_get_def(struct otus_softc *, uint32_t);
int		otus_set_board_values(struct otus_softc *,
		    struct ieee80211_channel *);
int		otus_program_phy(struct otus_softc *,
		    struct ieee80211_channel *);
int		otus_set_rf_bank4(struct otus_softc *,
		    struct ieee80211_channel *);
void		otus_get_delta_slope(uint32_t, uint32_t *, uint32_t *);
int		otus_set_chan(struct otus_softc *, struct ieee80211_channel *,
		    int);
int		otus_set_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		otus_set_key_cb(struct otus_softc *, void *);
void		otus_delete_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		otus_delete_key_cb(struct otus_softc *, void *);
void		otus_calibrate_to(void *);
int		otus_set_bssid(struct otus_softc *, const uint8_t *);
int		otus_set_macaddr(struct otus_softc *, const uint8_t *);
void		otus_led_newstate_type1(struct otus_softc *);
void		otus_led_newstate_type2(struct otus_softc *);
void		otus_led_newstate_type3(struct otus_softc *);
int		otus_init(struct ifnet *);
void		otus_stop(struct ifnet *);

struct cfdriver otus_cd = {
	NULL, "otus", DV_IFNET
};

const struct cfattach otus_ca = {
	sizeof (struct otus_softc), otus_match, otus_attach, otus_detach
};

int
otus_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return UMATCH_NONE;

	return (usb_lookup(otus_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
otus_attach(struct device *parent, struct device *self, void *aux)
{
	struct otus_softc *sc = (struct otus_softc *)self;
	struct usb_attach_arg *uaa = aux;
	int error;

	sc->sc_udev = uaa->device;

	usb_init_task(&sc->sc_task, otus_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->scan_to, otus_next_scan, sc);
	timeout_set(&sc->calib_to, otus_calibrate_to, sc);

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 10;

	/* Get the first interface handle. */
	error = usbd_device2interface_handle(sc->sc_udev, 0, &sc->sc_iface);
	if (error != 0) {
		printf("%s: could not get interface handle\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	if ((error = otus_open_pipes(sc)) != 0) {
		printf("%s: could not open pipes\n", sc->sc_dev.dv_xname);
		return;
	}

	config_mountroot(self, otus_attachhook);
}

int
otus_detach(struct device *self, int flags)
{
	struct otus_softc *sc = (struct otus_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s;

	s = splusb();

	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
	if (timeout_initialized(&sc->calib_to))
		timeout_del(&sc->calib_to);

	/* Wait for all queued asynchronous commands to complete. */
	usb_rem_wait_task(sc->sc_udev, &sc->sc_task);

	usbd_ref_wait(sc->sc_udev);

	if (ifp->if_softc != NULL) {
		ifp->if_flags &= ~IFF_RUNNING;
		ifq_clr_oactive(&ifp->if_snd);
		ieee80211_ifdetach(ifp);
		if_detach(ifp);
	}

	otus_close_pipes(sc);

	splx(s);

	return 0;
}

void
otus_attachhook(struct device *self)
{
	struct otus_softc *sc = (struct otus_softc *)self;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	usb_device_request_t req;
	uint32_t in, out;
	int error;

	error = otus_load_firmware(sc, "otus-init", AR_FW_INIT_ADDR);
	if (error != 0) {
		printf("%s: could not load %s firmware\n",
		    sc->sc_dev.dv_xname, "init");
		return;
	}

	usbd_delay_ms(sc->sc_udev, 1000);

	error = otus_load_firmware(sc, "otus-main", AR_FW_MAIN_ADDR);
	if (error != 0) {
		printf("%s: could not load %s firmware\n",
		    sc->sc_dev.dv_xname, "main");
		return;
	}

	/* Tell device that firmware transfer is complete. */
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = AR_FW_DOWNLOAD_COMPLETE;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	if (usbd_do_request(sc->sc_udev, &req, NULL) != 0) {
		printf("%s: firmware initialization failed\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/* Send an ECHO command to check that everything is settled. */
	in = 0xbadc0ffe;
	if (otus_cmd(sc, AR_CMD_ECHO, &in, sizeof in, &out) != 0) {
		printf("%s: echo command failed\n", sc->sc_dev.dv_xname);
		return;
	}
	if (in != out) {
		printf("%s: echo reply mismatch: 0x%08x!=0x%08x\n",
		    sc->sc_dev.dv_xname, in, out);
		return;
	}

	/* Read entire EEPROM. */
	if (otus_read_eeprom(sc) != 0) {
		printf("%s: could not read EEPROM\n", sc->sc_dev.dv_xname);
		return;
	}

	sc->txmask = sc->eeprom.baseEepHeader.txMask;
	sc->rxmask = sc->eeprom.baseEepHeader.rxMask;
	sc->capflags = sc->eeprom.baseEepHeader.opCapFlags;
	IEEE80211_ADDR_COPY(ic->ic_myaddr, sc->eeprom.baseEepHeader.macAddr);
	sc->sc_led_newstate = otus_led_newstate_type3;	/* XXX */

	printf("%s: MAC/BBP AR9170, RF AR%X, MIMO %dT%dR, address %s\n",
	    sc->sc_dev.dv_xname, (sc->capflags & AR5416_OPFLAGS_11A) ?
	        0x9104 : ((sc->txmask == 0x5) ? 0x9102 : 0x9101),
	    (sc->txmask == 0x5) ? 2 : 1, (sc->rxmask == 0x5) ? 2 : 1,
	    ether_sprintf(ic->ic_myaddr));

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* Set device capabilities. */
	ic->ic_caps =
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WEP |		/* WEP */
	    IEEE80211_C_RSN;		/* WPA/RSN */

	if (sc->eeprom.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11G) {
		/* Set supported .11b and .11g rates. */
		ic->ic_sup_rates[IEEE80211_MODE_11B] =
		    ieee80211_std_rateset_11b;
		ic->ic_sup_rates[IEEE80211_MODE_11G] =
		    ieee80211_std_rateset_11g;
	}
	if (sc->eeprom.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11A) {
		/* Set supported .11a rates. */
		ic->ic_sup_rates[IEEE80211_MODE_11A] =
		    ieee80211_std_rateset_11a;
	}

	/* Build the list of supported channels. */
	otus_get_chanlist(sc);

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = otus_ioctl;
	ifp->if_start = otus_start;
	ifp->if_watchdog = otus_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_node_alloc = otus_node_alloc;
	ic->ic_newassoc = otus_newassoc;
	ic->ic_updateslot = otus_updateslot;
	ic->ic_updateedca = otus_updateedca;
#ifdef notyet
	ic->ic_set_key = otus_set_key;
	ic->ic_delete_key = otus_delete_key;
#endif
	/* Override state transition machine. */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = otus_newstate;
	ieee80211_media_init(ifp, otus_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(OTUS_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(OTUS_TX_RADIOTAP_PRESENT);
#endif
}

void
otus_get_chanlist(struct otus_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t domain;
	uint8_t chan;
	int i;

	/* XXX regulatory domain. */
	domain = letoh16(sc->eeprom.baseEepHeader.regDmn[0]);
	DPRINTF(("regdomain=0x%04x\n", domain));

	if (sc->eeprom.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11G) {
		for (i = 0; i < 14; i++) {
			chan = ar_chans[i];
			ic->ic_channels[chan].ic_freq =
			    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_2GHZ);
			ic->ic_channels[chan].ic_flags =
			    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
			    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
		}
	}
	if (sc->eeprom.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11A) {
		for (i = 14; i < nitems(ar_chans); i++) {
			chan = ar_chans[i];
			ic->ic_channels[chan].ic_freq =
			    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[chan].ic_flags = IEEE80211_CHAN_A;
		}
	}
}

int
otus_load_firmware(struct otus_softc *sc, const char *name, uint32_t addr)
{
	usb_device_request_t req;
	size_t fwsize, size;
	u_char *fw, *ptr;
	int mlen, error;

	/* Read firmware image from the filesystem. */
	if ((error = loadfirmware(name, &fw, &fwsize)) != 0) {
		printf("%s: failed loadfirmware of file %s (error %d)\n",
		    sc->sc_dev.dv_xname, name, error);
		return error;
	}
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = AR_FW_DOWNLOAD;
	USETW(req.wIndex, 0);

	ptr = fw;
	size = fwsize;
	addr >>= 8;
	while (size > 0) {
		mlen = MIN(size, 4096);

		USETW(req.wValue, addr);
		USETW(req.wLength, mlen);
		if (usbd_do_request(sc->sc_udev, &req, ptr) != 0) {
			error = EIO;
			break;
		}
		addr += mlen >> 8;
		ptr  += mlen;
		size -= mlen;
	}
	free(fw, M_DEVBUF, fwsize);
	return error;
}

int
otus_open_pipes(struct otus_softc *sc)
{
	usb_endpoint_descriptor_t *ed;
	int i, isize, error;

	error = usbd_open_pipe(sc->sc_iface, AR_EPT_BULK_RX_NO, 0,
	    &sc->data_rx_pipe);
	if (error != 0) {
		printf("%s: could not open Rx bulk pipe\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ed = usbd_get_endpoint_descriptor(sc->sc_iface, AR_EPT_INTR_RX_NO);
	if (ed == NULL) {
		printf("%s: could not retrieve Rx intr pipe descriptor\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	isize = UGETW(ed->wMaxPacketSize);
	if (isize == 0) {
		printf("%s: invalid Rx intr pipe descriptor\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	sc->ibuf = malloc(isize, M_USBDEV, M_NOWAIT);
	if (sc->ibuf == NULL) {
		printf("%s: could not allocate Rx intr buffer\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	sc->ibuflen = isize;
	error = usbd_open_pipe_intr(sc->sc_iface, AR_EPT_INTR_RX_NO,
	    USBD_SHORT_XFER_OK, &sc->cmd_rx_pipe, sc, sc->ibuf, isize,
	    otus_intr, USBD_DEFAULT_INTERVAL);
	if (error != 0) {
		printf("%s: could not open Rx intr pipe\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = usbd_open_pipe(sc->sc_iface, AR_EPT_BULK_TX_NO, 0,
	    &sc->data_tx_pipe);
	if (error != 0) {
		printf("%s: could not open Tx bulk pipe\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = usbd_open_pipe(sc->sc_iface, AR_EPT_INTR_TX_NO, 0,
	    &sc->cmd_tx_pipe);
	if (error != 0) {
		printf("%s: could not open Tx intr pipe\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	if (otus_alloc_tx_cmd(sc) != 0) {
		printf("%s: could not allocate command xfer\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	if (otus_alloc_tx_data_list(sc) != 0) {
		printf("%s: could not allocate Tx xfers\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	if (otus_alloc_rx_data_list(sc) != 0) {
		printf("%s: could not allocate Rx xfers\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	for (i = 0; i < OTUS_RX_DATA_LIST_COUNT; i++) {
		struct otus_rx_data *data = &sc->rx_data[i];

		usbd_setup_xfer(data->xfer, sc->data_rx_pipe, data, data->buf,
		    OTUS_RXBUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, otus_rxeof);
		error = usbd_transfer(data->xfer);
		if (error != USBD_IN_PROGRESS && error != 0) {
			printf("%s: could not queue Rx xfer\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}
	return 0;

 fail:	otus_close_pipes(sc);
	return error;
}

void
otus_close_pipes(struct otus_softc *sc)
{
	otus_free_tx_cmd(sc);
	otus_free_tx_data_list(sc);
	otus_free_rx_data_list(sc);

	if (sc->data_rx_pipe != NULL)
		usbd_close_pipe(sc->data_rx_pipe);
	if (sc->cmd_rx_pipe != NULL)
		usbd_close_pipe(sc->cmd_rx_pipe);
	if (sc->ibuf != NULL)
		free(sc->ibuf, M_USBDEV, sc->ibuflen);
	if (sc->data_tx_pipe != NULL)
		usbd_close_pipe(sc->data_tx_pipe);
	if (sc->cmd_tx_pipe != NULL)
		usbd_close_pipe(sc->cmd_tx_pipe);
}

int
otus_alloc_tx_cmd(struct otus_softc *sc)
{
	struct otus_tx_cmd *cmd = &sc->tx_cmd;

	cmd->xfer = usbd_alloc_xfer(sc->sc_udev);
	if (cmd->xfer == NULL) {
		printf("%s: could not allocate xfer\n",
		    sc->sc_dev.dv_xname);
		return ENOMEM;
	}
	cmd->buf = usbd_alloc_buffer(cmd->xfer, OTUS_MAX_TXCMDSZ);
	if (cmd->buf == NULL) {
		printf("%s: could not allocate xfer buffer\n",
		    sc->sc_dev.dv_xname);
		usbd_free_xfer(cmd->xfer);
		cmd->xfer = NULL;
		return ENOMEM;
	}
	return 0;
}

void
otus_free_tx_cmd(struct otus_softc *sc)
{
	/* Make sure no transfers are pending. */
	usbd_abort_pipe(sc->cmd_tx_pipe);

	if (sc->tx_cmd.xfer != NULL)
		usbd_free_xfer(sc->tx_cmd.xfer);
}

int
otus_alloc_tx_data_list(struct otus_softc *sc)
{
	struct otus_tx_data *data;
	int i, error;

	for (i = 0; i < OTUS_TX_DATA_LIST_COUNT; i++) {
		data = &sc->tx_data[i];

		data->sc = sc;  /* Backpointer for callbacks. */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		data->buf = usbd_alloc_buffer(data->xfer, OTUS_TXBUFSZ);
		if (data->buf == NULL) {
			printf("%s: could not allocate xfer buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
	}
	return 0;

fail:	otus_free_tx_data_list(sc);
	return error;
}

void
otus_free_tx_data_list(struct otus_softc *sc)
{
	int i;

	/* Make sure no transfers are pending. */
	usbd_abort_pipe(sc->data_tx_pipe);

	for (i = 0; i < OTUS_TX_DATA_LIST_COUNT; i++)
		if (sc->tx_data[i].xfer != NULL)
			usbd_free_xfer(sc->tx_data[i].xfer);
}

int
otus_alloc_rx_data_list(struct otus_softc *sc)
{
	struct otus_rx_data *data;
	int i, error;

	for (i = 0; i < OTUS_RX_DATA_LIST_COUNT; i++) {
		data = &sc->rx_data[i];

		data->sc = sc;	/* Backpointer for callbacks. */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		data->buf = usbd_alloc_buffer(data->xfer, OTUS_RXBUFSZ);
		if (data->buf == NULL) {
			printf("%s: could not allocate xfer buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
	}
	return 0;

fail:	otus_free_rx_data_list(sc);
	return error;
}

void
otus_free_rx_data_list(struct otus_softc *sc)
{
	int i;

	/* Make sure no transfers are pending. */
	usbd_abort_pipe(sc->data_rx_pipe);

	for (i = 0; i < OTUS_RX_DATA_LIST_COUNT; i++)
		if (sc->rx_data[i].xfer != NULL)
			usbd_free_xfer(sc->rx_data[i].xfer);
}

void
otus_next_scan(void *arg)
{
	struct otus_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);

	if (sc->sc_ic.ic_state == IEEE80211_S_SCAN &&
	    (ifp->if_flags & IFF_RUNNING))
		ieee80211_next_scan(&sc->sc_ic.ic_if);

	usbd_ref_decr(sc->sc_udev);
}

void
otus_task(void *arg)
{
	struct otus_softc *sc = arg;
	struct otus_host_cmd_ring *ring = &sc->cmdq;
	struct otus_host_cmd *cmd;
	int s;

	/* Process host commands. */
	s = splusb();
	while (ring->next != ring->cur) {
		cmd = &ring->cmd[ring->next];
		splx(s);
		/* Callback. */
		cmd->cb(sc, cmd->data);
		s = splusb();
		ring->queued--;
		ring->next = (ring->next + 1) % OTUS_HOST_CMD_RING_COUNT;
	}
	splx(s);
}

void
otus_do_async(struct otus_softc *sc, void (*cb)(struct otus_softc *, void *),
    void *arg, int len)
{
	struct otus_host_cmd_ring *ring = &sc->cmdq;
	struct otus_host_cmd *cmd;
	int s;

	s = splusb();
	cmd = &ring->cmd[ring->cur];
	cmd->cb = cb;
	KASSERT(len <= sizeof (cmd->data));
	memcpy(cmd->data, arg, len);
	ring->cur = (ring->cur + 1) % OTUS_HOST_CMD_RING_COUNT;

	/* If there is no pending command already, schedule a task. */
	if (++ring->queued == 1)
		usb_add_task(sc->sc_udev, &sc->sc_task);
	splx(s);
}

int
otus_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct otus_softc *sc = ic->ic_softc;
	struct otus_cmd_newstate cmd;

	/* Do it in a process context. */
	cmd.state = nstate;
	cmd.arg = arg;
	otus_do_async(sc, otus_newstate_cb, &cmd, sizeof cmd);
	return 0;
}

void
otus_newstate_cb(struct otus_softc *sc, void *arg)
{
	struct otus_cmd_newstate *cmd = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	int s;

	s = splnet();

	switch (cmd->state) {
	case IEEE80211_S_INIT:
		break;

	case IEEE80211_S_SCAN:
		(void)otus_set_chan(sc, ic->ic_bss->ni_chan, 0);
		if (!usbd_is_dying(sc->sc_udev))
			timeout_add_msec(&sc->scan_to, 200);
		break;

	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		(void)otus_set_chan(sc, ic->ic_bss->ni_chan, 0);
		break;

	case IEEE80211_S_RUN:
		(void)otus_set_chan(sc, ic->ic_bss->ni_chan, 1);

		ni = ic->ic_bss;

		if (ic->ic_opmode == IEEE80211_M_STA) {
			otus_updateslot(ic);
			otus_set_bssid(sc, ni->ni_bssid);

			/* Fake a join to init the Tx rate. */
			otus_newassoc(ic, ni, 1);

			/* Start calibration timer. */
			if (!usbd_is_dying(sc->sc_udev))
				timeout_add_sec(&sc->calib_to, 1);
		}
		break;
	}

	sc->sc_led_newstate(sc);
	(void)sc->sc_newstate(ic, cmd->state, cmd->arg);

	splx(s);
}

int
otus_cmd(struct otus_softc *sc, uint8_t code, const void *idata, int ilen,
    void *odata)
{
	struct otus_tx_cmd *cmd = &sc->tx_cmd;
	struct ar_cmd_hdr *hdr;
	int s, xferlen, error;

	/* Always bulk-out a multiple of 4 bytes. */
	xferlen = (sizeof (*hdr) + ilen + 3) & ~3;

	hdr = (struct ar_cmd_hdr *)cmd->buf;
	hdr->code  = code;
	hdr->len   = ilen;
	hdr->token = ++cmd->token;	/* Don't care about endianness. */
	memcpy((uint8_t *)&hdr[1], idata, ilen);

	DPRINTFN(2, ("sending command code=0x%02x len=%d token=%d\n",
	    code, ilen, hdr->token));

	s = splusb();
	cmd->odata = odata;
	cmd->done = 0;

	usbd_setup_xfer(cmd->xfer, sc->cmd_tx_pipe, cmd, cmd->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY | USBD_SYNCHRONOUS,
	    OTUS_CMD_TIMEOUT, NULL);
	error = usbd_transfer(cmd->xfer);
	if (error != 0) {
		splx(s);
		printf("%s: could not send command 0x%x (error=%s)\n",
		    sc->sc_dev.dv_xname, code, usbd_errstr(error));
		return EIO;
	}
	if (!cmd->done)
		error = tsleep_nsec(cmd, PCATCH, "otuscmd", SEC_TO_NSEC(1));
	cmd->odata = NULL;	/* In case answer is received too late. */
	splx(s);
	if (error != 0) {
		printf("%s: timeout waiting for command 0x%02x reply\n",
		    sc->sc_dev.dv_xname, code);
	}
	return error;
}

void
otus_write(struct otus_softc *sc, uint32_t reg, uint32_t val)
{
	sc->write_buf[sc->write_idx].reg = htole32(reg);
	sc->write_buf[sc->write_idx].val = htole32(val);

	if (++sc->write_idx > AR_MAX_WRITE_IDX)
		(void)otus_write_barrier(sc);
}

int
otus_write_barrier(struct otus_softc *sc)
{
	int error;

	if (sc->write_idx == 0)
		return 0;	/* Nothing to flush. */

	error = otus_cmd(sc, AR_CMD_WREG, sc->write_buf,
	    sizeof (sc->write_buf[0]) * sc->write_idx, NULL);
	sc->write_idx = 0;
	return error;
}

struct ieee80211_node *
otus_node_alloc(struct ieee80211com *ic)
{
	return malloc(sizeof (struct otus_node), M_USBDEV, M_NOWAIT | M_ZERO);
}

int
otus_media_change(struct ifnet *ifp)
{
	struct otus_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rate, ridx;
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if (ic->ic_fixed_rate != -1) {
		rate = ic->ic_sup_rates[ic->ic_curmode].
		    rs_rates[ic->ic_fixed_rate] & IEEE80211_RATE_VAL;
		for (ridx = 0; ridx <= OTUS_RIDX_MAX; ridx++)
			if (otus_rates[ridx].rate == rate)
				break;
		sc->fixed_ridx = ridx;
	}

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		error = otus_init(ifp);

	return error;
}

int
otus_read_eeprom(struct otus_softc *sc)
{
	uint32_t regs[8], reg;
	uint8_t *eep;
	int i, j, error;

	/* Read EEPROM by blocks of 32 bytes. */
	eep = (uint8_t *)&sc->eeprom;
	reg = AR_EEPROM_OFFSET;
	for (i = 0; i < sizeof (sc->eeprom) / 32; i++) {
		for (j = 0; j < 8; j++, reg += 4)
			regs[j] = htole32(reg);
		error = otus_cmd(sc, AR_CMD_RREG, regs, sizeof regs, eep);
		if (error != 0)
			break;
		eep += 32;
	}
	return error;
}

void
otus_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct otus_softc *sc = ic->ic_softc;
	struct otus_node *on = (void *)ni;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	uint8_t rate;
	int ridx, i;

	DPRINTF(("new assoc isnew=%d addr=%s\n",
	    isnew, ether_sprintf(ni->ni_macaddr)));

	ieee80211_amrr_node_init(&sc->amrr, &on->amn);
	/* Start at lowest available bit-rate, AMRR will raise. */
	ni->ni_txrate = 0;

	for (i = 0; i < rs->rs_nrates; i++) {
		rate = rs->rs_rates[i] & IEEE80211_RATE_VAL;
		/* Convert 802.11 rate to hardware rate index. */
		for (ridx = 0; ridx <= OTUS_RIDX_MAX; ridx++)
			if (otus_rates[ridx].rate == rate)
				break;
		on->ridx[i] = ridx;
		DPRINTF(("rate=0x%02x ridx=%d\n",
		    rs->rs_rates[i], on->ridx[i]));
	}
}

void
otus_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
#if 0
	struct otus_softc *sc = priv;
	int len;

	/*
	 * The Rx intr pipe is unused with current firmware.  Notifications
	 * and replies to commands are sent through the Rx bulk pipe instead
	 * (with a magic PLCP header.)
	 */
	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("intr status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->cmd_rx_pipe);
		return;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	otus_cmd_rxeof(sc, sc->ibuf, len);
#endif
}

void
otus_cmd_rxeof(struct otus_softc *sc, uint8_t *buf, int len)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct otus_tx_cmd *cmd;
	struct ar_cmd_hdr *hdr;
	int s;

	if (__predict_false(len < sizeof (*hdr))) {
		DPRINTF(("cmd too small %d\n", len));
		return;
	}
	hdr = (struct ar_cmd_hdr *)buf;
	if (__predict_false(sizeof (*hdr) + hdr->len > len ||
	    sizeof (*hdr) + hdr->len > 64)) {
		DPRINTF(("cmd too large %d\n", hdr->len));
		return;
	}

	if ((hdr->code & 0xc0) != 0xc0) {
		DPRINTFN(2, ("received reply code=0x%02x len=%d token=%d\n",
		    hdr->code, hdr->len, hdr->token));
		cmd = &sc->tx_cmd;
		if (__predict_false(hdr->token != cmd->token))
			return;
		/* Copy answer into caller's supplied buffer. */
		if (cmd->odata != NULL)
			memcpy(cmd->odata, &hdr[1], hdr->len);
		cmd->done = 1;
		wakeup(cmd);
		return;
	}

	/* Received unsolicited notification. */
	DPRINTF(("received notification code=0x%02x len=%d\n",
	    hdr->code, hdr->len));
	switch (hdr->code & 0x3f) {
	case AR_EVT_BEACON:
		break;
	case AR_EVT_TX_COMP:
	{
		struct ar_evt_tx_comp *tx = (struct ar_evt_tx_comp *)&hdr[1];
		struct ieee80211_node *ni;
		struct otus_node *on;

		DPRINTF(("tx completed %s status=%d phy=0x%x\n",
		    ether_sprintf(tx->macaddr), letoh16(tx->status),
		    letoh32(tx->phy)));
		s = splnet();
#ifdef notyet
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode != IEEE80211_M_STA) {
			ni = ieee80211_find_node(ic, tx->macaddr);
			if (__predict_false(ni == NULL)) {
				splx(s);
				break;
			}
		} else
#endif
#endif
			ni = ic->ic_bss;
		/* Update rate control statistics. */
		on = (void *)ni;
		/* NB: we do not set the TX_MAC_RATE_PROBING flag. */
		if (__predict_true(tx->status != 0))
			on->amn.amn_retrycnt++;
		splx(s);
		break;
	}
	case AR_EVT_TBTT:
		break;
	}
}

void
otus_sub_rxeof(struct otus_softc *sc, uint8_t *buf, int len,
    struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct ar_rx_tail *tail;
	struct ieee80211_frame *wh;
	struct mbuf *m;
	uint8_t *plcp;
	int s, mlen, align;

	if (__predict_false(len < AR_PLCP_HDR_LEN)) {
		DPRINTF(("sub-xfer too short %d\n", len));
		return;
	}
	plcp = buf;

	/* All bits in the PLCP header are set to 1 for non-MPDU. */
	if (memcmp(plcp, AR_PLCP_HDR_INTR, AR_PLCP_HDR_LEN) == 0) {
		otus_cmd_rxeof(sc, plcp + AR_PLCP_HDR_LEN,
		    len - AR_PLCP_HDR_LEN);
		return;
	}

	/* Received MPDU. */
	if (__predict_false(len < AR_PLCP_HDR_LEN + sizeof (*tail))) {
		DPRINTF(("MPDU too short %d\n", len));
		ifp->if_ierrors++;
		return;
	}
	tail = (struct ar_rx_tail *)(plcp + len - sizeof (*tail));

	/* Discard error frames. */
	if (__predict_false(tail->error != 0)) {
		DPRINTF(("error frame 0x%02x\n", tail->error));
		if (tail->error & AR_RX_ERROR_FCS) {
			DPRINTFN(3, ("bad FCS\n"));
		} else if (tail->error & AR_RX_ERROR_MMIC) {
			/* Report Michael MIC failures to net80211. */
			ic->ic_stats.is_rx_locmicfail++;
			ieee80211_michael_mic_failure(ic, 0);
		}
		ifp->if_ierrors++;
		return;
	}
	/* Compute MPDU's length. */
	mlen = len - AR_PLCP_HDR_LEN - sizeof (*tail);
	/* Make sure there's room for an 802.11 header + FCS. */
	if (__predict_false(mlen < IEEE80211_MIN_LEN)) {
		ifp->if_ierrors++;
		return;
	}
	mlen -= IEEE80211_CRC_LEN;	/* strip 802.11 FCS */
	if (mlen > MCLBYTES) {
		DPRINTF(("frame too large: %d\n", mlen));
		ifp->if_ierrors++;
		return;
	}

	wh = (struct ieee80211_frame *)(plcp + AR_PLCP_HDR_LEN);
	/* Provide a 32-bit aligned protocol header to the stack. */
	align = (ieee80211_has_qos(wh) ^ ieee80211_has_addr4(wh)) ? 2 : 0;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (__predict_false(m == NULL)) {
		ifp->if_ierrors++;
		return;
	}
	if (align + mlen > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if (__predict_false(!(m->m_flags & M_EXT))) {
			ifp->if_ierrors++;
			m_freem(m);
			return;
		}
	}
	/* Finalize mbuf. */
	m->m_data += align;
	memcpy(mtod(m, caddr_t), wh, mlen);
	m->m_pkthdr.len = m->m_len = mlen;

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct otus_rx_radiotap_header *tap = &sc->sc_rxtap;
		struct mbuf mb;

		tap->wr_flags = 0;
		tap->wr_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);
		tap->wr_antsignal = tail->rssi;
		tap->wr_rate = 2;	/* In case it can't be found below. */
		switch (tail->status & AR_RX_STATUS_MT_MASK) {
		case AR_RX_STATUS_MT_CCK:
			switch (plcp[0]) {
			case  10: tap->wr_rate =   2; break;
			case  20: tap->wr_rate =   4; break;
			case  55: tap->wr_rate =  11; break;
			case 110: tap->wr_rate =  22; break;
			}
			if (tail->status & AR_RX_STATUS_SHPREAMBLE)
				tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
			break;
		case AR_RX_STATUS_MT_OFDM:
			switch (plcp[0] & 0xf) {
			case 0xb: tap->wr_rate =  12; break;
			case 0xf: tap->wr_rate =  18; break;
			case 0xa: tap->wr_rate =  24; break;
			case 0xe: tap->wr_rate =  36; break;
			case 0x9: tap->wr_rate =  48; break;
			case 0xd: tap->wr_rate =  72; break;
			case 0x8: tap->wr_rate =  96; break;
			case 0xc: tap->wr_rate = 108; break;
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
	memset(&rxi, 0, sizeof(rxi));
	rxi.rxi_rssi = tail->rssi;
	ieee80211_inputm(ifp, m, ni, &rxi, ml);

	/* Node is no longer needed. */
	ieee80211_release_node(ic, ni);
	splx(s);
}

void
otus_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct otus_rx_data *data = priv;
	struct otus_softc *sc = data->sc;
	caddr_t buf = data->buf;
	struct ar_rx_head *head;
	uint16_t hlen;
	int len;

	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("RX status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->data_rx_pipe);
		if (status != USBD_CANCELLED)
			goto resubmit;
		return;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	while (len >= sizeof (*head)) {
		head = (struct ar_rx_head *)buf;
		if (__predict_false(head->tag != htole16(AR_RX_HEAD_TAG))) {
			DPRINTF(("tag not valid 0x%x\n", letoh16(head->tag)));
			break;
		}
		hlen = letoh16(head->len);
		if (__predict_false(sizeof (*head) + hlen > len)) {
			DPRINTF(("xfer too short %d/%d\n", len, hlen));
			break;
		}
		/* Process sub-xfer. */
		otus_sub_rxeof(sc, (uint8_t *)&head[1], hlen, &ml);

		/* Next sub-xfer is aligned on a 32-bit boundary. */
		hlen = (sizeof (*head) + hlen + 3) & ~3;
		buf += hlen;
		len -= hlen;
	}
	if_input(&sc->sc_ic.ic_if, &ml);

 resubmit:
	usbd_setup_xfer(xfer, sc->data_rx_pipe, data, data->buf, OTUS_RXBUFSZ,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, otus_rxeof);
	(void)usbd_transfer(data->xfer);
}

void
otus_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct otus_tx_data *data = priv;
	struct otus_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	s = splnet();
	sc->tx_queued--;
	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("TX status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->data_tx_pipe);
		ifp->if_oerrors++;
		splx(s);
		return;
	}
	sc->sc_tx_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);
	otus_start(ifp);
	splx(s);
}

int
otus_tx(struct otus_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct otus_node *on = (void *)ni;
	struct otus_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	struct ar_tx_head *head;
	uint32_t phyctl;
	uint16_t macctl, qos;
	uint8_t tid, qid;
	int error, ridx, hasqos, xferlen;

	wh = mtod(m, struct ieee80211_frame *);
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_get_txkey(ic, wh, ni);
		if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
			return ENOBUFS;
		wh = mtod(m, struct ieee80211_frame *);
	}

	if ((hasqos = ieee80211_has_qos(wh))) {
		qos = ieee80211_get_qos(wh);
		tid = qos & IEEE80211_QOS_TID;
		qid = ieee80211_up_to_ac(ic, tid);
	} else {
		qos = 0;
		qid = EDCA_AC_BE;
	}

	/* Pickup a rate index. */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_DATA)
		ridx = (ic->ic_curmode == IEEE80211_MODE_11A) ?
		    OTUS_RIDX_OFDM6 : OTUS_RIDX_CCK1;
	else if (ic->ic_fixed_rate != -1)
		ridx = sc->fixed_ridx;
	else
		ridx = on->ridx[ni->ni_txrate];

	phyctl = 0;
	macctl = AR_TX_MAC_BACKOFF | AR_TX_MAC_HW_DUR | AR_TX_MAC_QID(qid);

	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    (hasqos && ((qos & IEEE80211_QOS_ACK_POLICY_MASK) ==
	     IEEE80211_QOS_ACK_POLICY_NOACK)))
		macctl |= AR_TX_MAC_NOACK;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		if (m->m_pkthdr.len + IEEE80211_CRC_LEN >= ic->ic_rtsthreshold)
			macctl |= AR_TX_MAC_RTS;
		else if ((ic->ic_flags & IEEE80211_F_USEPROT) &&
		    ridx >= OTUS_RIDX_OFDM6) {
			if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
				macctl |= AR_TX_MAC_CTS;
			else if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
				macctl |= AR_TX_MAC_RTS;
		}
	}

	phyctl |= AR_TX_PHY_MCS(otus_rates[ridx].mcs);
	if (ridx >= OTUS_RIDX_OFDM6) {
		phyctl |= AR_TX_PHY_MT_OFDM;
		if (ridx <= OTUS_RIDX_OFDM24)
			phyctl |= AR_TX_PHY_ANTMSK(sc->txmask);
		else
			phyctl |= AR_TX_PHY_ANTMSK(1);
	} else {	/* CCK */
		phyctl |= AR_TX_PHY_MT_CCK;
		phyctl |= AR_TX_PHY_ANTMSK(sc->txmask);
	}

	/* Update rate control stats for frames that are ACK'ed. */
	if (!(macctl & AR_TX_MAC_NOACK))
		((struct otus_node *)ni)->amn.amn_txcnt++;

	data = &sc->tx_data[sc->tx_cur];
	/* Fill Tx descriptor. */
	head = (struct ar_tx_head *)data->buf;
	head->len = htole16(m->m_pkthdr.len + IEEE80211_CRC_LEN);
	head->macctl = htole16(macctl);
	head->phyctl = htole32(phyctl);

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct otus_tx_radiotap_header *tap = &sc->sc_txtap;
		struct mbuf mb;

		tap->wt_flags = 0;
		tap->wt_rate = otus_rates[ridx].rate;
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

	xferlen = sizeof (*head) + m->m_pkthdr.len;
	m_copydata(m, 0, m->m_pkthdr.len, &head[1]);
	m_freem(m);

	DPRINTFN(5, ("tx queued=%d len=%d mac=0x%04x phy=0x%08x rate=%d\n",
	    sc->tx_queued, head->len, head->macctl, head->phyctl,
	    otus_rates[ridx].rate));
	usbd_setup_xfer(data->xfer, sc->data_tx_pipe, data, data->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, OTUS_TX_TIMEOUT, otus_txeof);
	error = usbd_transfer(data->xfer);
	if (__predict_false(error != USBD_IN_PROGRESS && error != 0))
		return error;

	ieee80211_release_node(ic, ni);

	sc->tx_queued++;
	sc->tx_cur = (sc->tx_cur + 1) % OTUS_TX_DATA_LIST_COUNT;

	return 0;
}

void
otus_start(struct ifnet *ifp)
{
	struct otus_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->tx_queued >= OTUS_TX_DATA_LIST_COUNT) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
		/* Send pending management frames first. */
		m = mq_dequeue(&ic->ic_mgtq);
		if (m != NULL) {
			ni = m->m_pkthdr.ph_cookie;
			goto sendit;
		}
		if (ic->ic_state != IEEE80211_S_RUN)
			break;

		/* Encapsulate and send data frames. */
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
		if (otus_tx(sc, m, ni) != 0) {
			ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			continue;
		}

		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

void
otus_watchdog(struct ifnet *ifp)
{
	struct otus_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			/* otus_init(ifp); XXX needs a process context! */
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}
	ieee80211_watchdog(ifp);
}

int
otus_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct otus_softc *sc = ifp->if_softc;
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
			if ((ifp->if_flags & IFF_RUNNING) &&
			    ((ifp->if_flags ^ sc->sc_if_flags) &
			     (IFF_ALLMULTI | IFF_PROMISC)) != 0) {
				otus_set_multi(sc);
			} else if (!(ifp->if_flags & IFF_RUNNING))
				otus_init(ifp);

		} else if (ifp->if_flags & IFF_RUNNING)
			otus_stop(ifp);

		sc->sc_if_flags = ifp->if_flags;
		break;
	case SIOCS80211CHANNEL:
		error = ieee80211_ioctl(ifp, cmd, data);
		if (error == ENETRESET &&
		    ic->ic_opmode == IEEE80211_M_MONITOR) {
			if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
			    (IFF_UP | IFF_RUNNING))
				otus_set_chan(sc, ic->ic_ibss_chan, 0);
			error = 0;
		}
		break;
	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			otus_init(ifp);
		error = 0;
	}

	splx(s);

	usbd_ref_decr(sc->sc_udev);

	return error;
}

int
otus_set_multi(struct otus_softc *sc)
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
	hi |= 1U << 31;	/* Make sure the broadcast bit is set. */
	otus_write(sc, AR_MAC_REG_GROUP_HASH_TBL_L, lo);
	otus_write(sc, AR_MAC_REG_GROUP_HASH_TBL_H, hi);
	return otus_write_barrier(sc);
}

void
otus_updateedca(struct ieee80211com *ic)
{
	/* Do it in a process context. */
	otus_do_async(ic->ic_softc, otus_updateedca_cb, NULL, 0);
}

void
otus_updateedca_cb(struct otus_softc *sc, void *arg)
{
#define EXP2(val)	((1 << (val)) - 1)
#define AIFS(val)	((val) * 9 + 10)
	struct ieee80211com *ic = &sc->sc_ic;
	const struct ieee80211_edca_ac_params *edca;
	int s;

	s = splnet();

	edca = (ic->ic_flags & IEEE80211_F_QOS) ?
	    ic->ic_edca_ac : otus_edca_def;

	/* Set CWmin/CWmax values. */
	otus_write(sc, AR_MAC_REG_AC0_CW,
	    EXP2(edca[EDCA_AC_BE].ac_ecwmax) << 16 |
	    EXP2(edca[EDCA_AC_BE].ac_ecwmin));
	otus_write(sc, AR_MAC_REG_AC1_CW,
	    EXP2(edca[EDCA_AC_BK].ac_ecwmax) << 16 |
	    EXP2(edca[EDCA_AC_BK].ac_ecwmin));
	otus_write(sc, AR_MAC_REG_AC2_CW,
	    EXP2(edca[EDCA_AC_VI].ac_ecwmax) << 16 |
	    EXP2(edca[EDCA_AC_VI].ac_ecwmin));
	otus_write(sc, AR_MAC_REG_AC3_CW,
	    EXP2(edca[EDCA_AC_VO].ac_ecwmax) << 16 |
	    EXP2(edca[EDCA_AC_VO].ac_ecwmin));
	otus_write(sc, AR_MAC_REG_AC4_CW,		/* Special TXQ. */
	    EXP2(edca[EDCA_AC_VO].ac_ecwmax) << 16 |
	    EXP2(edca[EDCA_AC_VO].ac_ecwmin));

	/* Set AIFSN values. */
	otus_write(sc, AR_MAC_REG_AC1_AC0_AIFS,
	    AIFS(edca[EDCA_AC_VI].ac_aifsn) << 24 |
	    AIFS(edca[EDCA_AC_BK].ac_aifsn) << 12 |
	    AIFS(edca[EDCA_AC_BE].ac_aifsn));
	otus_write(sc, AR_MAC_REG_AC3_AC2_AIFS,
	    AIFS(edca[EDCA_AC_VO].ac_aifsn) << 16 |	/* Special TXQ. */
	    AIFS(edca[EDCA_AC_VO].ac_aifsn) <<  4 |
	    AIFS(edca[EDCA_AC_VI].ac_aifsn) >>  8);

	/* Set TXOP limit. */
	otus_write(sc, AR_MAC_REG_AC1_AC0_TXOP,
	    edca[EDCA_AC_BK].ac_txoplimit << 16 |
	    edca[EDCA_AC_BE].ac_txoplimit);
	otus_write(sc, AR_MAC_REG_AC3_AC2_TXOP,
	    edca[EDCA_AC_VO].ac_txoplimit << 16 |
	    edca[EDCA_AC_VI].ac_txoplimit);

	splx(s);

	(void)otus_write_barrier(sc);
#undef AIFS
#undef EXP2
}

void
otus_updateslot(struct ieee80211com *ic)
{
	/* Do it in a process context. */
	otus_do_async(ic->ic_softc, otus_updateslot_cb, NULL, 0);
}

void
otus_updateslot_cb(struct otus_softc *sc, void *arg)
{
	uint32_t slottime;

	slottime = (sc->sc_ic.ic_flags & IEEE80211_F_SHSLOT) ?
	    IEEE80211_DUR_DS_SHSLOT: IEEE80211_DUR_DS_SLOT;
	otus_write(sc, AR_MAC_REG_SLOT_TIME, slottime << 10);
	(void)otus_write_barrier(sc);
}

int
otus_init_mac(struct otus_softc *sc)
{
	int error;

	otus_write(sc, AR_MAC_REG_ACK_EXTENSION, 0x40);
	otus_write(sc, AR_MAC_REG_RETRY_MAX, 0);
	otus_write(sc, AR_MAC_REG_SNIFFER, 0x2000000);
	otus_write(sc, AR_MAC_REG_RX_THRESHOLD, 0xc1f80);
	otus_write(sc, AR_MAC_REG_RX_PE_DELAY, 0x70);
	otus_write(sc, AR_MAC_REG_EIFS_AND_SIFS, 0xa144000);
	otus_write(sc, AR_MAC_REG_SLOT_TIME, 9 << 10);
	otus_write(sc, 0x1c3b2c, 0x19000000);
	/* NAV protects ACK only (in TXOP). */
	otus_write(sc, 0x1c3b38, 0x201);
	/* Set beacon Tx power to 0x7. */
	otus_write(sc, AR_MAC_REG_BCN_HT1, 0x8000170);
	otus_write(sc, AR_MAC_REG_BACKOFF_PROTECT, 0x105);
	otus_write(sc, 0x1c3b9c, 0x10000a);
	/* Filter any control frames, BAR is bit 24. */
	otus_write(sc, 0x1c368c, 0x0500ffff);
	otus_write(sc, 0x1c3c40, 0x1);
	otus_write(sc, AR_MAC_REG_BASIC_RATE, 0x150f);
	otus_write(sc, AR_MAC_REG_MANDATORY_RATE, 0x150f);
	otus_write(sc, AR_MAC_REG_RTS_CTS_RATE, 0x10b01bb);
	otus_write(sc, 0x1c3694, 0x4003c1e);
	/* Enable LED0 and LED1. */
	otus_write(sc, 0x1d0100, 0x3);
	otus_write(sc, 0x1d0104, 0x3);
	/* Switch MAC to OTUS interface. */
	otus_write(sc, 0x1c3600, 0x3);
	otus_write(sc, 0x1c3c50, 0xffff);
	otus_write(sc, 0x1c3680, 0xf00008);
	/* Disable Rx timeout (workaround). */
	otus_write(sc, 0x1c362c, 0);

	/* Set USB Rx stream mode maximum frame number to 2. */
	otus_write(sc, 0x1e1110, 0x4);
	/* Set USB Rx stream mode timeout to 10us. */
	otus_write(sc, 0x1e1114, 0x80);

	/* Set clock frequency to 88/80MHz. */
	otus_write(sc, 0x1d4008, 0x73);
	/* Set WLAN DMA interrupt mode: generate intr per packet. */
	otus_write(sc, 0x1c3d7c, 0x110011);
	otus_write(sc, 0x1c3bb0, 0x4);
	otus_write(sc, AR_MAC_REG_TXOP_NOT_ENOUGH_INDICATION, 0x141e0f48);

	/* Disable HW decryption for now. */
	otus_write(sc, 0x1c3678, 0x78);

	if ((error = otus_write_barrier(sc)) != 0)
		return error;

	/* Set default EDCA parameters. */
	otus_updateedca_cb(sc, NULL);

	return 0;
}

/*
 * Return default value for PHY register based on current operating mode.
 */
uint32_t
otus_phy_get_def(struct otus_softc *sc, uint32_t reg)
{
	int i;

	for (i = 0; i < nitems(ar5416_phy_regs); i++)
		if (AR_PHY(ar5416_phy_regs[i]) == reg)
			return sc->phy_vals[i];
	return 0;	/* Register not found. */
}

/*
 * Update PHY's programming based on vendor-specific data stored in EEPROM.
 * This is for FEM-type devices only.
 */
int
otus_set_board_values(struct otus_softc *sc, struct ieee80211_channel *c)
{
	const struct ModalEepHeader *eep;
	uint32_t tmp, offset;

	if (IEEE80211_IS_CHAN_5GHZ(c))
		eep = &sc->eeprom.modalHeader[0];
	else
		eep = &sc->eeprom.modalHeader[1];

	/* Offset of chain 2. */
	offset = 2 * 0x1000;

	tmp = letoh32(eep->antCtrlCommon);
	otus_write(sc, AR_PHY_SWITCH_COM, tmp);

	tmp = letoh32(eep->antCtrlChain[0]);
	otus_write(sc, AR_PHY_SWITCH_CHAIN_0, tmp);

	tmp = letoh32(eep->antCtrlChain[1]);
	otus_write(sc, AR_PHY_SWITCH_CHAIN_0 + offset, tmp);

	if (1 /* sc->sc_sco == AR_SCO_SCN */) {
		tmp = otus_phy_get_def(sc, AR_PHY_SETTLING);
		tmp &= ~(0x7f << 7);
		tmp |= (eep->switchSettling & 0x7f) << 7;
		otus_write(sc, AR_PHY_SETTLING, tmp);
	}

	tmp = otus_phy_get_def(sc, AR_PHY_DESIRED_SZ);
	tmp &= ~0xffff;
	tmp |= eep->pgaDesiredSize << 8 | eep->adcDesiredSize;
	otus_write(sc, AR_PHY_DESIRED_SZ, tmp);

	tmp = eep->txEndToXpaOff << 24 | eep->txEndToXpaOff << 16 |
	      eep->txFrameToXpaOn << 8 | eep->txFrameToXpaOn;
	otus_write(sc, AR_PHY_RF_CTL4, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_RF_CTL3);
	tmp &= ~(0xff << 16);
	tmp |= eep->txEndToRxOn << 16;
	otus_write(sc, AR_PHY_RF_CTL3, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_CCA);
	tmp &= ~(0x7f << 12);
	tmp |= (eep->thresh62 & 0x7f) << 12;
	otus_write(sc, AR_PHY_CCA, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_RXGAIN);
	tmp &= ~(0x3f << 12);
	tmp |= (eep->txRxAttenCh[0] & 0x3f) << 12;
	otus_write(sc, AR_PHY_RXGAIN, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_RXGAIN + offset);
	tmp &= ~(0x3f << 12);
	tmp |= (eep->txRxAttenCh[1] & 0x3f) << 12;
	otus_write(sc, AR_PHY_RXGAIN + offset, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_GAIN_2GHZ);
	tmp &= ~(0x3f << 18);
	tmp |= (eep->rxTxMarginCh[0] & 0x3f) << 18;
	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		tmp &= ~(0xf << 10);
		tmp |= (eep->bswMargin[0] & 0xf) << 10;
	}
	otus_write(sc, AR_PHY_GAIN_2GHZ, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_GAIN_2GHZ + offset);
	tmp &= ~(0x3f << 18);
	tmp |= (eep->rxTxMarginCh[1] & 0x3f) << 18;
	otus_write(sc, AR_PHY_GAIN_2GHZ + offset, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_TIMING_CTRL4);
	tmp &= ~(0x3f << 5 | 0x1f);
	tmp |= (eep->iqCalICh[0] & 0x3f) << 5 | (eep->iqCalQCh[0] & 0x1f);
	otus_write(sc, AR_PHY_TIMING_CTRL4, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_TIMING_CTRL4 + offset);
	tmp &= ~(0x3f << 5 | 0x1f);
	tmp |= (eep->iqCalICh[1] & 0x3f) << 5 | (eep->iqCalQCh[1] & 0x1f);
	otus_write(sc, AR_PHY_TIMING_CTRL4 + offset, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_TPCRG1);
	tmp &= ~(0xf << 16);
	tmp |= (eep->xpd & 0xf) << 16;
	otus_write(sc, AR_PHY_TPCRG1, tmp);

	return otus_write_barrier(sc);
}

int
otus_program_phy(struct otus_softc *sc, struct ieee80211_channel *c)
{
	const uint32_t *vals;
	int error, i;

	/* Select PHY programming based on band and bandwidth. */
	if (IEEE80211_IS_CHAN_2GHZ(c))
		vals = ar5416_phy_vals_2ghz_20mhz;
	else
		vals = ar5416_phy_vals_5ghz_20mhz;
	for (i = 0; i < nitems(ar5416_phy_regs); i++)
		otus_write(sc, AR_PHY(ar5416_phy_regs[i]), vals[i]);
	sc->phy_vals = vals;

	if (sc->eeprom.baseEepHeader.deviceType == 0x80)	/* FEM */
		if ((error = otus_set_board_values(sc, c)) != 0)
			return error;

	/* Initial Tx power settings. */
	otus_write(sc, AR_PHY_POWER_TX_RATE_MAX, 0x7f);
	otus_write(sc, AR_PHY_POWER_TX_RATE1, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE2, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE3, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE4, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE5, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE6, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE7, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE8, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE9, 0x3f3f3f3f);

	if (IEEE80211_IS_CHAN_2GHZ(c))
		otus_write(sc, 0x1d4014, 0x5163);
	else
		otus_write(sc, 0x1d4014, 0x5143);

	return otus_write_barrier(sc);
}

static __inline uint8_t
otus_reverse_bits(uint8_t v)
{
	v = ((v >> 1) & 0x55) | ((v & 0x55) << 1);
	v = ((v >> 2) & 0x33) | ((v & 0x33) << 2);
	v = ((v >> 4) & 0x0f) | ((v & 0x0f) << 4);
	return v;
}

int
otus_set_rf_bank4(struct otus_softc *sc, struct ieee80211_channel *c)
{
	uint8_t chansel, d0, d1;
	uint16_t data;
	int error;

	d0 = 0;
	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		chansel = (c->ic_freq - 4800) / 5;
		if (chansel & 1)
			d0 |= AR_BANK4_AMODE_REFSEL(2);
		else
			d0 |= AR_BANK4_AMODE_REFSEL(1);
	} else {
		d0 |= AR_BANK4_AMODE_REFSEL(2);
		if (c->ic_freq == 2484) {	/* CH 14 */
			d0 |= AR_BANK4_BMODE_LF_SYNTH_FREQ;
			chansel = 10 + (c->ic_freq - 2274) / 5;
		} else
			chansel = 16 + (c->ic_freq - 2272) / 5;
		chansel <<= 2;
	}
	d0 |= AR_BANK4_ADDR(1) | AR_BANK4_CHUP;
	d1 = otus_reverse_bits(chansel);

	/* Write bits 0-4 of d0 and d1. */
	data = (d1 & 0x1f) << 5 | (d0 & 0x1f);
	otus_write(sc, AR_PHY(44), data);
	/* Write bits 5-7 of d0 and d1. */
	data = (d1 >> 5) << 5 | (d0 >> 5);
	otus_write(sc, AR_PHY(58), data);

	if ((error = otus_write_barrier(sc)) == 0)
		usbd_delay_ms(sc->sc_udev, 10);
	return error;
}

void
otus_get_delta_slope(uint32_t coeff, uint32_t *exponent, uint32_t *mantissa)
{
#define COEFF_SCALE_SHIFT	24
	uint32_t exp, man;

	/* exponent = 14 - floor(log2(coeff)) */
	for (exp = 31; exp > 0; exp--)
		if (coeff & (1 << exp))
			break;
	KASSERT(exp != 0);
	exp = 14 - (exp - COEFF_SCALE_SHIFT);

	/* mantissa = floor(coeff * 2^exponent + 0.5) */
	man = coeff + (1 << (COEFF_SCALE_SHIFT - exp - 1));

	*mantissa = man >> (COEFF_SCALE_SHIFT - exp);
	*exponent = exp - 16;
#undef COEFF_SCALE_SHIFT
}

int
otus_set_chan(struct otus_softc *sc, struct ieee80211_channel *c, int assoc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ar_cmd_frequency cmd;
	struct ar_rsp_frequency rsp;
	const uint32_t *vals;
	uint32_t coeff, exp, man, tmp;
	uint8_t code;
	int error, chan, i;

	chan = ieee80211_chan2ieee(ic, c);
	DPRINTF(("setting channel %d (%dMHz)\n", chan, c->ic_freq));

	tmp = IEEE80211_IS_CHAN_2GHZ(c) ? 0x105 : 0x104;
	otus_write(sc, AR_MAC_REG_DYNAMIC_SIFS_ACK, tmp);
	if ((error = otus_write_barrier(sc)) != 0)
		return error;

	/* Disable BB Heavy Clip. */
	otus_write(sc, AR_PHY_HEAVY_CLIP_ENABLE, 0x200);
	if ((error = otus_write_barrier(sc)) != 0)
		return error;

	/* XXX Is that FREQ_START ? */
	error = otus_cmd(sc, AR_CMD_FREQ_STRAT, NULL, 0, NULL);
	if (error != 0)
		return error;

	/* Reprogram PHY and RF on channel band or bandwidth changes. */
	if (sc->bb_reset || c->ic_flags != sc->sc_curchan->ic_flags) {
		DPRINTF(("band switch\n"));

		/* Cold/Warm reset BB/ADDA. */
		otus_write(sc, 0x1d4004, sc->bb_reset ? 0x800 : 0x400);
		if ((error = otus_write_barrier(sc)) != 0)
			return error;
		otus_write(sc, 0x1d4004, 0);
		if ((error = otus_write_barrier(sc)) != 0)
			return error;
		sc->bb_reset = 0;

		if ((error = otus_program_phy(sc, c)) != 0) {
			printf("%s: could not program PHY\n",
			    sc->sc_dev.dv_xname);
			return error;
		}

		/* Select RF programming based on band. */
		if (IEEE80211_IS_CHAN_5GHZ(c))
			vals = ar5416_banks_vals_5ghz;
		else
			vals = ar5416_banks_vals_2ghz;
		for (i = 0; i < nitems(ar5416_banks_regs); i++)
			otus_write(sc, AR_PHY(ar5416_banks_regs[i]), vals[i]);
		if ((error = otus_write_barrier(sc)) != 0) {
			printf("%s: could not program RF\n",
			    sc->sc_dev.dv_xname);
			return error;
		}
		code = AR_CMD_RF_INIT;
	} else {
		code = AR_CMD_FREQUENCY;
	}

	if ((error = otus_set_rf_bank4(sc, c)) != 0)
		return error;

	tmp = (sc->txmask == 0x5) ? 0x340 : 0x240;
	otus_write(sc, AR_PHY_TURBO, tmp);
	if ((error = otus_write_barrier(sc)) != 0)
		return error;

	/* Send firmware command to set channel. */
	cmd.freq = htole32((uint32_t)c->ic_freq * 1000);
	cmd.dynht2040 = htole32(0);
	cmd.htena = htole32(1);
	/* Set Delta Slope (exponent and mantissa). */
	coeff = (100 << 24) / c->ic_freq;
	otus_get_delta_slope(coeff, &exp, &man);
	cmd.dsc_exp = htole32(exp);
	cmd.dsc_man = htole32(man);
	DPRINTF(("ds coeff=%u exp=%u man=%u\n", coeff, exp, man));
	/* For Short GI, coeff is 9/10 that of normal coeff. */
	coeff = (9 * coeff) / 10;
	otus_get_delta_slope(coeff, &exp, &man);
	cmd.dsc_shgi_exp = htole32(exp);
	cmd.dsc_shgi_man = htole32(man);
	DPRINTF(("ds shgi coeff=%u exp=%u man=%u\n", coeff, exp, man));
	/* Set wait time for AGC and noise calibration (100 or 200ms). */
	cmd.check_loop_count = assoc ? htole32(2000) : htole32(1000);
	DPRINTF(("%s\n", (code == AR_CMD_RF_INIT) ? "RF_INIT" : "FREQUENCY"));
	error = otus_cmd(sc, code, &cmd, sizeof cmd, &rsp);
	if (error != 0)
		return error;
	if ((rsp.status & htole32(AR_CAL_ERR_AGC | AR_CAL_ERR_NF_VAL)) != 0) {
		DPRINTF(("status=0x%x\n", letoh32(rsp.status)));
		/* Force cold reset on next channel. */
		sc->bb_reset = 1;
	}
#ifdef OTUS_DEBUG
	if (otus_debug) {
		printf("calibration status=0x%x\n", letoh32(rsp.status));
		for (i = 0; i < 2; i++) {	/* 2 Rx chains */
			/* Sign-extend 9-bit NF values. */
			printf("noisefloor chain %d=%d\n", i,
			    (((int32_t)letoh32(rsp.nf[i])) << 4) >> 23);
			printf("noisefloor ext chain %d=%d\n", i,
			    ((int32_t)letoh32(rsp.nf_ext[i])) >> 23);
		}
	}
#endif
	sc->sc_curchan = c;
	return 0;
}

#ifdef notyet
int
otus_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct otus_softc *sc = ic->ic_softc;
	struct otus_cmd_key cmd;

	/* Defer setting of WEP keys until interface is brought up. */
	if ((ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) !=
	    (IFF_UP | IFF_RUNNING))
		return 0;

	/* Do it in a process context. */
	cmd.key = *k;
	cmd.ni = *ni;
	otus_do_async(sc, otus_set_key_cb, &cmd, sizeof cmd);
	sc->sc_key_tasks++
	return EBUSY;
}

void
otus_set_key_cb(struct otus_softc *sc, void *arg)
{
	struct otus_cmd_key *cmd = arg;
	struct ieee80211_key *k = &cmd->key;
	struct ar_cmd_ekey key;
	uint16_t cipher;
	int error;

	sc->sc_keys_tasks--;

	memset(&key, 0, sizeof key);
	if (k->k_flags & IEEE80211_KEY_GROUP) {
		key.uid = htole16(k->k_id);
		IEEE80211_ADDR_COPY(key.macaddr, sc->sc_ic.ic_myaddr);
		key.macaddr[0] |= 0x80;
	} else {
		key.uid = htole16(OTUS_UID(cmd->associd));
		IEEE80211_ADDR_COPY(key.macaddr, ni->ni_macaddr);
	}
	key.kix = htole16(0);
	/* Map net80211 cipher to hardware. */
	switch (k->k_cipher) {
	case IEEE80211_CIPHER_WEP40:
		cipher = AR_CIPHER_WEP64;
		break;
	case IEEE80211_CIPHER_WEP104:
		cipher = AR_CIPHER_WEP128;
		break;
	case IEEE80211_CIPHER_TKIP:
		cipher = AR_CIPHER_TKIP;
		break;
	case IEEE80211_CIPHER_CCMP:
		cipher = AR_CIPHER_AES;
		break;
	default:
		IEEE80211_SEND_MGMT(ic, cmd->ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_AUTH_LEAVE);
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		return;
	}
	key.cipher = htole16(cipher);
	memcpy(key.key, k->k_key, MIN(k->k_len, 16));
	error = otus_cmd(sc, AR_CMD_EKEY, &key, sizeof key, NULL);
	if (error != 0 || k->k_cipher != IEEE80211_CIPHER_TKIP) {
		IEEE80211_SEND_MGMT(ic, cmd->ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_AUTH_LEAVE);
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		return;
	}

	/* TKIP: set Tx/Rx MIC Key. */
	key.kix = htole16(1);
	memcpy(key.key, k->k_key + 16, 16);
	(void)otus_cmd(sc, AR_CMD_EKEY, &key, sizeof key, NULL);

	if (sc->sc_key_tasks == 0) {
		DPRINTF(("marking port %s valid\n",
		    ether_sprintf(cmd->ni->ni_macaddr)));
		cmd->ni->ni_port_valid = 1;
		ieee80211_set_link_state(ic, LINK_STATE_UP);
	}
}

void
otus_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct otus_softc *sc = ic->ic_softc;
	struct otus_cmd_key cmd;

	if (!(ic->ic_if.if_flags & IFF_RUNNING) ||
	    ic->ic_state != IEEE80211_S_RUN)
		return;	/* Nothing to do. */

	/* Do it in a process context. */
	cmd.key = *k;
	cmd.associd = (ni != NULL) ? ni->ni_associd : 0;
	otus_do_async(sc, otus_delete_key_cb, &cmd, sizeof cmd);
}

void
otus_delete_key_cb(struct otus_softc *sc, void *arg)
{
	struct otus_cmd_key *cmd = arg;
	struct ieee80211_key *k = &cmd->key;
	uint32_t uid;

	if (k->k_flags & IEEE80211_KEY_GROUP)
		uid = htole32(k->k_id);
	else
		uid = htole32(OTUS_UID(cmd->associd));
	(void)otus_cmd(sc, AR_CMD_DKEY, &uid, sizeof uid, NULL);
}
#endif

void
otus_calibrate_to(void *arg)
{
	struct otus_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);

	s = splnet();
	ni = ic->ic_bss;
	ieee80211_amrr_choose(&sc->amrr, ni, &((struct otus_node *)ni)->amn);
	splx(s);

	if (!usbd_is_dying(sc->sc_udev))
		timeout_add_sec(&sc->calib_to, 1);

	usbd_ref_decr(sc->sc_udev);
}

int
otus_set_bssid(struct otus_softc *sc, const uint8_t *bssid)
{
	otus_write(sc, AR_MAC_REG_BSSID_L,
	    bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24);
	otus_write(sc, AR_MAC_REG_BSSID_H,
	    bssid[4] | bssid[5] << 8);
	return otus_write_barrier(sc);
}

int
otus_set_macaddr(struct otus_softc *sc, const uint8_t *addr)
{
	otus_write(sc, AR_MAC_REG_MAC_ADDR_L,
	    addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	otus_write(sc, AR_MAC_REG_MAC_ADDR_H,
	    addr[4] | addr[5] << 8);
	return otus_write_barrier(sc);
}

/* Default single-LED. */
void
otus_led_newstate_type1(struct otus_softc *sc)
{
	/* TBD */
}

/* NETGEAR, dual-LED. */
void
otus_led_newstate_type2(struct otus_softc *sc)
{
	/* TBD */
}

/* NETGEAR, single-LED/3 colors (blue, red, purple.) */
void
otus_led_newstate_type3(struct otus_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t state = sc->led_state;

	if (ic->ic_state == IEEE80211_S_INIT) {
		state = 0;	/* LED off. */
	} else if (ic->ic_state == IEEE80211_S_RUN) {
		/* Associated, LED always on. */
		if (IEEE80211_IS_CHAN_2GHZ(sc->sc_curchan))
			state = AR_LED0_ON;	/* 2GHz=>Red. */
		else
			state = AR_LED1_ON;	/* 5GHz=>Blue. */
	} else {
		/* Scanning, blink LED. */
		state ^= AR_LED0_ON | AR_LED1_ON;
		if (IEEE80211_IS_CHAN_2GHZ(sc->sc_curchan))
			state &= ~AR_LED1_ON;
		else
			state &= ~AR_LED0_ON;
	}
	if (state != sc->led_state) {
		otus_write(sc, 0x1d0104, state);
		if (otus_write_barrier(sc) == 0)
			sc->led_state = state;
	}
}

int
otus_init(struct ifnet *ifp)
{
	struct otus_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

	/* Init host command ring. */
	sc->cmdq.cur = sc->cmdq.next = sc->cmdq.queued = 0;

	if ((error = otus_init_mac(sc)) != 0) {
		printf("%s: could not initialize MAC\n", sc->sc_dev.dv_xname);
		return error;
	}

	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	(void)otus_set_macaddr(sc, ic->ic_myaddr);

	switch (ic->ic_opmode) {
#ifdef notyet
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_HOSTAP:
		otus_write(sc, 0x1c3700, 0x0f0000a1);
		otus_write(sc, 0x1c3c40, 0x1);
		break;
	case IEEE80211_M_IBSS:
		otus_write(sc, 0x1c3700, 0x0f000000);
		otus_write(sc, 0x1c3c40, 0x1);
		break;
#endif
#endif
	case IEEE80211_M_STA:
		otus_write(sc, 0x1c3700, 0x0f000002);
		otus_write(sc, 0x1c3c40, 0x1);
		break;
	default:
		break;
	}
	otus_write(sc, AR_MAC_REG_SNIFFER,
	    (ic->ic_opmode == IEEE80211_M_MONITOR) ? 0x2000001 : 0x2000000);
	(void)otus_write_barrier(sc);

	sc->bb_reset = 1;	/* Force cold reset. */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	if ((error = otus_set_chan(sc, ic->ic_ibss_chan, 0)) != 0) {
		printf("%s: could not set channel\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* Start Rx. */
	otus_write(sc, AR_MAC_REG_DMA_TRIGGER, AR_DMA_TRIGGER_RXQ);
	(void)otus_write_barrier(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return 0;
}

void
otus_stop(struct ifnet *ifp)
{
	struct otus_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s;

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_del(&sc->scan_to);
	timeout_del(&sc->calib_to);

	s = splusb();
	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	/* Wait for all queued asynchronous commands to complete. */
	usb_wait_task(sc->sc_udev, &sc->sc_task);
	splx(s);

	/* Stop Rx. */
	otus_write(sc, AR_MAC_REG_DMA_TRIGGER, 0);
	(void)otus_write_barrier(sc);

	sc->tx_queued = 0;
}

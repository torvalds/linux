/*	$OpenBSD: if_uath.c,v 1.89 2024/05/23 03:21:08 jsg Exp $	*/

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
 * Driver for Atheros AR5005UG/AR5005UX chipsets.
 *
 * IMPORTANT NOTICE:
 * This driver was written without any documentation or support from Atheros
 * Communications. It is based on a black-box analysis of the Windows binary
 * driver. It handles both pre and post-firmware devices.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/timeout.h>
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
#include <net80211/ieee80211_radiotap.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>	/* needs_reattach() */
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_uathreg.h>
#include <dev/usb/if_uathvar.h>

#ifdef UATH_DEBUG
#define DPRINTF(x)	do { if (uath_debug) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (uath_debug >= (n)) printf x; } while (0)
int uath_debug = 1;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

/*-
 * Various supported device vendors/products.
 * UB51: AR5005UG 802.11b/g, UB52: AR5005UX 802.11a/b/g
 */
#define UATH_DEV(v, p, f)						\
	{ { USB_VENDOR_##v, USB_PRODUCT_##v##_##p }, (f) },		\
	{ { USB_VENDOR_##v, USB_PRODUCT_##v##_##p##_NF },		\
	    (f) | UATH_FLAG_PRE_FIRMWARE }
#define UATH_DEV_UG(v, p)	UATH_DEV(v, p, 0)
#define UATH_DEV_UX(v, p)	UATH_DEV(v, p, UATH_FLAG_ABG)
static const struct uath_type {
	struct usb_devno	dev;
	unsigned int		flags;
#define UATH_FLAG_PRE_FIRMWARE	(1 << 0)
#define UATH_FLAG_ABG		(1 << 1)
} uath_devs[] = {
	UATH_DEV_UG(ACCTON,		SMCWUSBTG2),
	UATH_DEV_UG(ATHEROS,		AR5523),
	UATH_DEV_UG(ATHEROS2,		AR5523_1),
	UATH_DEV_UG(ATHEROS2,		AR5523_2),
	UATH_DEV_UX(ATHEROS2,		AR5523_3),
	UATH_DEV_UG(CONCEPTRONIC,	AR5523_1),
	UATH_DEV_UX(CONCEPTRONIC,	AR5523_2),
	UATH_DEV_UX(DLINK,		DWLAG122),
	UATH_DEV_UX(DLINK,		DWLAG132),	
	UATH_DEV_UG(DLINK,		DWLG132),
	UATH_DEV_UG(DLINK2,		WUA2340),
	UATH_DEV_UG(GIGASET,		AR5523),
	UATH_DEV_UG(GIGASET,		SMCWUSBTG),
	UATH_DEV_UG(GLOBALSUN,		AR5523_1),
	UATH_DEV_UX(GLOBALSUN,		AR5523_2),
	UATH_DEV_UG(IODATA,		USBWNG54US),
	UATH_DEV_UG(MELCO,		WLIU2KAMG54),
	UATH_DEV_UX(NETGEAR,		WG111U),
	UATH_DEV_UG(NETGEAR3,		WG111T),
	UATH_DEV_UG(NETGEAR3,		WPN111),
	UATH_DEV_UG(PHILIPS,		SNU6500),
	UATH_DEV_UX(UMEDIA,		AR5523_2),
	UATH_DEV_UG(UMEDIA,		TEW444UBEU),
	UATH_DEV_UG(WISTRONNEWEB,	AR5523_1),
	UATH_DEV_UX(WISTRONNEWEB,	AR5523_2),
	UATH_DEV_UG(ZCOM,		AR5523),

	/* Devices that share one of the IDs above. */
	{ { USB_VENDOR_NETGEAR3, USB_PRODUCT_NETGEAR3_WG111T_1 }, 0 }		\
};
#define uath_lookup(v, p)	\
	((const struct uath_type *)usb_lookup(uath_devs, v, p))

void	uath_attachhook(struct device *);
int	uath_open_pipes(struct uath_softc *);
void	uath_close_pipes(struct uath_softc *);
int	uath_alloc_tx_data_list(struct uath_softc *);
void	uath_free_tx_data_list(struct uath_softc *);
int	uath_alloc_rx_data_list(struct uath_softc *);
void	uath_free_rx_data_list(struct uath_softc *);
int	uath_alloc_tx_cmd_list(struct uath_softc *);
void	uath_free_tx_cmd_list(struct uath_softc *);
int	uath_alloc_rx_cmd_list(struct uath_softc *);
void	uath_free_rx_cmd_list(struct uath_softc *);
int	uath_media_change(struct ifnet *);
void	uath_stat(void *);
void	uath_next_scan(void *);
void	uath_task(void *);
int	uath_newstate(struct ieee80211com *, enum ieee80211_state, int);
#ifdef UATH_DEBUG
void	uath_dump_cmd(const uint8_t *, int, char);
#endif
int	uath_cmd(struct uath_softc *, uint32_t, const void *, int, void *,
	    int);
int	uath_cmd_write(struct uath_softc *, uint32_t, const void *, int, int);
int	uath_cmd_read(struct uath_softc *, uint32_t, const void *, int, void *,
	    int);
int	uath_write_reg(struct uath_softc *, uint32_t, uint32_t);
int	uath_write_multi(struct uath_softc *, uint32_t, const void *, int);
int	uath_read_reg(struct uath_softc *, uint32_t, uint32_t *);
int	uath_read_eeprom(struct uath_softc *, uint32_t, void *);
void	uath_cmd_rxeof(struct usbd_xfer *, void *, usbd_status);
void	uath_data_rxeof(struct usbd_xfer *, void *, usbd_status);
void	uath_data_txeof(struct usbd_xfer *, void *, usbd_status);
int	uath_tx_null(struct uath_softc *);
int	uath_tx_data(struct uath_softc *, struct mbuf *,
	    struct ieee80211_node *);
void	uath_start(struct ifnet *);
void	uath_watchdog(struct ifnet *);
int	uath_ioctl(struct ifnet *, u_long, caddr_t);
int	uath_query_eeprom(struct uath_softc *);
int	uath_reset(struct uath_softc *);
int	uath_reset_tx_queues(struct uath_softc *);
int	uath_wme_init(struct uath_softc *);
int	uath_set_chan(struct uath_softc *, struct ieee80211_channel *);
int	uath_set_key(struct uath_softc *, const struct ieee80211_key *, int);
int	uath_set_keys(struct uath_softc *);
int	uath_set_rates(struct uath_softc *, const struct ieee80211_rateset *);
int	uath_set_rxfilter(struct uath_softc *, uint32_t, uint32_t);
int	uath_set_led(struct uath_softc *, int, int);
int	uath_switch_channel(struct uath_softc *, struct ieee80211_channel *);
int	uath_init(struct ifnet *);
void	uath_stop(struct ifnet *, int);
int	uath_loadfirmware(struct uath_softc *, const u_char *, int);

int uath_match(struct device *, void *, void *);
void uath_attach(struct device *, struct device *, void *);
int uath_detach(struct device *, int);

struct cfdriver uath_cd = {
	NULL, "uath", DV_IFNET
};

const struct cfattach uath_ca = {
	sizeof(struct uath_softc), uath_match, uath_attach, uath_detach
};

int
uath_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != UATH_CONFIG_NO)
		return UMATCH_NONE;

	return (uath_lookup(uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
uath_attachhook(struct device *self)
{
	struct uath_softc *sc = (struct uath_softc *)self;
	u_char *fw;
	size_t size;
	int error;

	if ((error = loadfirmware("uath-ar5523", &fw, &size)) != 0) {
		printf("%s: error %d, could not read firmware %s\n",
		    sc->sc_dev.dv_xname, error, "uath-ar5523");
		return;
	}

	error = uath_loadfirmware(sc, fw, size);
	free(fw, M_DEVBUF, size);

	if (error == 0) {
		/*
		 * Hack alert: the device doesn't always gracefully detach
		 * from the bus after a firmware upload.  We need to force
		 * a port reset and a re-exploration on the parent hub.
		 */
		usbd_reset_port(sc->sc_uhub, sc->sc_port);
		usb_needs_reattach(sc->sc_udev);
	} else {
		printf("%s: could not load firmware (error=%s)\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
	}
}

void
uath_attach(struct device *parent, struct device *self, void *aux)
{
	struct uath_softc *sc = (struct uath_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	usbd_status error;
	int i;

	sc->sc_udev = uaa->device;
	sc->sc_uhub = uaa->device->myhub;
	sc->sc_port = uaa->port;

	sc->sc_flags = uath_lookup(uaa->vendor, uaa->product)->flags;

	/* get the first interface handle */
	error = usbd_device2interface_handle(sc->sc_udev, UATH_IFACE_INDEX,
	    &sc->sc_iface);
	if (error != 0) {
		printf("%s: could not get interface handle\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * We must open the pipes early because they're used to upload the
	 * firmware (pre-firmware devices) or to send firmware commands.
	 */
	if (uath_open_pipes(sc) != 0) {
		printf("%s: could not open pipes\n", sc->sc_dev.dv_xname);
		return;
	}

	if (sc->sc_flags & UATH_FLAG_PRE_FIRMWARE) {
		config_mountroot(self, uath_attachhook);
		return;
	}

	/*
	 * Only post-firmware devices here.
	 */
	usb_init_task(&sc->sc_task, uath_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->scan_to, uath_next_scan, sc);
	timeout_set(&sc->stat_to, uath_stat, sc);

	/*
	 * Allocate xfers for firmware commands.
	 */
	if (uath_alloc_tx_cmd_list(sc) != 0) {
		printf("%s: could not allocate Tx command list\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	if (uath_alloc_rx_cmd_list(sc) != 0) {
		printf("%s: could not allocate Rx command list\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/*
	 * Queue Rx command xfers.
	 */
	for (i = 0; i < UATH_RX_CMD_LIST_COUNT; i++) {
		struct uath_rx_cmd *cmd = &sc->rx_cmd[i];

		usbd_setup_xfer(cmd->xfer, sc->cmd_rx_pipe, cmd, cmd->buf,
		    UATH_MAX_RXCMDSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, uath_cmd_rxeof);
		error = usbd_transfer(cmd->xfer);
		if (error != USBD_IN_PROGRESS && error != 0) {
			printf("%s: could not queue Rx command xfer\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}

	/*
	 * We're now ready to send/receive firmware commands.
	 */
	if (uath_reset(sc) != 0) {
		printf("%s: could not initialize adapter\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	if (uath_query_eeprom(sc) != 0) {
		printf("%s: could not read EEPROM\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	printf("%s: MAC/BBP AR5523, RF AR%c112, address %s\n",
	    sc->sc_dev.dv_xname, (sc->sc_flags & UATH_FLAG_ABG) ? '5': '2',
	    ether_sprintf(ic->ic_myaddr));

	/*
	 * Allocate xfers for Tx/Rx data pipes.
	 */
	if (uath_alloc_tx_data_list(sc) != 0) {
		printf("%s: could not allocate Tx data list\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	if (uath_alloc_rx_data_list(sc) != 0) {
		printf("%s: could not allocate Rx data list\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_TXPMGT |	/* tx power management */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WEP;		/* h/w WEP */

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
	ifp->if_ioctl = uath_ioctl;
	ifp->if_start = uath_start;
	ifp->if_watchdog = uath_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);

	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = uath_newstate;
	ieee80211_media_init(ifp, uath_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(UATH_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(UATH_TX_RADIOTAP_PRESENT);
#endif

	return;

fail:	uath_close_pipes(sc);
	uath_free_tx_data_list(sc);
	uath_free_rx_cmd_list(sc);
	uath_free_tx_cmd_list(sc);
	usbd_deactivate(sc->sc_udev);
}

int
uath_detach(struct device *self, int flags)
{
	struct uath_softc *sc = (struct uath_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s;

	s = splnet();

	if (sc->sc_flags & UATH_FLAG_PRE_FIRMWARE) {
		uath_close_pipes(sc);
		splx(s);
		return 0;
	}

	/* post-firmware device */

	usb_rem_task(sc->sc_udev, &sc->sc_task);
	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
	if (timeout_initialized(&sc->stat_to))
		timeout_del(&sc->stat_to);

	/* close Tx/Rx pipes */
	uath_close_pipes(sc);

	/* free xfers */
	uath_free_tx_data_list(sc);
	uath_free_rx_data_list(sc);
	uath_free_tx_cmd_list(sc);
	uath_free_rx_cmd_list(sc);

	if (ifp->if_softc != NULL) {
		ieee80211_ifdetach(ifp);	/* free all nodes */
		if_detach(ifp);
	}

	splx(s);

	return 0;
}

int
uath_open_pipes(struct uath_softc *sc)
{
	int error;

	/*
	 * XXX pipes numbers are hardcoded because we don't have any way
	 * to distinguish the data pipes from the firmware command pipes
	 * (both are bulk pipes) using the endpoints descriptors.
	 */
	error = usbd_open_pipe(sc->sc_iface, 0x01, USBD_EXCLUSIVE_USE,
	    &sc->cmd_tx_pipe);
	if (error != 0) {
		printf("%s: could not open Tx command pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	error = usbd_open_pipe(sc->sc_iface, 0x02, USBD_EXCLUSIVE_USE,
	    &sc->data_tx_pipe);
	if (error != 0) {
		printf("%s: could not open Tx data pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	error = usbd_open_pipe(sc->sc_iface, 0x81, USBD_EXCLUSIVE_USE,
	    &sc->cmd_rx_pipe);
	if (error != 0) {
		printf("%s: could not open Rx command pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	error = usbd_open_pipe(sc->sc_iface, 0x82, USBD_EXCLUSIVE_USE,
	    &sc->data_rx_pipe);
	if (error != 0) {
		printf("%s: could not open Rx data pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	return 0;

fail:	uath_close_pipes(sc);
	return error;
}

void
uath_close_pipes(struct uath_softc *sc)
{
	if (sc->data_tx_pipe != NULL) {
		usbd_close_pipe(sc->data_tx_pipe);
		sc->data_tx_pipe = NULL;
	}

	if (sc->data_rx_pipe != NULL) {
		usbd_close_pipe(sc->data_rx_pipe);
		sc->data_rx_pipe = NULL;
	}

	if (sc->cmd_tx_pipe != NULL) {
		usbd_close_pipe(sc->cmd_tx_pipe);
		sc->cmd_tx_pipe = NULL;
	}

	if (sc->cmd_rx_pipe != NULL) {
		usbd_close_pipe(sc->cmd_rx_pipe);
		sc->cmd_rx_pipe = NULL;
	}
}

int
uath_alloc_tx_data_list(struct uath_softc *sc)
{
	int i, error;

	for (i = 0; i < UATH_TX_DATA_LIST_COUNT; i++) {
		struct uath_tx_data *data = &sc->tx_data[i];

		data->sc = sc;	/* backpointer for callbacks */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		data->buf = usbd_alloc_buffer(data->xfer, UATH_MAX_TXBUFSZ);
		if (data->buf == NULL) {
			printf("%s: could not allocate xfer buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
	}
	return 0;

fail:	uath_free_tx_data_list(sc);
	return error;
}

void
uath_free_tx_data_list(struct uath_softc *sc)
{
	int i;

	for (i = 0; i < UATH_TX_DATA_LIST_COUNT; i++)
		if (sc->tx_data[i].xfer != NULL) {
			usbd_free_xfer(sc->tx_data[i].xfer);
			sc->tx_data[i].xfer = NULL;
		}
}

int
uath_alloc_rx_data_list(struct uath_softc *sc)
{
	int i, error;

	for (i = 0; i < UATH_RX_DATA_LIST_COUNT; i++) {
		struct uath_rx_data *data = &sc->rx_data[i];

		data->sc = sc;	/* backpointer for callbacks */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		if (usbd_alloc_buffer(data->xfer, sc->rxbufsz) == NULL) {
			printf("%s: could not allocate xfer buffer\n",
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
		MCLGETL(data->m, M_DONTWAIT, sc->rxbufsz);
		if (!(data->m->m_flags & M_EXT)) {
			printf("%s: could not allocate rx mbuf cluster\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		data->buf = mtod(data->m, uint8_t *);
	}
	return 0;

fail:	uath_free_rx_data_list(sc);
	return error;
}

void
uath_free_rx_data_list(struct uath_softc *sc)
{
	int i;

	for (i = 0; i < UATH_RX_DATA_LIST_COUNT; i++) {
		struct uath_rx_data *data = &sc->rx_data[i];

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
uath_alloc_tx_cmd_list(struct uath_softc *sc)
{
	int i, error;

	for (i = 0; i < UATH_TX_CMD_LIST_COUNT; i++) {
		struct uath_tx_cmd *cmd = &sc->tx_cmd[i];

		cmd->sc = sc;	/* backpointer for callbacks */

		cmd->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (cmd->xfer == NULL) {
			printf("%s: could not allocate xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		cmd->buf = usbd_alloc_buffer(cmd->xfer, UATH_MAX_TXCMDSZ);
		if (cmd->buf == NULL) {
			printf("%s: could not allocate xfer buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
	}
	return 0;

fail:	uath_free_tx_cmd_list(sc);
	return error;
}

void
uath_free_tx_cmd_list(struct uath_softc *sc)
{
	int i;

	for (i = 0; i < UATH_TX_CMD_LIST_COUNT; i++)
		if (sc->tx_cmd[i].xfer != NULL) {
			usbd_free_xfer(sc->tx_cmd[i].xfer);
			sc->tx_cmd[i].xfer = NULL;
		}
}

int
uath_alloc_rx_cmd_list(struct uath_softc *sc)
{
	int i, error;

	for (i = 0; i < UATH_RX_CMD_LIST_COUNT; i++) {
		struct uath_rx_cmd *cmd = &sc->rx_cmd[i];

		cmd->sc = sc;	/* backpointer for callbacks */

		cmd->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (cmd->xfer == NULL) {
			printf("%s: could not allocate xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		cmd->buf = usbd_alloc_buffer(cmd->xfer, UATH_MAX_RXCMDSZ);
		if (cmd->buf == NULL) {
			printf("%s: could not allocate xfer buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
	}
	return 0;

fail:	uath_free_rx_cmd_list(sc);
	return error;
}

void
uath_free_rx_cmd_list(struct uath_softc *sc)
{
	int i;

	for (i = 0; i < UATH_RX_CMD_LIST_COUNT; i++)
		if (sc->rx_cmd[i].xfer != NULL) {
			usbd_free_xfer(sc->rx_cmd[i].xfer);
			sc->rx_cmd[i].xfer = NULL;
		}
}

int
uath_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		error = uath_init(ifp);

	return error;
}

/*
 * This function is called periodically (every second) when associated to
 * query device statistics.
 */
void
uath_stat(void *arg)
{
	struct uath_softc *sc = arg;
	int error;

	/*
	 * Send request for statistics asynchronously. The timer will be
	 * restarted when we'll get the stats notification.
	 */
	error = uath_cmd_write(sc, UATH_CMD_STATS, NULL, 0,
	    UATH_CMD_FLAG_ASYNC);
	if (error != 0) {
		printf("%s: could not query statistics (error=%d)\n",
		    sc->sc_dev.dv_xname, error);
	}
}

/*
 * This function is called periodically (every 250ms) during scanning to
 * switch from one channel to another.
 */
void
uath_next_scan(void *arg)
{
	struct uath_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);
}

void
uath_task(void *arg)
{
	struct uath_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state ostate;

	ostate = ic->ic_state;

	switch (sc->sc_state) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* turn link and activity LEDs off */
			(void)uath_set_led(sc, UATH_LED_LINK, 0);
			(void)uath_set_led(sc, UATH_LED_ACTIVITY, 0);
		}
		break;

	case IEEE80211_S_SCAN:
		if (uath_switch_channel(sc, ic->ic_bss->ni_chan) != 0) {
			printf("%s: could not switch channel\n",
			    sc->sc_dev.dv_xname);
			break;
		}
		timeout_add_msec(&sc->scan_to, 250);
		break;

	case IEEE80211_S_AUTH:
	{
		struct ieee80211_node *ni = ic->ic_bss;
		struct uath_cmd_bssid bssid;
		struct uath_cmd_0b cmd0b;
		struct uath_cmd_0c cmd0c;

		if (uath_switch_channel(sc, ni->ni_chan) != 0) {
			printf("%s: could not switch channel\n",
			    sc->sc_dev.dv_xname);
			break;
		}

		(void)uath_cmd_write(sc, UATH_CMD_24, NULL, 0, 0);

		bzero(&bssid, sizeof bssid);
		bssid.len = htobe32(IEEE80211_ADDR_LEN);
		IEEE80211_ADDR_COPY(bssid.bssid, ni->ni_bssid);
		(void)uath_cmd_write(sc, UATH_CMD_SET_BSSID, &bssid,
		    sizeof bssid, 0);

		bzero(&cmd0b, sizeof cmd0b);
		cmd0b.code = htobe32(2);
		cmd0b.size = htobe32(sizeof (cmd0b.data));
		(void)uath_cmd_write(sc, UATH_CMD_0B, &cmd0b, sizeof cmd0b, 0);

		bzero(&cmd0c, sizeof cmd0c);
		cmd0c.magic1 = htobe32(2);
		cmd0c.magic2 = htobe32(7);
		cmd0c.magic3 = htobe32(1);
		(void)uath_cmd_write(sc, UATH_CMD_0C, &cmd0c, sizeof cmd0c, 0);

		if (uath_set_rates(sc, &ni->ni_rates) != 0) {
			printf("%s: could not set negotiated rate set\n",
			    sc->sc_dev.dv_xname);
			break;
		}
		break;
	}

	case IEEE80211_S_ASSOC:
		break;

	case IEEE80211_S_RUN:
	{
		struct ieee80211_node *ni = ic->ic_bss;
		struct uath_cmd_bssid bssid;
		struct uath_cmd_xled xled;
		uint32_t val;

		if (ic->ic_opmode == IEEE80211_M_MONITOR) {
			/* make both LEDs blink while monitoring */
			bzero(&xled, sizeof xled);
			xled.which = htobe32(0);
			xled.rate = htobe32(1);
			xled.mode = htobe32(2);
			(void)uath_cmd_write(sc, UATH_CMD_SET_XLED, &xled,
			    sizeof xled, 0);
			break;
		}

		/*
		 * Tx rate is controlled by firmware, report the maximum
		 * negotiated rate in ifconfig output.
		 */
		ni->ni_txrate = ni->ni_rates.rs_nrates - 1;

		val = htobe32(1);
		(void)uath_cmd_write(sc, UATH_CMD_2E, &val, sizeof val, 0);

		bzero(&bssid, sizeof bssid);
		bssid.flags1 = htobe32(0xc004);
		bssid.flags2 = htobe32(0x003b);
		bssid.len = htobe32(IEEE80211_ADDR_LEN);
		IEEE80211_ADDR_COPY(bssid.bssid, ni->ni_bssid);
		(void)uath_cmd_write(sc, UATH_CMD_SET_BSSID, &bssid,
		    sizeof bssid, 0);

		/* turn link LED on */
		(void)uath_set_led(sc, UATH_LED_LINK, 1);

		/* make activity LED blink */
		bzero(&xled, sizeof xled);
		xled.which = htobe32(1);
		xled.rate = htobe32(1);
		xled.mode = htobe32(2);
		(void)uath_cmd_write(sc, UATH_CMD_SET_XLED, &xled, sizeof xled,
		    0);

		/* set state to associated */
		val = htobe32(1);
		(void)uath_cmd_write(sc, UATH_CMD_SET_STATE, &val, sizeof val,
		    0);

		/* start statistics timer */
		timeout_add_sec(&sc->stat_to, 1);
		break;
	}
	}
	sc->sc_newstate(ic, sc->sc_state, sc->sc_arg);
}

int
uath_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct uath_softc *sc = ic->ic_softc;

	usb_rem_task(sc->sc_udev, &sc->sc_task);
	timeout_del(&sc->scan_to);
	timeout_del(&sc->stat_to);

	/* do it in a process context */
	sc->sc_state = nstate;
	sc->sc_arg = arg;
	usb_add_task(sc->sc_udev, &sc->sc_task);
	return 0;
}

#ifdef UATH_DEBUG
void
uath_dump_cmd(const uint8_t *buf, int len, char prefix)
{
	int i;

	for (i = 0; i < len; i++) {
		if ((i % 16) == 0)
			printf("\n%c ", prefix);
		else if ((i % 4) == 0)
			printf(" ");
		printf("%02x", buf[i]);
	}
	printf("\n");
}
#endif

/*
 * Low-level function to send read or write commands to the firmware.
 */
int
uath_cmd(struct uath_softc *sc, uint32_t code, const void *idata, int ilen,
    void *odata, int flags)
{
	struct uath_cmd_hdr *hdr;
	struct uath_tx_cmd *cmd;
	uint16_t xferflags;
	int s, xferlen, error;

	/* grab a xfer */
	cmd = &sc->tx_cmd[sc->cmd_idx];

	/* always bulk-out a multiple of 4 bytes */
	xferlen = (sizeof (struct uath_cmd_hdr) + ilen + 3) & ~3;

	hdr = (struct uath_cmd_hdr *)cmd->buf;
	bzero(hdr, sizeof (struct uath_cmd_hdr));
	hdr->len   = htobe32(xferlen);
	hdr->code  = htobe32(code);
	hdr->priv  = sc->cmd_idx;	/* don't care about endianness */
	hdr->magic = htobe32((flags & UATH_CMD_FLAG_MAGIC) ? 1 << 24 : 0);
	bcopy(idata, (uint8_t *)(hdr + 1), ilen);

#ifdef UATH_DEBUG
	if (uath_debug >= 5) {
		printf("sending command code=0x%02x flags=0x%x index=%u",
		    code, flags, sc->cmd_idx);
		uath_dump_cmd(cmd->buf, xferlen, '+');
	}
#endif
	xferflags = USBD_FORCE_SHORT_XFER | USBD_NO_COPY;
	if (!(flags & UATH_CMD_FLAG_READ)) {
		if (!(flags & UATH_CMD_FLAG_ASYNC))
			xferflags |= USBD_SYNCHRONOUS;
	} else
		s = splusb();

	cmd->odata = odata;

	usbd_setup_xfer(cmd->xfer, sc->cmd_tx_pipe, cmd, cmd->buf, xferlen,
	    xferflags, UATH_CMD_TIMEOUT, NULL);
	error = usbd_transfer(cmd->xfer);
	if (error != USBD_IN_PROGRESS && error != 0) {
		if (flags & UATH_CMD_FLAG_READ)
			splx(s);
		printf("%s: could not send command 0x%x (error=%s)\n",
		    sc->sc_dev.dv_xname, code, usbd_errstr(error));
		return error;
	}
	sc->cmd_idx = (sc->cmd_idx + 1) % UATH_TX_CMD_LIST_COUNT;

	if (!(flags & UATH_CMD_FLAG_READ))
		return 0;	/* write: don't wait for reply */

	/* wait at most two seconds for command reply */
	error = tsleep_nsec(cmd, PCATCH, "uathcmd", SEC_TO_NSEC(2));
	cmd->odata = NULL;	/* in case answer is received too late */
	splx(s);
	if (error != 0) {
		printf("%s: timeout waiting for command reply\n",
		    sc->sc_dev.dv_xname);
	}
	return error;
}

int
uath_cmd_write(struct uath_softc *sc, uint32_t code, const void *data, int len,
    int flags)
{
	flags &= ~UATH_CMD_FLAG_READ;
	return uath_cmd(sc, code, data, len, NULL, flags);
}

int
uath_cmd_read(struct uath_softc *sc, uint32_t code, const void *idata,
    int ilen, void *odata, int flags)
{
	flags |= UATH_CMD_FLAG_READ;
	return uath_cmd(sc, code, idata, ilen, odata, flags);
}

int
uath_write_reg(struct uath_softc *sc, uint32_t reg, uint32_t val)
{
	struct uath_write_mac write;
	int error;

	write.reg = htobe32(reg);
	write.len = htobe32(0);	/* 0 = single write */
	*(uint32_t *)write.data = htobe32(val);

	error = uath_cmd_write(sc, UATH_CMD_WRITE_MAC, &write,
	    3 * sizeof (uint32_t), 0);
	if (error != 0) {
		printf("%s: could not write register 0x%02x\n",
		    sc->sc_dev.dv_xname, reg);
	}
	return error;
}

int
uath_write_multi(struct uath_softc *sc, uint32_t reg, const void *data,
    int len)
{
	struct uath_write_mac write;
	int error;

	write.reg = htobe32(reg);
	write.len = htobe32(len);
	bcopy(data, write.data, len);

	/* properly handle the case where len is zero (reset) */
	error = uath_cmd_write(sc, UATH_CMD_WRITE_MAC, &write,
	    (len == 0) ? sizeof (uint32_t) : 2 * sizeof (uint32_t) + len, 0);
	if (error != 0) {
		printf("%s: could not write %d bytes to register 0x%02x\n",
		    sc->sc_dev.dv_xname, len, reg);
	}
	return error;
}

int
uath_read_reg(struct uath_softc *sc, uint32_t reg, uint32_t *val)
{
	struct uath_read_mac read;
	int error;

	reg = htobe32(reg);
	error = uath_cmd_read(sc, UATH_CMD_READ_MAC, &reg, sizeof reg, &read,
	    0);
	if (error != 0) {
		printf("%s: could not read register 0x%02x\n",
		    sc->sc_dev.dv_xname, betoh32(reg));
		return error;
	}
	*val = betoh32(*(uint32_t *)read.data);
	return error;
}

int
uath_read_eeprom(struct uath_softc *sc, uint32_t reg, void *odata)
{
	struct uath_read_mac read;
	int len, error;

	reg = htobe32(reg);
	error = uath_cmd_read(sc, UATH_CMD_READ_EEPROM, &reg, sizeof reg,
	    &read, 0);
	if (error != 0) {
		printf("%s: could not read EEPROM offset 0x%02x\n",
		    sc->sc_dev.dv_xname, betoh32(reg));
		return error;
	}
	len = betoh32(read.len);
	bcopy(read.data, odata, (len == 0) ? sizeof (uint32_t) : len);
	return error;
}

void
uath_cmd_rxeof(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct uath_rx_cmd *cmd = priv;
	struct uath_softc *sc = cmd->sc;
	struct uath_cmd_hdr *hdr;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->cmd_rx_pipe);
		return;
	}

	hdr = (struct uath_cmd_hdr *)cmd->buf;

#ifdef UATH_DEBUG
	if (uath_debug >= 5) {
		printf("received command code=0x%x index=%u len=%u",
		    betoh32(hdr->code), hdr->priv, betoh32(hdr->len));
		uath_dump_cmd(cmd->buf, betoh32(hdr->len), '-');
	}
#endif

	switch (betoh32(hdr->code) & 0xff) {
	/* reply to a read command */
	default:
	{
		struct uath_tx_cmd *txcmd = &sc->tx_cmd[hdr->priv];

		if (txcmd->odata != NULL) {
			/* copy answer into caller's supplied buffer */
			bcopy((uint8_t *)(hdr + 1), txcmd->odata,
			    betoh32(hdr->len) - sizeof (struct uath_cmd_hdr));
		}
		wakeup(txcmd);	/* wake up caller */
		break;
	}
	/* spontaneous firmware notifications */
	case UATH_NOTIF_READY:
		DPRINTF(("received device ready notification\n"));
		wakeup(UATH_COND_INIT(sc));
		break;

	case UATH_NOTIF_TX:
		/* this notification is sent when UATH_TX_NOTIFY is set */
		DPRINTF(("received Tx notification\n"));
		break;

	case UATH_NOTIF_STATS:
		DPRINTFN(2, ("received device statistics\n"));
		timeout_add_sec(&sc->stat_to, 1);
		break;
	}

	/* setup a new transfer */
	usbd_setup_xfer(xfer, sc->cmd_rx_pipe, cmd, cmd->buf, UATH_MAX_RXCMDSZ,
	    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT,
	    uath_cmd_rxeof);
	(void)usbd_transfer(xfer);
}

void
uath_data_rxeof(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct uath_rx_data *data = priv;
	struct uath_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct uath_rx_desc *desc;
	struct mbuf *mnew, *m;
	uint32_t hdr;
	int s, len;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->data_rx_pipe);

		ifp->if_ierrors++;
		return;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	if (len < UATH_MIN_RXBUFSZ) {
		DPRINTF(("wrong xfer size (len=%d)\n", len));
		ifp->if_ierrors++;
		goto skip;
	}

	hdr = betoh32(*(uint32_t *)data->buf);

	/* Rx descriptor is located at the end, 32-bit aligned */
	desc = (struct uath_rx_desc *)
	    (data->buf + len - sizeof (struct uath_rx_desc));

	if (betoh32(desc->len) > sc->rxbufsz) {
		DPRINTF(("bad descriptor (len=%d)\n", betoh32(desc->len)));
		ifp->if_ierrors++;
		goto skip;
	}

	/* there's probably a "bad CRC" flag somewhere in the descriptor.. */

	MGETHDR(mnew, M_DONTWAIT, MT_DATA);
	if (mnew == NULL) {
		printf("%s: could not allocate rx mbuf\n",
		    sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		goto skip;
	}
	MCLGETL(mnew, M_DONTWAIT, sc->rxbufsz);
	if (!(mnew->m_flags & M_EXT)) {
		printf("%s: could not allocate rx mbuf cluster\n",
		    sc->sc_dev.dv_xname);
		m_freem(mnew);
		ifp->if_ierrors++;
		goto skip;
	}

	m = data->m;
	data->m = mnew;

	/* finalize mbuf */
	m->m_data = data->buf + sizeof (uint32_t);
	m->m_pkthdr.len = m->m_len = betoh32(desc->len) -
	    sizeof (struct uath_rx_desc) - IEEE80211_CRC_LEN;

	data->buf = mtod(data->m, uint8_t *);

	wh = mtod(m, struct ieee80211_frame *);
	memset(&rxi, 0, sizeof(rxi));
	if ((wh->i_fc[1] & IEEE80211_FC1_WEP) &&
	    ic->ic_opmode != IEEE80211_M_MONITOR) {
		/*
		 * Hardware decrypts the frame itself but leaves the WEP bit
		 * set in the 802.11 header and doesn't remove the IV and CRC
		 * fields.
		 */
		wh->i_fc[1] &= ~IEEE80211_FC1_WEP;
		memmove((caddr_t)wh + IEEE80211_WEP_IVLEN +
		    IEEE80211_WEP_KIDLEN, wh, sizeof (struct ieee80211_frame));
		m_adj(m, IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN);
		m_adj(m, -IEEE80211_WEP_CRCLEN);
		wh = mtod(m, struct ieee80211_frame *);

		rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
	}

#if NBPFILTER > 0
	/* there are a lot more fields in the Rx descriptor */
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct uath_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		tap->wr_chan_freq = htole16(betoh32(desc->freq));
		tap->wr_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		tap->wr_dbm_antsignal = (int8_t)betoh32(desc->rssi);

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
	rxi.rxi_rssi = (int)betoh32(desc->rssi);
	ieee80211_input(ifp, m, ni, &rxi);

	/* node is no longer needed */
	ieee80211_release_node(ic, ni);
	splx(s);

skip:	/* setup a new transfer */
	usbd_setup_xfer(xfer, sc->data_rx_pipe, data, data->buf, sc->rxbufsz,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, uath_data_rxeof);
	(void)usbd_transfer(data->xfer);
}

int
uath_tx_null(struct uath_softc *sc)
{
	struct uath_tx_data *data;
	struct uath_tx_desc *desc;

	data = &sc->tx_data[sc->data_idx];

	data->ni = NULL;

	*(uint32_t *)data->buf = UATH_MAKECTL(1, sizeof (struct uath_tx_desc));
	desc = (struct uath_tx_desc *)(data->buf + sizeof (uint32_t));

	bzero(desc, sizeof (struct uath_tx_desc));
	desc->len  = htobe32(sizeof (struct uath_tx_desc));
	desc->type = htobe32(UATH_TX_NULL);

	usbd_setup_xfer(data->xfer, sc->data_tx_pipe, data, data->buf,
	    sizeof (uint32_t) + sizeof (struct uath_tx_desc), USBD_NO_COPY |
	    USBD_FORCE_SHORT_XFER | USBD_SYNCHRONOUS, UATH_DATA_TIMEOUT, NULL);
	if (usbd_transfer(data->xfer) != 0)
		return EIO;

	sc->data_idx = (sc->data_idx + 1) % UATH_TX_DATA_LIST_COUNT;

	return uath_cmd_write(sc, UATH_CMD_0F, NULL, 0, UATH_CMD_FLAG_ASYNC);
}

void
uath_data_txeof(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct uath_tx_data *data = priv;
	struct uath_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		printf("%s: could not transmit buffer: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(status));

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->data_tx_pipe);

		ifp->if_oerrors++;
		return;
	}

	s = splnet();

	ieee80211_release_node(ic, data->ni);
	data->ni = NULL;

	sc->tx_queued--;

	sc->sc_tx_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);
	uath_start(ifp);

	splx(s);
}

int
uath_tx_data(struct uath_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct uath_tx_data *data;
	struct uath_tx_desc *desc;
	const struct ieee80211_frame *wh;
	int paylen, totlen, xferlen, error;

	data = &sc->tx_data[sc->data_idx];
	desc = (struct uath_tx_desc *)(data->buf + sizeof (uint32_t));

	data->ni = ni;

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct uath_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_txtap_len;
		mb.m_next = m0;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_OUT);
	}
#endif

	paylen = m0->m_pkthdr.len;
	xferlen = sizeof (uint32_t) + sizeof (struct uath_tx_desc) + paylen;

	wh = mtod(m0, struct ieee80211_frame *);
	if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
		uint8_t *frm = (uint8_t *)(desc + 1);
		uint32_t iv;

		/* h/w WEP: it's up to the host to fill the IV field */
		bcopy(wh, frm, sizeof (struct ieee80211_frame));
		frm += sizeof (struct ieee80211_frame);

		/* insert IV: code copied from net80211 */
		iv = (ic->ic_iv != 0) ? ic->ic_iv : arc4random();
		if (iv >= 0x03ff00 && (iv & 0xf8ff00) == 0x00ff00)
			iv += 0x000100;
		ic->ic_iv = iv + 1;

		*frm++ = iv & 0xff;
		*frm++ = (iv >>  8) & 0xff;
		*frm++ = (iv >> 16) & 0xff;
		*frm++ = ic->ic_wep_txkey << 6;

		m_copydata(m0, sizeof(struct ieee80211_frame),
		    m0->m_pkthdr.len - sizeof(struct ieee80211_frame), frm);

		paylen  += IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN;
		xferlen += IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN;
		totlen = xferlen + IEEE80211_WEP_CRCLEN;
	} else {
		m_copydata(m0, 0, m0->m_pkthdr.len, desc + 1);
		totlen = xferlen;
	}

	/* fill Tx descriptor */
	*(uint32_t *)data->buf = UATH_MAKECTL(1, xferlen - sizeof (uint32_t));

	desc->len    = htobe32(totlen);
	desc->priv   = sc->data_idx;	/* don't care about endianness */
	desc->paylen = htobe32(paylen);
	desc->type   = htobe32(UATH_TX_DATA);
	desc->flags  = htobe32(0);
	if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		desc->dest  = htobe32(UATH_ID_BROADCAST);
		desc->magic = htobe32(3);
	} else {
		desc->dest  = htobe32(UATH_ID_BSS);
		desc->magic = htobe32(1);
	}

	m_freem(m0);	/* mbuf is no longer needed */

#ifdef UATH_DEBUG
	if (uath_debug >= 6) {
		printf("sending frame index=%u len=%d xferlen=%d",
		    sc->data_idx, paylen, xferlen);
		uath_dump_cmd(data->buf, xferlen, '+');
	}
#endif
	usbd_setup_xfer(data->xfer, sc->data_tx_pipe, data, data->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, UATH_DATA_TIMEOUT,
	    uath_data_txeof);
	error = usbd_transfer(data->xfer);
	if (error != USBD_IN_PROGRESS && error != 0) {
		ic->ic_if.if_oerrors++;
		return error;
	}
	sc->data_idx = (sc->data_idx + 1) % UATH_TX_DATA_LIST_COUNT;
	sc->tx_queued++;

	return 0;
}

void
uath_start(struct ifnet *ifp)
{
	struct uath_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m0;

	/*
	 * net80211 may still try to send management frames even if the
	 * IFF_RUNNING flag is not set...
	 */
	if (!(ifp->if_flags & IFF_RUNNING) && ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->tx_queued >= UATH_TX_DATA_LIST_COUNT) {
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
			if (uath_tx_data(sc, m0, ni) != 0)
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
			if (uath_tx_data(sc, m0, ni) != 0) {
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
uath_watchdog(struct ifnet *ifp)
{
	struct uath_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			/*uath_init(ifp); XXX needs a process context! */
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
uath_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING))
				uath_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				uath_stop(ifp, 1);
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			uath_init(ifp);
		error = 0;
	}

	splx(s);

	return error;
}

int
uath_query_eeprom(struct uath_softc *sc)
{
	uint32_t tmp;
	int error;

	/* retrieve MAC address */
	error = uath_read_eeprom(sc, UATH_EEPROM_MACADDR, sc->sc_ic.ic_myaddr);
	if (error != 0) {
		printf("%s: could not read MAC address\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/* retrieve the maximum frame size that the hardware can receive */
	error = uath_read_eeprom(sc, UATH_EEPROM_RXBUFSZ, &tmp);
	if (error != 0) {
		printf("%s: could not read maximum Rx buffer size\n",
		    sc->sc_dev.dv_xname);
		return error;
	}
	sc->rxbufsz = betoh32(tmp) & 0xfff;
	DPRINTF(("maximum Rx buffer size %d\n", sc->rxbufsz));
	return 0;
}

int
uath_reset(struct uath_softc *sc)
{
	struct uath_cmd_setup setup;
	uint32_t reg, val;
	int s, error;

	/* init device with some voodoo incantations.. */
	setup.magic1 = htobe32(1);
	setup.magic2 = htobe32(5);
	setup.magic3 = htobe32(200);
	setup.magic4 = htobe32(27);
	s = splusb();
	error = uath_cmd_write(sc, UATH_CMD_SETUP, &setup, sizeof setup,
	    UATH_CMD_FLAG_ASYNC);
	/* ..and wait until firmware notifies us that it is ready */
	if (error == 0)
		error = tsleep_nsec(UATH_COND_INIT(sc), PCATCH, "uathinit",
		    SEC_TO_NSEC(5));
	splx(s);
	if (error != 0)
		return error;

	/* read PHY registers */
	for (reg = 0x09; reg <= 0x24; reg++) {
		if (reg == 0x0b || reg == 0x0c)
			continue;
		DELAY(100);
		if ((error = uath_read_reg(sc, reg, &val)) != 0)
			return error;
		DPRINTFN(2, ("reg 0x%02x=0x%08x\n", reg, val));
	}
	return error;
}

int
uath_reset_tx_queues(struct uath_softc *sc)
{
	int ac, error;

	for (ac = 0; ac < 4; ac++) {
		const uint32_t qid = htobe32(UATH_AC_TO_QID(ac));

		DPRINTF(("resetting Tx queue %d\n", UATH_AC_TO_QID(ac)));
		error = uath_cmd_write(sc, UATH_CMD_RESET_QUEUE, &qid,
		    sizeof qid, 0);
		if (error != 0)
			break;
	}
	return error;
}

int
uath_wme_init(struct uath_softc *sc)
{
	struct uath_qinfo qinfo;
	int ac, error;
	static const struct uath_wme_settings uath_wme_11g[4] = {
		{ 7, 4, 10,  0, 0 },	/* Background */
		{ 3, 4, 10,  0, 0 },	/* Best-Effort */
		{ 3, 3,  4, 26, 0 },	/* Video */
		{ 2, 2,  3, 47, 0 }	/* Voice */
	};

	bzero(&qinfo, sizeof qinfo);
	qinfo.size   = htobe32(32);
	qinfo.magic1 = htobe32(1);	/* XXX ack policy? */
	qinfo.magic2 = htobe32(1);
	for (ac = 0; ac < 4; ac++) {
		qinfo.qid      = htobe32(UATH_AC_TO_QID(ac));
		qinfo.ac       = htobe32(ac);
		qinfo.aifsn    = htobe32(uath_wme_11g[ac].aifsn);
		qinfo.logcwmin = htobe32(uath_wme_11g[ac].logcwmin);
		qinfo.logcwmax = htobe32(uath_wme_11g[ac].logcwmax);
		qinfo.txop     = htobe32(UATH_TXOP_TO_US(
				     uath_wme_11g[ac].txop));
		qinfo.acm      = htobe32(uath_wme_11g[ac].acm);

		DPRINTF(("setting up Tx queue %d\n", UATH_AC_TO_QID(ac)));
		error = uath_cmd_write(sc, UATH_CMD_SET_QUEUE, &qinfo,
		    sizeof qinfo, 0);
		if (error != 0)
			break;
	}
	return error;
}

int
uath_set_chan(struct uath_softc *sc, struct ieee80211_channel *c)
{
	struct uath_set_chan chan;

	bzero(&chan, sizeof chan);
	chan.flags  = htobe32(0x1400);
	chan.freq   = htobe32(c->ic_freq);
	chan.magic1 = htobe32(20);
	chan.magic2 = htobe32(50);
	chan.magic3 = htobe32(1);

	DPRINTF(("switching to channel %d\n",
	    ieee80211_chan2ieee(&sc->sc_ic, c)));
	return uath_cmd_write(sc, UATH_CMD_SET_CHAN, &chan, sizeof chan, 0);
}

int
uath_set_key(struct uath_softc *sc, const struct ieee80211_key *k, int index)
{
	struct uath_cmd_crypto crypto;
	int i;

	bzero(&crypto, sizeof crypto);
	crypto.keyidx = htobe32(index);
	crypto.magic1 = htobe32(1);
	crypto.size   = htobe32(368);
	crypto.mask   = htobe32(0xffff);
	crypto.flags  = htobe32(0x80000068);
	if (index != UATH_DEFAULT_KEY)
		crypto.flags |= htobe32(index << 16);
	memset(crypto.magic2, 0xff, sizeof crypto.magic2);

	/*
	 * Each byte of the key must be XOR'ed with 10101010 before being
	 * transmitted to the firmware.
	 */
	for (i = 0; i < k->k_len; i++)
		crypto.key[i] = k->k_key[i] ^ 0xaa;

	DPRINTF(("setting crypto key index=%d len=%d\n", index, k->k_len));
	return uath_cmd_write(sc, UATH_CMD_CRYPTO, &crypto, sizeof crypto, 0);
}

int
uath_set_keys(struct uath_softc *sc)
{
	const struct ieee80211com *ic = &sc->sc_ic;
	int i, error;

	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		const struct ieee80211_key *k = &ic->ic_nw_keys[i];

		if (k->k_len > 0 && (error = uath_set_key(sc, k, i)) != 0)
			return error;
	}
	return uath_set_key(sc, &ic->ic_nw_keys[ic->ic_wep_txkey],
	    UATH_DEFAULT_KEY);
}

int
uath_set_rates(struct uath_softc *sc, const struct ieee80211_rateset *rs)
{
	struct uath_cmd_rates rates;

	bzero(&rates, sizeof rates);
	rates.magic1 = htobe32(0x02);
	rates.size   = htobe32(1 + sizeof rates.rates);
	rates.nrates = rs->rs_nrates;
	bcopy(rs->rs_rates, rates.rates, rs->rs_nrates);

	DPRINTF(("setting supported rates nrates=%d\n", rs->rs_nrates));
	return uath_cmd_write(sc, UATH_CMD_SET_RATES, &rates, sizeof rates, 0);
}

int
uath_set_rxfilter(struct uath_softc *sc, uint32_t filter, uint32_t flags)
{
	struct uath_cmd_filter rxfilter;

	rxfilter.filter = htobe32(filter);
	rxfilter.flags  = htobe32(flags);

	DPRINTF(("setting Rx filter=0x%x flags=0x%x\n", filter, flags));
	return uath_cmd_write(sc, UATH_CMD_SET_FILTER, &rxfilter,
	    sizeof rxfilter, 0);
}

int
uath_set_led(struct uath_softc *sc, int which, int on)
{
	struct uath_cmd_led led;

	led.which = htobe32(which);
	led.state = htobe32(on ? UATH_LED_ON : UATH_LED_OFF);

	DPRINTFN(2, ("switching %s led %s\n",
	    (which == UATH_LED_LINK) ? "link" : "activity",
	    on ? "on" : "off"));
	return uath_cmd_write(sc, UATH_CMD_SET_LED, &led, sizeof led, 0);
}

int
uath_switch_channel(struct uath_softc *sc, struct ieee80211_channel *c)
{
	uint32_t val;
	int error;

	/* set radio frequency */
	if ((error = uath_set_chan(sc, c)) != 0) {
		printf("%s: could not set channel\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* reset Tx rings */
	if ((error = uath_reset_tx_queues(sc)) != 0) {
		printf("%s: could not reset Tx queues\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/* set Tx rings WME properties */
	if ((error = uath_wme_init(sc)) != 0) {
		printf("%s: could not init Tx queues\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	val = htobe32(0);
	error = uath_cmd_write(sc, UATH_CMD_SET_STATE, &val, sizeof val, 0);
	if (error != 0) {
		printf("%s: could not set state\n", sc->sc_dev.dv_xname);
		return error;
	}

	return uath_tx_null(sc);
}

int
uath_init(struct ifnet *ifp)
{
	struct uath_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct uath_cmd_31 cmd31;
	uint32_t val;
	int i, error;

	/* reset data and command rings */
	sc->tx_queued = sc->data_idx = sc->cmd_idx = 0;

	val = htobe32(0);
	(void)uath_cmd_write(sc, UATH_CMD_02, &val, sizeof val, 0);

	/* set MAC address */
	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	(void)uath_write_multi(sc, 0x13, ic->ic_myaddr, IEEE80211_ADDR_LEN);

	(void)uath_write_reg(sc, 0x02, 0x00000001);
	(void)uath_write_reg(sc, 0x0e, 0x0000003f);
	(void)uath_write_reg(sc, 0x10, 0x00000001);
	(void)uath_write_reg(sc, 0x06, 0x0000001e);

	/*
	 * Queue Rx data xfers.
	 */
	for (i = 0; i < UATH_RX_DATA_LIST_COUNT; i++) {
		struct uath_rx_data *data = &sc->rx_data[i];

		usbd_setup_xfer(data->xfer, sc->data_rx_pipe, data, data->buf,
		    sc->rxbufsz, USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT,
		    uath_data_rxeof);
		error = usbd_transfer(data->xfer);
		if (error != USBD_IN_PROGRESS && error != 0) {
			printf("%s: could not queue Rx transfer\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}

	error = uath_cmd_read(sc, UATH_CMD_07, NULL, 0, &val,
	    UATH_CMD_FLAG_MAGIC);
	if (error != 0) {
		printf("%s: could not send read command 07h\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	DPRINTF(("command 07h return code: %x\n", betoh32(val)));

	/* set default channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	if ((error = uath_set_chan(sc, ic->ic_bss->ni_chan)) != 0) {
		printf("%s: could not set channel\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	if ((error = uath_wme_init(sc)) != 0) {
		printf("%s: could not setup WME parameters\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/* init MAC registers */
	(void)uath_write_reg(sc, 0x19, 0x00000000);
	(void)uath_write_reg(sc, 0x1a, 0x0000003c);
	(void)uath_write_reg(sc, 0x1b, 0x0000003c);
	(void)uath_write_reg(sc, 0x1c, 0x00000000);
	(void)uath_write_reg(sc, 0x1e, 0x00000000);
	(void)uath_write_reg(sc, 0x1f, 0x00000003);
	(void)uath_write_reg(sc, 0x0c, 0x00000000);
	(void)uath_write_reg(sc, 0x0f, 0x00000002);
	(void)uath_write_reg(sc, 0x0a, 0x00000007);	/* XXX retry? */
	(void)uath_write_reg(sc, 0x09, ic->ic_rtsthreshold);

	val = htobe32(4);
	(void)uath_cmd_write(sc, UATH_CMD_27, &val, sizeof val, 0);
	(void)uath_cmd_write(sc, UATH_CMD_27, &val, sizeof val, 0);
	(void)uath_cmd_write(sc, UATH_CMD_1B, NULL, 0, 0);

	if ((error = uath_set_keys(sc)) != 0) {
		printf("%s: could not set crypto keys\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/* enable Rx */
	(void)uath_set_rxfilter(sc, 0x0000, 4);
	(void)uath_set_rxfilter(sc, 0x0817, 1);

	cmd31.magic1 = htobe32(0xffffffff);
	cmd31.magic2 = htobe32(0xffffffff);
	(void)uath_cmd_write(sc, UATH_CMD_31, &cmd31, sizeof cmd31, 0);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return 0;

fail:	uath_stop(ifp, 1);
	return error;
}

void
uath_stop(struct ifnet *ifp, int disable)
{
	struct uath_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t val;
	int s;

	s = splusb();

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);	/* free all nodes */

	val = htobe32(0);
	(void)uath_cmd_write(sc, UATH_CMD_SET_STATE, &val, sizeof val, 0);
	(void)uath_cmd_write(sc, UATH_CMD_RESET, NULL, 0, 0);

	val = htobe32(0);
	(void)uath_cmd_write(sc, UATH_CMD_15, &val, sizeof val, 0);

#if 0
	(void)uath_cmd_read(sc, UATH_CMD_SHUTDOWN, NULL, 0, NULL,
	    UATH_CMD_FLAG_MAGIC);
#endif

	/* abort any pending transfers */
	usbd_abort_pipe(sc->data_tx_pipe);
	usbd_abort_pipe(sc->data_rx_pipe);
	usbd_abort_pipe(sc->cmd_tx_pipe);

	splx(s);
}

/*
 * Load the MIPS R4000 microcode into the device.  Once the image is loaded,
 * the device will detach itself from the bus and reattach later with a new
 * product Id (a la ezusb).  XXX this could also be implemented in userland
 * through /dev/ugen.
 */
int
uath_loadfirmware(struct uath_softc *sc, const u_char *fw, int len)
{
	struct usbd_xfer *ctlxfer, *txxfer, *rxxfer;
	struct uath_fwblock *txblock, *rxblock;
	uint8_t *txdata;
	int error = 0;

	if ((ctlxfer = usbd_alloc_xfer(sc->sc_udev)) == NULL) {
		printf("%s: could not allocate Tx control xfer\n",
		    sc->sc_dev.dv_xname);
		error = USBD_NOMEM;
		goto fail1;
	}
	txblock = usbd_alloc_buffer(ctlxfer, sizeof (struct uath_fwblock));
	if (txblock == NULL) {
		printf("%s: could not allocate Tx control block\n",
		    sc->sc_dev.dv_xname);
		error = USBD_NOMEM;
		goto fail2;
	}

	if ((txxfer = usbd_alloc_xfer(sc->sc_udev)) == NULL) {
		printf("%s: could not allocate Tx xfer\n",
		    sc->sc_dev.dv_xname);
		error = USBD_NOMEM;
		goto fail2;
	}
	txdata = usbd_alloc_buffer(txxfer, UATH_MAX_FWBLOCK_SIZE);
	if (txdata == NULL) {
		printf("%s: could not allocate Tx buffer\n",
		    sc->sc_dev.dv_xname);
		error = USBD_NOMEM;
		goto fail3;
	}

	if ((rxxfer = usbd_alloc_xfer(sc->sc_udev)) == NULL) {
		printf("%s: could not allocate Rx control xfer\n",
		    sc->sc_dev.dv_xname);
		error = USBD_NOMEM;
		goto fail3;
	}
	rxblock = usbd_alloc_buffer(rxxfer, sizeof (struct uath_fwblock));
	if (rxblock == NULL) {
		printf("%s: could not allocate Rx control block\n",
		    sc->sc_dev.dv_xname);
		error = USBD_NOMEM;
		goto fail4;
	}

	bzero(txblock, sizeof (struct uath_fwblock));
	txblock->flags = htobe32(UATH_WRITE_BLOCK);
	txblock->total = htobe32(len);

	while (len > 0) {
		int mlen = min(len, UATH_MAX_FWBLOCK_SIZE);

		txblock->remain = htobe32(len - mlen);
		txblock->len = htobe32(mlen);

		DPRINTF(("sending firmware block: %d bytes remaining\n",
		    len - mlen));

		/* send firmware block meta-data */
		usbd_setup_xfer(ctlxfer, sc->cmd_tx_pipe, sc, txblock,
		    sizeof (struct uath_fwblock),
		    USBD_NO_COPY | USBD_SYNCHRONOUS,
		    UATH_CMD_TIMEOUT, NULL);
		if ((error = usbd_transfer(ctlxfer)) != 0) {
			printf("%s: could not send firmware block info\n",
			    sc->sc_dev.dv_xname);
			break;
		}

		/* send firmware block data */
		bcopy(fw, txdata, mlen);
		usbd_setup_xfer(txxfer, sc->data_tx_pipe, sc, txdata, mlen,
		    USBD_NO_COPY | USBD_SYNCHRONOUS, UATH_DATA_TIMEOUT, NULL);
		if ((error = usbd_transfer(txxfer)) != 0) {
			printf("%s: could not send firmware block data\n",
			    sc->sc_dev.dv_xname);
			break;
		}

		/* wait for ack from firmware */
		usbd_setup_xfer(rxxfer, sc->cmd_rx_pipe, sc, rxblock,
		    sizeof (struct uath_fwblock), USBD_SHORT_XFER_OK |
		    USBD_NO_COPY | USBD_SYNCHRONOUS, UATH_CMD_TIMEOUT, NULL);
		if ((error = usbd_transfer(rxxfer)) != 0) {
			printf("%s: could not read firmware answer\n",
			    sc->sc_dev.dv_xname);
			break;
		}

		DPRINTFN(2, ("rxblock flags=0x%x total=%d\n",
		    betoh32(rxblock->flags), betoh32(rxblock->rxtotal)));
		fw += mlen;
		len -= mlen;
	}

fail4:	usbd_free_xfer(rxxfer);
fail3:	usbd_free_xfer(txxfer);
fail2:	usbd_free_xfer(ctlxfer);
fail1:	return error;
}

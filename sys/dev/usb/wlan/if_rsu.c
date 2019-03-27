/*	$OpenBSD: if_rsu.c,v 1.17 2013/04/15 09:23:01 mglocker Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
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

/*
 * Driver for Realtek RTL8188SU/RTL8191SU/RTL8192SU.
 *
 * TODO:
 *   o tx a-mpdu
 *   o hostap / ibss / mesh
 *   o power-save operation
 */

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/firmware.h>
#include <sys/module.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"

#include <dev/rtwn/if_rtwn_ridx.h>	/* XXX */
#include <dev/usb/wlan/if_rsureg.h>

#define RSU_RATE_IS_CCK	RTWN_RATE_IS_CCK

#ifdef USB_DEBUG
static int rsu_debug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, rsu, CTLFLAG_RW, 0, "USB rsu");
SYSCTL_INT(_hw_usb_rsu, OID_AUTO, debug, CTLFLAG_RWTUN, &rsu_debug, 0,
    "Debug level");
#define	RSU_DPRINTF(_sc, _flg, ...)					\
	do								\
		if (((_flg) == (RSU_DEBUG_ANY)) || (rsu_debug & (_flg))) \
			device_printf((_sc)->sc_dev, __VA_ARGS__);	\
	while (0)
#else
#define	RSU_DPRINTF(_sc, _flg, ...)
#endif

static int rsu_enable_11n = 1;
TUNABLE_INT("hw.usb.rsu.enable_11n", &rsu_enable_11n);

#define	RSU_DEBUG_ANY		0xffffffff
#define	RSU_DEBUG_TX		0x00000001
#define	RSU_DEBUG_RX		0x00000002
#define	RSU_DEBUG_RESET		0x00000004
#define	RSU_DEBUG_CALIB		0x00000008
#define	RSU_DEBUG_STATE		0x00000010
#define	RSU_DEBUG_SCAN		0x00000020
#define	RSU_DEBUG_FWCMD		0x00000040
#define	RSU_DEBUG_TXDONE	0x00000080
#define	RSU_DEBUG_FW		0x00000100
#define	RSU_DEBUG_FWDBG		0x00000200
#define	RSU_DEBUG_AMPDU		0x00000400
#define	RSU_DEBUG_KEY		0x00000800
#define	RSU_DEBUG_USB		0x00001000

static const STRUCT_USB_HOST_ID rsu_devs[] = {
#define	RSU_HT_NOT_SUPPORTED 0
#define	RSU_HT_SUPPORTED 1
#define RSU_DEV_HT(v,p)  { USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, \
				   RSU_HT_SUPPORTED) }
#define RSU_DEV(v,p)     { USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, \
				   RSU_HT_NOT_SUPPORTED) }
	RSU_DEV(ASUS,			RTL8192SU),
	RSU_DEV(AZUREWAVE,		RTL8192SU_4),
	RSU_DEV(SITECOMEU,		WLA1000),
	RSU_DEV_HT(ACCTON,		RTL8192SU),
	RSU_DEV_HT(ASUS,		USBN10),
	RSU_DEV_HT(AZUREWAVE,		RTL8192SU_1),
	RSU_DEV_HT(AZUREWAVE,		RTL8192SU_2),
	RSU_DEV_HT(AZUREWAVE,		RTL8192SU_3),
	RSU_DEV_HT(AZUREWAVE,		RTL8192SU_5),
	RSU_DEV_HT(BELKIN,		RTL8192SU_1),
	RSU_DEV_HT(BELKIN,		RTL8192SU_2),
	RSU_DEV_HT(BELKIN,		RTL8192SU_3),
	RSU_DEV_HT(CONCEPTRONIC2,	RTL8192SU_1),
	RSU_DEV_HT(CONCEPTRONIC2,	RTL8192SU_2),
	RSU_DEV_HT(CONCEPTRONIC2,	RTL8192SU_3),
	RSU_DEV_HT(COREGA,		RTL8192SU),
	RSU_DEV_HT(DLINK2,		DWA131A1),
	RSU_DEV_HT(DLINK2,		RTL8192SU_1),
	RSU_DEV_HT(DLINK2,		RTL8192SU_2),
	RSU_DEV_HT(EDIMAX,		RTL8192SU_1),
	RSU_DEV_HT(EDIMAX,		RTL8192SU_2),
	RSU_DEV_HT(EDIMAX,		EW7622UMN),
	RSU_DEV_HT(GUILLEMOT,		HWGUN54),
	RSU_DEV_HT(GUILLEMOT,		HWNUM300),
	RSU_DEV_HT(HAWKING,		RTL8192SU_1),
	RSU_DEV_HT(HAWKING,		RTL8192SU_2),
	RSU_DEV_HT(PLANEX2,		GWUSNANO),
	RSU_DEV_HT(REALTEK,		RTL8171),
	RSU_DEV_HT(REALTEK,		RTL8172),
	RSU_DEV_HT(REALTEK,		RTL8173),
	RSU_DEV_HT(REALTEK,		RTL8174),
	RSU_DEV_HT(REALTEK,		RTL8192SU),
	RSU_DEV_HT(REALTEK,		RTL8712),
	RSU_DEV_HT(REALTEK,		RTL8713),
	RSU_DEV_HT(SENAO,		RTL8192SU_1),
	RSU_DEV_HT(SENAO,		RTL8192SU_2),
	RSU_DEV_HT(SITECOMEU,		WL349V1),
	RSU_DEV_HT(SITECOMEU,		WL353),
	RSU_DEV_HT(SWEEX2,		LW154),
	RSU_DEV_HT(TRENDNET,		TEW646UBH),
#undef RSU_DEV_HT
#undef RSU_DEV
};

static device_probe_t   rsu_match;
static device_attach_t  rsu_attach;
static device_detach_t  rsu_detach;
static usb_callback_t   rsu_bulk_tx_callback_be_bk;
static usb_callback_t   rsu_bulk_tx_callback_vi_vo;
static usb_callback_t   rsu_bulk_tx_callback_h2c;
static usb_callback_t   rsu_bulk_rx_callback;
static usb_error_t	rsu_do_request(struct rsu_softc *,
			    struct usb_device_request *, void *);
static struct ieee80211vap *
		rsu_vap_create(struct ieee80211com *, const char name[],
		    int, enum ieee80211_opmode, int, const uint8_t bssid[],
		    const uint8_t mac[]);
static void	rsu_vap_delete(struct ieee80211vap *);
static void	rsu_scan_start(struct ieee80211com *);
static void	rsu_scan_end(struct ieee80211com *);
static void	rsu_getradiocaps(struct ieee80211com *, int, int *,
		    struct ieee80211_channel[]);
static void	rsu_set_channel(struct ieee80211com *);
static void	rsu_scan_curchan(struct ieee80211_scan_state *, unsigned long);
static void	rsu_scan_mindwell(struct ieee80211_scan_state *);
static void	rsu_update_promisc(struct ieee80211com *);
static uint8_t	rsu_get_multi_pos(const uint8_t[]);
static void	rsu_set_multi(struct rsu_softc *);
static void	rsu_update_mcast(struct ieee80211com *);
static int	rsu_alloc_rx_list(struct rsu_softc *);
static void	rsu_free_rx_list(struct rsu_softc *);
static int	rsu_alloc_tx_list(struct rsu_softc *);
static void	rsu_free_tx_list(struct rsu_softc *);
static void	rsu_free_list(struct rsu_softc *, struct rsu_data [], int);
static struct rsu_data *_rsu_getbuf(struct rsu_softc *);
static struct rsu_data *rsu_getbuf(struct rsu_softc *);
static void	rsu_freebuf(struct rsu_softc *, struct rsu_data *);
static int	rsu_write_region_1(struct rsu_softc *, uint16_t, uint8_t *,
		    int);
static void	rsu_write_1(struct rsu_softc *, uint16_t, uint8_t);
static void	rsu_write_2(struct rsu_softc *, uint16_t, uint16_t);
static void	rsu_write_4(struct rsu_softc *, uint16_t, uint32_t);
static int	rsu_read_region_1(struct rsu_softc *, uint16_t, uint8_t *,
		    int);
static uint8_t	rsu_read_1(struct rsu_softc *, uint16_t);
static uint16_t	rsu_read_2(struct rsu_softc *, uint16_t);
static uint32_t	rsu_read_4(struct rsu_softc *, uint16_t);
static int	rsu_fw_iocmd(struct rsu_softc *, uint32_t);
static uint8_t	rsu_efuse_read_1(struct rsu_softc *, uint16_t);
static int	rsu_read_rom(struct rsu_softc *);
static int	rsu_fw_cmd(struct rsu_softc *, uint8_t, void *, int);
static void	rsu_calib_task(void *, int);
static void	rsu_tx_task(void *, int);
static void	rsu_set_led(struct rsu_softc *, int);
static int	rsu_monitor_newstate(struct ieee80211vap *,
		    enum ieee80211_state, int);
static int	rsu_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static int	rsu_key_alloc(struct ieee80211vap *, struct ieee80211_key *,
		    ieee80211_keyix *, ieee80211_keyix *);
static int	rsu_process_key(struct ieee80211vap *,
		    const struct ieee80211_key *, int);
static int	rsu_key_set(struct ieee80211vap *,
		    const struct ieee80211_key *);
static int	rsu_key_delete(struct ieee80211vap *,
		    const struct ieee80211_key *);
static int	rsu_cam_read(struct rsu_softc *, uint8_t, uint32_t *);
static void	rsu_cam_write(struct rsu_softc *, uint8_t, uint32_t);
static int	rsu_key_check(struct rsu_softc *, ieee80211_keyix, int);
static uint8_t	rsu_crypto_mode(struct rsu_softc *, u_int, int);
static int	rsu_set_key_group(struct rsu_softc *,
		    const struct ieee80211_key *);
static int	rsu_set_key_pair(struct rsu_softc *,
		    const struct ieee80211_key *);
static int	rsu_reinit_static_keys(struct rsu_softc *);
static int	rsu_delete_key(struct rsu_softc *sc, ieee80211_keyix);
static void	rsu_delete_key_pair_cb(void *, int);
static int	rsu_site_survey(struct rsu_softc *,
		    struct ieee80211_scan_ssid *);
static int	rsu_join_bss(struct rsu_softc *, struct ieee80211_node *);
static int	rsu_disconnect(struct rsu_softc *);
static int	rsu_hwrssi_to_rssi(struct rsu_softc *, int hw_rssi);
static void	rsu_event_survey(struct rsu_softc *, uint8_t *, int);
static void	rsu_event_join_bss(struct rsu_softc *, uint8_t *, int);
static void	rsu_rx_event(struct rsu_softc *, uint8_t, uint8_t *, int);
static void	rsu_rx_multi_event(struct rsu_softc *, uint8_t *, int);
static int8_t	rsu_get_rssi(struct rsu_softc *, int, void *);
static struct mbuf * rsu_rx_copy_to_mbuf(struct rsu_softc *,
		    struct r92s_rx_stat *, int);
static uint32_t	rsu_get_tsf_low(struct rsu_softc *);
static uint32_t	rsu_get_tsf_high(struct rsu_softc *);
static struct ieee80211_node * rsu_rx_frame(struct rsu_softc *, struct mbuf *);
static struct mbuf * rsu_rx_multi_frame(struct rsu_softc *, uint8_t *, int);
static struct mbuf *
		rsu_rxeof(struct usb_xfer *, struct rsu_data *);
static void	rsu_txeof(struct usb_xfer *, struct rsu_data *);
static int	rsu_raw_xmit(struct ieee80211_node *, struct mbuf *, 
		    const struct ieee80211_bpf_params *);
static void	rsu_rxfilter_init(struct rsu_softc *);
static void	rsu_rxfilter_set(struct rsu_softc *, uint32_t, uint32_t);
static void	rsu_rxfilter_refresh(struct rsu_softc *);
static int	rsu_init(struct rsu_softc *);
static int	rsu_tx_start(struct rsu_softc *, struct ieee80211_node *, 
		    struct mbuf *, struct rsu_data *);
static int	rsu_transmit(struct ieee80211com *, struct mbuf *);
static void	rsu_start(struct rsu_softc *);
static void	_rsu_start(struct rsu_softc *);
static int	rsu_ioctl_net(struct ieee80211com *, u_long, void *);
static void	rsu_parent(struct ieee80211com *);
static void	rsu_stop(struct rsu_softc *);
static void	rsu_ms_delay(struct rsu_softc *, int);

static device_method_t rsu_methods[] = {
	DEVMETHOD(device_probe,		rsu_match),
	DEVMETHOD(device_attach,	rsu_attach),
	DEVMETHOD(device_detach,	rsu_detach),

	DEVMETHOD_END
};

static driver_t rsu_driver = {
	.name = "rsu",
	.methods = rsu_methods,
	.size = sizeof(struct rsu_softc)
};

static devclass_t rsu_devclass;

DRIVER_MODULE(rsu, uhub, rsu_driver, rsu_devclass, NULL, 0);
MODULE_DEPEND(rsu, wlan, 1, 1, 1);
MODULE_DEPEND(rsu, usb, 1, 1, 1);
MODULE_DEPEND(rsu, firmware, 1, 1, 1);
MODULE_VERSION(rsu, 1);
USB_PNP_HOST_INFO(rsu_devs);

static uint8_t rsu_wme_ac_xfer_map[4] = {
	[WME_AC_BE] = RSU_BULK_TX_BE_BK,
	[WME_AC_BK] = RSU_BULK_TX_BE_BK,
	[WME_AC_VI] = RSU_BULK_TX_VI_VO,
	[WME_AC_VO] = RSU_BULK_TX_VI_VO,
};

/* XXX hard-coded */
#define	RSU_H2C_ENDPOINT	3

static const struct usb_config rsu_config[RSU_N_TRANSFER] = {
	[RSU_BULK_RX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = RSU_RXBUFSZ,
		.flags = {
			.pipe_bof = 1,
			.short_xfer_ok = 1
		},
		.callback = rsu_bulk_rx_callback
	},
	[RSU_BULK_TX_BE_BK] = {
		.type = UE_BULK,
		.endpoint = 0x06,
		.direction = UE_DIR_OUT,
		.bufsize = RSU_TXBUFSZ,
		.flags = {
			.ext_buffer = 1,
			.pipe_bof = 1,
			.force_short_xfer = 1
		},
		.callback = rsu_bulk_tx_callback_be_bk,
		.timeout = RSU_TX_TIMEOUT
	},
	[RSU_BULK_TX_VI_VO] = {
		.type = UE_BULK,
		.endpoint = 0x04,
		.direction = UE_DIR_OUT,
		.bufsize = RSU_TXBUFSZ,
		.flags = {
			.ext_buffer = 1,
			.pipe_bof = 1,
			.force_short_xfer = 1
		},
		.callback = rsu_bulk_tx_callback_vi_vo,
		.timeout = RSU_TX_TIMEOUT
	},
	[RSU_BULK_TX_H2C] = {
		.type = UE_BULK,
		.endpoint = 0x0d,
		.direction = UE_DIR_OUT,
		.bufsize = RSU_TXBUFSZ,
		.flags = {
			.ext_buffer = 1,
			.pipe_bof = 1,
			.short_xfer_ok = 1
		},
		.callback = rsu_bulk_tx_callback_h2c,
		.timeout = RSU_TX_TIMEOUT
	},
};

static int
rsu_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->usb_mode != USB_MODE_HOST ||
	    uaa->info.bIfaceIndex != 0 ||
	    uaa->info.bConfigIndex != 0)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(rsu_devs, sizeof(rsu_devs), uaa));
}

static int
rsu_send_mgmt(struct ieee80211_node *ni, int type, int arg)
{

	return (ENOTSUP);
}

static void
rsu_update_chw(struct ieee80211com *ic)
{

}

/*
 * notification from net80211 that it'd like to do A-MPDU on the given TID.
 *
 * Note: this actually hangs traffic at the present moment, so don't use it.
 * The firmware debug does indiciate it's sending and establishing a TX AMPDU
 * session, but then no traffic flows.
 */
static int
rsu_ampdu_enable(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap)
{
#if 0
	struct rsu_softc *sc = ni->ni_ic->ic_softc;
	struct r92s_add_ba_req req;

	/* Don't enable if it's requested or running */
	if (IEEE80211_AMPDU_REQUESTED(tap))
		return (0);
	if (IEEE80211_AMPDU_RUNNING(tap))
		return (0);

	/* We've decided to send addba; so send it */
	req.tid = htole32(tap->txa_tid);

	/* Attempt net80211 state */
	if (ieee80211_ampdu_tx_request_ext(ni, tap->txa_tid) != 1)
		return (0);

	/* Send the firmware command */
	RSU_DPRINTF(sc, RSU_DEBUG_AMPDU, "%s: establishing AMPDU TX for TID %d\n",
	    __func__,
	    tap->txa_tid);

	RSU_LOCK(sc);
	if (rsu_fw_cmd(sc, R92S_CMD_ADDBA_REQ, &req, sizeof(req)) != 1) {
		RSU_UNLOCK(sc);
		/* Mark failure */
		(void) ieee80211_ampdu_tx_request_active_ext(ni, tap->txa_tid, 0);
		return (0);
	}
	RSU_UNLOCK(sc);

	/* Mark success; we don't get any further notifications */
	(void) ieee80211_ampdu_tx_request_active_ext(ni, tap->txa_tid, 1);
#endif
	/* Return 0, we're driving this ourselves */
	return (0);
}

static int
rsu_wme_update(struct ieee80211com *ic)
{

	/* Firmware handles this; not our problem */
	return (0);
}

static int
rsu_attach(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct rsu_softc *sc = device_get_softc(self);
	struct ieee80211com *ic = &sc->sc_ic;
	int error;
	uint8_t iface_index;
	struct usb_interface *iface;
	const char *rft;

	device_set_usb_desc(self);
	sc->sc_udev = uaa->device;
	sc->sc_dev = self;
	sc->sc_rx_checksum_enable = 1;
	if (rsu_enable_11n)
		sc->sc_ht = !! (USB_GET_DRIVER_INFO(uaa) & RSU_HT_SUPPORTED);

	/* Get number of endpoints */
	iface = usbd_get_iface(sc->sc_udev, 0);
	sc->sc_nendpoints = iface->idesc->bNumEndpoints;

	/* Endpoints are hard-coded for now, so enforce 4-endpoint only */
	if (sc->sc_nendpoints != 4) {
		device_printf(sc->sc_dev,
		    "the driver currently only supports 4-endpoint devices\n");
		return (ENXIO);
	}

	mtx_init(&sc->sc_mtx, device_get_nameunit(self), MTX_NETWORK_LOCK,
	    MTX_DEF);
	RSU_DELKEY_BMAP_LOCK_INIT(sc);
	TIMEOUT_TASK_INIT(taskqueue_thread, &sc->calib_task, 0, 
	    rsu_calib_task, sc);
	TASK_INIT(&sc->del_key_task, 0, rsu_delete_key_pair_cb, sc);
	TASK_INIT(&sc->tx_task, 0, rsu_tx_task, sc);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	/* Allocate Tx/Rx buffers. */
	error = rsu_alloc_rx_list(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate Rx buffers\n");
		goto fail_usb;
	}

	error = rsu_alloc_tx_list(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate Tx buffers\n");
		rsu_free_rx_list(sc);
		goto fail_usb;
	}

	iface_index = 0;
	error = usbd_transfer_setup(uaa->device, &iface_index, sc->sc_xfer,
	    rsu_config, RSU_N_TRANSFER, sc, &sc->sc_mtx);
	if (error) {
		device_printf(sc->sc_dev,
		    "could not allocate USB transfers, err=%s\n", 
		    usbd_errstr(error));
		goto fail_usb;
	}
	RSU_LOCK(sc);
	/* Read chip revision. */
	sc->cut = MS(rsu_read_4(sc, R92S_PMC_FSM), R92S_PMC_FSM_CUT);
	if (sc->cut != 3)
		sc->cut = (sc->cut >> 1) + 1;
	error = rsu_read_rom(sc);
	RSU_UNLOCK(sc);
	if (error != 0) {
		device_printf(self, "could not read ROM\n");
		goto fail_rom;
	}

	/* Figure out TX/RX streams */
	switch (sc->rom[84]) {
	case 0x0:
		sc->sc_rftype = RTL8712_RFCONFIG_1T1R;
		sc->sc_nrxstream = 1;
		sc->sc_ntxstream = 1;
		rft = "1T1R";
		break;
	case 0x1:
		sc->sc_rftype = RTL8712_RFCONFIG_1T2R;
		sc->sc_nrxstream = 2;
		sc->sc_ntxstream = 1;
		rft = "1T2R";
		break;
	case 0x2:
		sc->sc_rftype = RTL8712_RFCONFIG_2T2R;
		sc->sc_nrxstream = 2;
		sc->sc_ntxstream = 2;
		rft = "2T2R";
		break;
	case 0x3:	/* "green" NIC */
		sc->sc_rftype = RTL8712_RFCONFIG_1T2R;
		sc->sc_nrxstream = 2;
		sc->sc_ntxstream = 1;
		rft = "1T2R ('green')";
		break;
	default:
		device_printf(sc->sc_dev,
		    "%s: unknown board type (rfconfig=0x%02x)\n",
		    __func__,
		    sc->rom[84]);
		goto fail_rom;
	}

	IEEE80211_ADDR_COPY(ic->ic_macaddr, &sc->rom[0x12]);
	device_printf(self, "MAC/BB RTL8712 cut %d %s\n", sc->cut, rft);

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(self);
	ic->ic_phytype = IEEE80211_T_OFDM;	/* Not only, but not used. */
	ic->ic_opmode = IEEE80211_M_STA;	/* Default to BSS mode. */

	/* Set device capabilities. */
	ic->ic_caps =
	    IEEE80211_C_STA |		/* station mode */
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
#if 0
	    IEEE80211_C_BGSCAN |	/* Background scan. */
#endif
	    IEEE80211_C_SHPREAMBLE |	/* Short preamble supported. */
	    IEEE80211_C_WME |		/* WME/QoS */
	    IEEE80211_C_SHSLOT |	/* Short slot time supported. */
	    IEEE80211_C_WPA;		/* WPA/RSN. */

	ic->ic_cryptocaps =
	    IEEE80211_CRYPTO_WEP |
	    IEEE80211_CRYPTO_TKIP |
	    IEEE80211_CRYPTO_AES_CCM;

	/* Check if HT support is present. */
	if (sc->sc_ht) {
		device_printf(sc->sc_dev, "%s: enabling 11n\n", __func__);

		/* Enable basic HT */
		ic->ic_htcaps = IEEE80211_HTC_HT |
#if 0
		    IEEE80211_HTC_AMPDU |
#endif
		    IEEE80211_HTC_AMSDU |
		    IEEE80211_HTCAP_MAXAMSDU_3839 |
		    IEEE80211_HTCAP_SMPS_OFF;
		ic->ic_htcaps |= IEEE80211_HTCAP_CHWIDTH40;

		/* set number of spatial streams */
		ic->ic_txstream = sc->sc_ntxstream;
		ic->ic_rxstream = sc->sc_nrxstream;
	}
	ic->ic_flags_ext |= IEEE80211_FEXT_SCAN_OFFLOAD;

	rsu_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	ieee80211_ifattach(ic);
	ic->ic_raw_xmit = rsu_raw_xmit;
	ic->ic_scan_start = rsu_scan_start;
	ic->ic_scan_end = rsu_scan_end;
	ic->ic_getradiocaps = rsu_getradiocaps;
	ic->ic_set_channel = rsu_set_channel;
	ic->ic_scan_curchan = rsu_scan_curchan;
	ic->ic_scan_mindwell = rsu_scan_mindwell;
	ic->ic_vap_create = rsu_vap_create;
	ic->ic_vap_delete = rsu_vap_delete;
	ic->ic_update_promisc = rsu_update_promisc;
	ic->ic_update_mcast = rsu_update_mcast;
	ic->ic_ioctl = rsu_ioctl_net;
	ic->ic_parent = rsu_parent;
	ic->ic_transmit = rsu_transmit;
	ic->ic_send_mgmt = rsu_send_mgmt;
	ic->ic_update_chw = rsu_update_chw;
	ic->ic_ampdu_enable = rsu_ampdu_enable;
	ic->ic_wme.wme_update = rsu_wme_update;

	ieee80211_radiotap_attach(ic, &sc->sc_txtap.wt_ihdr,
	    sizeof(sc->sc_txtap), RSU_TX_RADIOTAP_PRESENT, 
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
	    RSU_RX_RADIOTAP_PRESENT);

	if (bootverbose)
		ieee80211_announce(ic);

	return (0);

fail_rom:
	usbd_transfer_unsetup(sc->sc_xfer, RSU_N_TRANSFER);
fail_usb:
	mtx_destroy(&sc->sc_mtx);
	return (ENXIO);
}

static int
rsu_detach(device_t self)
{
	struct rsu_softc *sc = device_get_softc(self);
	struct ieee80211com *ic = &sc->sc_ic;

	rsu_stop(sc);

	usbd_transfer_unsetup(sc->sc_xfer, RSU_N_TRANSFER);

	/*
	 * Free buffers /before/ we detach from net80211, else node
	 * references to destroyed vaps will lead to a panic.
	 */
	/* Free Tx/Rx buffers. */
	RSU_LOCK(sc);
	rsu_free_tx_list(sc);
	rsu_free_rx_list(sc);
	RSU_UNLOCK(sc);

	/* Frames are freed; detach from net80211 */
	ieee80211_ifdetach(ic);

	taskqueue_drain_timeout(taskqueue_thread, &sc->calib_task);
	taskqueue_drain(taskqueue_thread, &sc->del_key_task);
	taskqueue_drain(taskqueue_thread, &sc->tx_task);

	RSU_DELKEY_BMAP_LOCK_DESTROY(sc);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static usb_error_t
rsu_do_request(struct rsu_softc *sc, struct usb_device_request *req,
    void *data)
{
	usb_error_t err;
	int ntries = 10;
	
	RSU_ASSERT_LOCKED(sc);

	while (ntries--) {
		err = usbd_do_request_flags(sc->sc_udev, &sc->sc_mtx,
		    req, data, 0, NULL, 250 /* ms */);
		if (err == 0 || err == USB_ERR_NOT_CONFIGURED)
			break;
		RSU_DPRINTF(sc, RSU_DEBUG_USB,
		    "Control request failed, %s (retries left: %d)\n",
		    usbd_errstr(err), ntries);
		rsu_ms_delay(sc, 10);
        }

        return (err);
}

static struct ieee80211vap *
rsu_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct rsu_softc *sc = ic->ic_softc;
	struct rsu_vap *uvp;
	struct ieee80211vap *vap;
	struct ifnet *ifp;

	if (!TAILQ_EMPTY(&ic->ic_vaps))         /* only one at a time */
		return (NULL);

	uvp =  malloc(sizeof(struct rsu_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &uvp->vap;

	if (ieee80211_vap_setup(ic, vap, name, unit, opmode,
	    flags, bssid) != 0) {
		/* out of memory */
		free(uvp, M_80211_VAP);
		return (NULL);
	}

	ifp = vap->iv_ifp;
	ifp->if_capabilities = IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6;
	RSU_LOCK(sc);
	if (sc->sc_rx_checksum_enable)
		ifp->if_capenable |= IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6;
	RSU_UNLOCK(sc);

	/* override state transition machine */
	uvp->newstate = vap->iv_newstate;
	if (opmode == IEEE80211_M_MONITOR)
		vap->iv_newstate = rsu_monitor_newstate;
	else
		vap->iv_newstate = rsu_newstate;
	vap->iv_key_alloc = rsu_key_alloc;
	vap->iv_key_set = rsu_key_set;
	vap->iv_key_delete = rsu_key_delete;

	/* Limits from the r92su driver */
	vap->iv_ampdu_density = IEEE80211_HTCAP_MPDUDENSITY_16;
	vap->iv_ampdu_rxmax = IEEE80211_HTCAP_MAXRXAMPDU_32K;

	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change,
	    ieee80211_media_status, mac);
	ic->ic_opmode = opmode;

	return (vap);
}

static void
rsu_vap_delete(struct ieee80211vap *vap)
{
	struct rsu_vap *uvp = RSU_VAP(vap);

	ieee80211_vap_detach(vap);
	free(uvp, M_80211_VAP);
}

static void
rsu_scan_start(struct ieee80211com *ic)
{
	struct rsu_softc *sc = ic->ic_softc;
	struct ieee80211_scan_state *ss = ic->ic_scan;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	int error;

	/* Scanning is done by the firmware. */
	RSU_LOCK(sc);
	sc->sc_active_scan = !!(ss->ss_flags & IEEE80211_SCAN_ACTIVE);
	/* XXX TODO: force awake if in network-sleep? */
	error = rsu_site_survey(sc, ss->ss_nssid > 0 ? &ss->ss_ssid[0] : NULL);
	RSU_UNLOCK(sc);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not send site survey command\n");
		ieee80211_cancel_scan(vap);
	}
}

static void
rsu_scan_end(struct ieee80211com *ic)
{
	/* Nothing to do here. */
}

static void
rsu_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct rsu_softc *sc = ic->ic_softc;
	uint8_t bands[IEEE80211_MODE_BYTES];

	/* Set supported .11b and .11g rates. */
	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	if (sc->sc_ht)
		setbit(bands, IEEE80211_MODE_11NG);
	ieee80211_add_channels_default_2ghz(chans, maxchans, nchans,
	    bands, (ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40) != 0);
}

static void
rsu_set_channel(struct ieee80211com *ic)
{
	struct rsu_softc *sc = ic->ic_softc;

	/*
	 * Only need to set the channel in Monitor mode. AP scanning and auth
	 * are already taken care of by their respective firmware commands.
	 */	
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		struct r92s_set_channel cmd;
		int error;

		cmd.channel = IEEE80211_CHAN2IEEE(ic->ic_curchan);

		RSU_LOCK(sc);
		error = rsu_fw_cmd(sc, R92S_CMD_SET_CHANNEL, &cmd,
		    sizeof(cmd));
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: error %d setting channel\n", __func__,
			    error);
		}
		RSU_UNLOCK(sc);
	}
}

static void
rsu_scan_curchan(struct ieee80211_scan_state *ss, unsigned long maxdwell)
{
	/* Scan is done in rsu_scan_start(). */
}

/**
 * Called by the net80211 framework to indicate
 * the minimum dwell time has been met, terminate the scan.
 * We don't actually terminate the scan as the firmware will notify
 * us when it's finished and we have no way to interrupt it.
 */
static void
rsu_scan_mindwell(struct ieee80211_scan_state *ss)
{
	/* NB: don't try to abort scan; wait for firmware to finish */
}

static void
rsu_update_promisc(struct ieee80211com *ic)
{
	struct rsu_softc *sc = ic->ic_softc;

	RSU_LOCK(sc);
	if (sc->sc_running)
		rsu_rxfilter_refresh(sc);
	RSU_UNLOCK(sc);
}

/*
 * The same as rtwn_get_multi_pos() / rtwn_set_multi().
 */
static uint8_t
rsu_get_multi_pos(const uint8_t maddr[])
{
	uint64_t mask = 0x00004d101df481b4;
	uint8_t pos = 0x27;	/* initial value */
	int i, j;

	for (i = 0; i < IEEE80211_ADDR_LEN; i++)
		for (j = (i == 0) ? 1 : 0; j < 8; j++)
			if ((maddr[i] >> j) & 1)
				pos ^= (mask >> (i * 8 + j - 1));

	pos &= 0x3f;

	return (pos);
}

static void
rsu_set_multi(struct rsu_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t mfilt[2];

	RSU_ASSERT_LOCKED(sc);

	/* general structure was copied from ath(4). */
	if (ic->ic_allmulti == 0) {
		struct ieee80211vap *vap;
		struct ifnet *ifp;
		struct ifmultiaddr *ifma;

		/*
		 * Merge multicast addresses to form the hardware filter.
		 */
		mfilt[0] = mfilt[1] = 0;
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
			ifp = vap->iv_ifp;
			if_maddr_rlock(ifp);
			CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
				caddr_t dl;
				uint8_t pos;

				dl = LLADDR((struct sockaddr_dl *)
				    ifma->ifma_addr);
				pos = rsu_get_multi_pos(dl);

				mfilt[pos / 32] |= (1 << (pos % 32));
			}
			if_maddr_runlock(ifp);
		}
	} else
		mfilt[0] = mfilt[1] = ~0;

	rsu_write_4(sc, R92S_MAR + 0, mfilt[0]);
	rsu_write_4(sc, R92S_MAR + 4, mfilt[1]);

	RSU_DPRINTF(sc, RSU_DEBUG_STATE, "%s: MC filter %08x:%08x\n",
	    __func__, mfilt[0], mfilt[1]);
}

static void
rsu_update_mcast(struct ieee80211com *ic)
{
	struct rsu_softc *sc = ic->ic_softc;

	RSU_LOCK(sc);
	if (sc->sc_running)
		rsu_set_multi(sc);
	RSU_UNLOCK(sc);
}

static int
rsu_alloc_list(struct rsu_softc *sc, struct rsu_data data[],
    int ndata, int maxsz)
{
	int i, error;

	for (i = 0; i < ndata; i++) {
		struct rsu_data *dp = &data[i];
		dp->sc = sc;
		dp->m = NULL;
		dp->buf = malloc(maxsz, M_USBDEV, M_NOWAIT);
		if (dp->buf == NULL) {
			device_printf(sc->sc_dev,
			    "could not allocate buffer\n");
			error = ENOMEM;
			goto fail;
		}
		dp->ni = NULL;
	}

	return (0);
fail:
	rsu_free_list(sc, data, ndata);
	return (error);
}

static int
rsu_alloc_rx_list(struct rsu_softc *sc)
{
        int error, i;

	error = rsu_alloc_list(sc, sc->sc_rx, RSU_RX_LIST_COUNT,
	    RSU_RXBUFSZ);
	if (error != 0)
		return (error);

	STAILQ_INIT(&sc->sc_rx_active);
	STAILQ_INIT(&sc->sc_rx_inactive);

	for (i = 0; i < RSU_RX_LIST_COUNT; i++)
		STAILQ_INSERT_HEAD(&sc->sc_rx_inactive, &sc->sc_rx[i], next);

	return (0);
}

static int
rsu_alloc_tx_list(struct rsu_softc *sc)
{
	int error, i;

	error = rsu_alloc_list(sc, sc->sc_tx, RSU_TX_LIST_COUNT,
	    RSU_TXBUFSZ);
	if (error != 0)
		return (error);

	STAILQ_INIT(&sc->sc_tx_inactive);

	for (i = 0; i != RSU_N_TRANSFER; i++) {
		STAILQ_INIT(&sc->sc_tx_active[i]);
		STAILQ_INIT(&sc->sc_tx_pending[i]);
	}

	for (i = 0; i < RSU_TX_LIST_COUNT; i++) {
		STAILQ_INSERT_HEAD(&sc->sc_tx_inactive, &sc->sc_tx[i], next);
	}

	return (0);
}

static void
rsu_free_tx_list(struct rsu_softc *sc)
{
	int i;

	/* prevent further allocations from TX list(s) */
	STAILQ_INIT(&sc->sc_tx_inactive);

	for (i = 0; i != RSU_N_TRANSFER; i++) {
		STAILQ_INIT(&sc->sc_tx_active[i]);
		STAILQ_INIT(&sc->sc_tx_pending[i]);
	}

	rsu_free_list(sc, sc->sc_tx, RSU_TX_LIST_COUNT);
}

static void
rsu_free_rx_list(struct rsu_softc *sc)
{
	/* prevent further allocations from RX list(s) */
	STAILQ_INIT(&sc->sc_rx_inactive);
	STAILQ_INIT(&sc->sc_rx_active);

	rsu_free_list(sc, sc->sc_rx, RSU_RX_LIST_COUNT);
}

static void
rsu_free_list(struct rsu_softc *sc, struct rsu_data data[], int ndata)
{
	int i;

	for (i = 0; i < ndata; i++) {
		struct rsu_data *dp = &data[i];

		if (dp->buf != NULL) {
			free(dp->buf, M_USBDEV);
			dp->buf = NULL;
		}
		if (dp->ni != NULL) {
			ieee80211_free_node(dp->ni);
			dp->ni = NULL;
		}
	}
}

static struct rsu_data *
_rsu_getbuf(struct rsu_softc *sc)
{
	struct rsu_data *bf;

	bf = STAILQ_FIRST(&sc->sc_tx_inactive);
	if (bf != NULL)
		STAILQ_REMOVE_HEAD(&sc->sc_tx_inactive, next);
	else
		bf = NULL;
	return (bf);
}

static struct rsu_data *
rsu_getbuf(struct rsu_softc *sc)
{
	struct rsu_data *bf;

	RSU_ASSERT_LOCKED(sc);

	bf = _rsu_getbuf(sc);
	if (bf == NULL) {
		RSU_DPRINTF(sc, RSU_DEBUG_TX, "%s: no buffers\n", __func__);
	}
	return (bf);
}

static void
rsu_freebuf(struct rsu_softc *sc, struct rsu_data *bf)
{

	RSU_ASSERT_LOCKED(sc);
	STAILQ_INSERT_TAIL(&sc->sc_tx_inactive, bf, next);
}

static int
rsu_write_region_1(struct rsu_softc *sc, uint16_t addr, uint8_t *buf,
    int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = R92S_REQ_REGS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);

	return (rsu_do_request(sc, &req, buf));
}

static void
rsu_write_1(struct rsu_softc *sc, uint16_t addr, uint8_t val)
{
	rsu_write_region_1(sc, addr, &val, 1);
}

static void
rsu_write_2(struct rsu_softc *sc, uint16_t addr, uint16_t val)
{
	val = htole16(val);
	rsu_write_region_1(sc, addr, (uint8_t *)&val, 2);
}

static void
rsu_write_4(struct rsu_softc *sc, uint16_t addr, uint32_t val)
{
	val = htole32(val);
	rsu_write_region_1(sc, addr, (uint8_t *)&val, 4);
}

static int
rsu_read_region_1(struct rsu_softc *sc, uint16_t addr, uint8_t *buf,
    int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = R92S_REQ_REGS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);

	return (rsu_do_request(sc, &req, buf));
}

static uint8_t
rsu_read_1(struct rsu_softc *sc, uint16_t addr)
{
	uint8_t val;

	if (rsu_read_region_1(sc, addr, &val, 1) != 0)
		return (0xff);
	return (val);
}

static uint16_t
rsu_read_2(struct rsu_softc *sc, uint16_t addr)
{
	uint16_t val;

	if (rsu_read_region_1(sc, addr, (uint8_t *)&val, 2) != 0)
		return (0xffff);
	return (le16toh(val));
}

static uint32_t
rsu_read_4(struct rsu_softc *sc, uint16_t addr)
{
	uint32_t val;

	if (rsu_read_region_1(sc, addr, (uint8_t *)&val, 4) != 0)
		return (0xffffffff);
	return (le32toh(val));
}

static int
rsu_fw_iocmd(struct rsu_softc *sc, uint32_t iocmd)
{
	int ntries;

	rsu_write_4(sc, R92S_IOCMD_CTRL, iocmd);
	rsu_ms_delay(sc, 1);
	for (ntries = 0; ntries < 50; ntries++) {
		if (rsu_read_4(sc, R92S_IOCMD_CTRL) == 0)
			return (0);
		rsu_ms_delay(sc, 1);
	}
	return (ETIMEDOUT);
}

static uint8_t
rsu_efuse_read_1(struct rsu_softc *sc, uint16_t addr)
{
	uint32_t reg;
	int ntries;

	reg = rsu_read_4(sc, R92S_EFUSE_CTRL);
	reg = RW(reg, R92S_EFUSE_CTRL_ADDR, addr);
	reg &= ~R92S_EFUSE_CTRL_VALID;
	rsu_write_4(sc, R92S_EFUSE_CTRL, reg);
	/* Wait for read operation to complete. */
	for (ntries = 0; ntries < 100; ntries++) {
		reg = rsu_read_4(sc, R92S_EFUSE_CTRL);
		if (reg & R92S_EFUSE_CTRL_VALID)
			return (MS(reg, R92S_EFUSE_CTRL_DATA));
		rsu_ms_delay(sc, 1);
	}
	device_printf(sc->sc_dev,
	    "could not read efuse byte at address 0x%x\n", addr);
	return (0xff);
}

static int
rsu_read_rom(struct rsu_softc *sc)
{
	uint8_t *rom = sc->rom;
	uint16_t addr = 0;
	uint32_t reg;
	uint8_t off, msk;
	int i;

	/* Make sure that ROM type is eFuse and that autoload succeeded. */
	reg = rsu_read_1(sc, R92S_EE_9346CR);
	if ((reg & (R92S_9356SEL | R92S_EEPROM_EN)) != R92S_EEPROM_EN)
		return (EIO);

	/* Turn on 2.5V to prevent eFuse leakage. */
	reg = rsu_read_1(sc, R92S_EFUSE_TEST + 3);
	rsu_write_1(sc, R92S_EFUSE_TEST + 3, reg | 0x80);
	rsu_ms_delay(sc, 1);
	rsu_write_1(sc, R92S_EFUSE_TEST + 3, reg & ~0x80);

	/* Read full ROM image. */
	memset(&sc->rom, 0xff, sizeof(sc->rom));
	while (addr < 512) {
		reg = rsu_efuse_read_1(sc, addr);
		if (reg == 0xff)
			break;
		addr++;
		off = reg >> 4;
		msk = reg & 0xf;
		for (i = 0; i < 4; i++) {
			if (msk & (1 << i))
				continue;
			rom[off * 8 + i * 2 + 0] =
			    rsu_efuse_read_1(sc, addr);
			addr++;
			rom[off * 8 + i * 2 + 1] =
			    rsu_efuse_read_1(sc, addr);
			addr++;
		}
	}
#ifdef USB_DEBUG
	if (rsu_debug & RSU_DEBUG_RESET) {
		/* Dump ROM content. */
		printf("\n");
		for (i = 0; i < sizeof(sc->rom); i++)
			printf("%02x:", rom[i]);
		printf("\n");
	}
#endif
	return (0);
}

static int
rsu_fw_cmd(struct rsu_softc *sc, uint8_t code, void *buf, int len)
{
	const uint8_t which = RSU_H2C_ENDPOINT;
	struct rsu_data *data;
	struct r92s_tx_desc *txd;
	struct r92s_fw_cmd_hdr *cmd;
	int cmdsz;
	int xferlen;

	RSU_ASSERT_LOCKED(sc);

	data = rsu_getbuf(sc);
	if (data == NULL)
		return (ENOMEM);

	/* Blank the entire payload, just to be safe */
	memset(data->buf, '\0', RSU_TXBUFSZ);

	/* Round-up command length to a multiple of 8 bytes. */
	/* XXX TODO: is this required? */
	cmdsz = (len + 7) & ~7;

	xferlen = sizeof(*txd) + sizeof(*cmd) + cmdsz;
	KASSERT(xferlen <= RSU_TXBUFSZ, ("%s: invalid length", __func__));
	memset(data->buf, 0, xferlen);

	/* Setup Tx descriptor. */
	txd = (struct r92s_tx_desc *)data->buf;
	txd->txdw0 = htole32(
	    SM(R92S_TXDW0_OFFSET, sizeof(*txd)) |
	    SM(R92S_TXDW0_PKTLEN, sizeof(*cmd) + cmdsz) |
	    R92S_TXDW0_OWN | R92S_TXDW0_FSG | R92S_TXDW0_LSG);
	txd->txdw1 = htole32(SM(R92S_TXDW1_QSEL, R92S_TXDW1_QSEL_H2C));

	/* Setup command header. */
	cmd = (struct r92s_fw_cmd_hdr *)&txd[1];
	cmd->len = htole16(cmdsz);
	cmd->code = code;
	cmd->seq = sc->cmd_seq;
	sc->cmd_seq = (sc->cmd_seq + 1) & 0x7f;

	/* Copy command payload. */
	memcpy(&cmd[1], buf, len);

	RSU_DPRINTF(sc, RSU_DEBUG_TX | RSU_DEBUG_FWCMD,
	    "%s: Tx cmd code=0x%x len=0x%x\n",
	    __func__, code, cmdsz);
	data->buflen = xferlen;
	STAILQ_INSERT_TAIL(&sc->sc_tx_pending[which], data, next);
	usbd_transfer_start(sc->sc_xfer[which]);

	return (0);
}

/* ARGSUSED */
static void
rsu_calib_task(void *arg, int pending __unused)
{
	struct rsu_softc *sc = arg;
#ifdef notyet
	uint32_t reg;
#endif

	RSU_DPRINTF(sc, RSU_DEBUG_CALIB, "%s: running calibration task\n",
	    __func__);

	RSU_LOCK(sc);
#ifdef notyet
	/* Read WPS PBC status. */
	rsu_write_1(sc, R92S_MAC_PINMUX_CTRL,
	    R92S_GPIOMUX_EN | SM(R92S_GPIOSEL_GPIO, R92S_GPIOSEL_GPIO_JTAG));
	rsu_write_1(sc, R92S_GPIO_IO_SEL,
	    rsu_read_1(sc, R92S_GPIO_IO_SEL) & ~R92S_GPIO_WPS);
	reg = rsu_read_1(sc, R92S_GPIO_CTRL);
	if (reg != 0xff && (reg & R92S_GPIO_WPS))
		RSU_DPRINTF(sc, RSU_DEBUG_CALIB, "WPS PBC is pushed\n");
#endif
	/* Read current signal level. */
	if (rsu_fw_iocmd(sc, 0xf4000001) == 0) {
		sc->sc_currssi = rsu_read_4(sc, R92S_IOCMD_DATA);
		RSU_DPRINTF(sc, RSU_DEBUG_CALIB, "%s: RSSI=%d (%d)\n",
		    __func__, sc->sc_currssi,
		    rsu_hwrssi_to_rssi(sc, sc->sc_currssi));
	}
	if (sc->sc_calibrating)
		taskqueue_enqueue_timeout(taskqueue_thread, &sc->calib_task, hz);
	RSU_UNLOCK(sc);
}

static void
rsu_tx_task(void *arg, int pending __unused)
{
	struct rsu_softc *sc = arg;

	RSU_LOCK(sc);
	_rsu_start(sc);
	RSU_UNLOCK(sc);
}

#define	RSU_PWR_UNKNOWN		0x0
#define	RSU_PWR_ACTIVE		0x1
#define	RSU_PWR_OFF		0x2
#define	RSU_PWR_SLEEP		0x3

/*
 * Set the current power state.
 *
 * The rtlwifi code doesn't do this so aggressively; it
 * waits for an idle period after association with
 * no traffic before doing this.
 *
 * For now - it's on in all states except RUN, and
 * in RUN it'll transition to allow sleep.
 */

struct r92s_pwr_cmd {
	uint8_t mode;
	uint8_t smart_ps;
	uint8_t bcn_pass_time;
};

static int
rsu_set_fw_power_state(struct rsu_softc *sc, int state)
{
	struct r92s_set_pwr_mode cmd;
	//struct r92s_pwr_cmd cmd;
	int error;

	RSU_ASSERT_LOCKED(sc);

	/* only change state if required */
	if (sc->sc_curpwrstate == state)
		return (0);

	memset(&cmd, 0, sizeof(cmd));

	switch (state) {
	case RSU_PWR_ACTIVE:
		/* Force the hardware awake */
		rsu_write_1(sc, R92S_USB_HRPWM,
		    R92S_USB_HRPWM_PS_ST_ACTIVE | R92S_USB_HRPWM_PS_ALL_ON);
		cmd.mode = R92S_PS_MODE_ACTIVE;
		break;
	case RSU_PWR_SLEEP:
		cmd.mode = R92S_PS_MODE_DTIM;	/* XXX configurable? */
		cmd.smart_ps = 1; /* XXX 2 if doing p2p */
		cmd.bcn_pass_time = 5; /* in 100mS usb.c, linux/rtlwifi */
		break;
	case RSU_PWR_OFF:
		cmd.mode = R92S_PS_MODE_RADIOOFF;
		break;
	default:
		device_printf(sc->sc_dev, "%s: unknown ps mode (%d)\n",
		    __func__,
		    state);
		return (ENXIO);
	}

	RSU_DPRINTF(sc, RSU_DEBUG_RESET,
	    "%s: setting ps mode to %d (mode %d)\n",
	    __func__, state, cmd.mode);
	error = rsu_fw_cmd(sc, R92S_CMD_SET_PWR_MODE, &cmd, sizeof(cmd));
	if (error == 0)
		sc->sc_curpwrstate = state;

	return (error);
}

static void
rsu_set_led(struct rsu_softc *sc, int on)
{
	rsu_write_1(sc, R92S_LEDCFG,
	    (rsu_read_1(sc, R92S_LEDCFG) & 0xf0) | (!on << 3));
}

static int
rsu_monitor_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate,
    int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct rsu_softc *sc = ic->ic_softc;
	struct rsu_vap *uvp = RSU_VAP(vap);

	if (vap->iv_state != nstate) {
		IEEE80211_UNLOCK(ic);
		RSU_LOCK(sc);

		switch (nstate) {
		case IEEE80211_S_INIT:
			sc->sc_vap_is_running = 0;
			rsu_set_led(sc, 0);
			break;
		case IEEE80211_S_RUN:
			sc->sc_vap_is_running = 1;
			rsu_set_led(sc, 1);
			break;
		default:
			/* NOTREACHED */
			break;
		}
		rsu_rxfilter_refresh(sc);

		RSU_UNLOCK(sc);
		IEEE80211_LOCK(ic);
	}

	return (uvp->newstate(vap, nstate, arg));
}

static int
rsu_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct rsu_vap *uvp = RSU_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct rsu_softc *sc = ic->ic_softc;
	struct ieee80211_node *ni;
	struct ieee80211_rateset *rs;
	enum ieee80211_state ostate;
	int error, startcal = 0;

	ostate = vap->iv_state;
	RSU_DPRINTF(sc, RSU_DEBUG_STATE, "%s: %s -> %s\n",
	    __func__,
	    ieee80211_state_name[ostate],
	    ieee80211_state_name[nstate]);

	IEEE80211_UNLOCK(ic);
	if (ostate == IEEE80211_S_RUN) {
		RSU_LOCK(sc);
		/* Stop calibration. */
		sc->sc_calibrating = 0;

		/* Pause Tx for AC queues. */
		rsu_write_1(sc, R92S_TXPAUSE, R92S_TXPAUSE_AC);
		usb_pause_mtx(&sc->sc_mtx, USB_MS_TO_TICKS(10));

		RSU_UNLOCK(sc);
		taskqueue_drain_timeout(taskqueue_thread, &sc->calib_task);
		taskqueue_drain(taskqueue_thread, &sc->tx_task);
		RSU_LOCK(sc);
		/* Disassociate from our current BSS. */
		rsu_disconnect(sc);
		usb_pause_mtx(&sc->sc_mtx, USB_MS_TO_TICKS(10));

		/* Refresh Rx filter (may be modified by firmware). */
		sc->sc_vap_is_running = 0;
		rsu_rxfilter_refresh(sc);

		/* Reinstall static keys. */
		if (sc->sc_running)
			rsu_reinit_static_keys(sc);
	} else
		RSU_LOCK(sc);
	switch (nstate) {
	case IEEE80211_S_INIT:
		(void) rsu_set_fw_power_state(sc, RSU_PWR_ACTIVE);
		break;
	case IEEE80211_S_AUTH:
		ni = ieee80211_ref_node(vap->iv_bss);
		(void) rsu_set_fw_power_state(sc, RSU_PWR_ACTIVE);
		error = rsu_join_bss(sc, ni);
		ieee80211_free_node(ni);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not send join command\n");
		}
		break;
	case IEEE80211_S_RUN:
		/* Flush all AC queues. */
		rsu_write_1(sc, R92S_TXPAUSE, 0);

		ni = ieee80211_ref_node(vap->iv_bss);
		rs = &ni->ni_rates;
		/* Indicate highest supported rate. */
		ni->ni_txrate = rs->rs_rates[rs->rs_nrates - 1];
		(void) rsu_set_fw_power_state(sc, RSU_PWR_SLEEP);
		ieee80211_free_node(ni);
		startcal = 1;
		break;
	default:
		break;
	}
	if (startcal != 0) {
		sc->sc_calibrating = 1;
		/* Start periodic calibration. */
		taskqueue_enqueue_timeout(taskqueue_thread, &sc->calib_task,
		    hz);
	}
	RSU_UNLOCK(sc);
	IEEE80211_LOCK(ic);
	return (uvp->newstate(vap, nstate, arg));
}

static int
rsu_key_alloc(struct ieee80211vap *vap, struct ieee80211_key *k,
    ieee80211_keyix *keyix, ieee80211_keyix *rxkeyix)
{
	struct rsu_softc *sc = vap->iv_ic->ic_softc;
	int is_checked = 0;

	if (&vap->iv_nw_keys[0] <= k &&
	    k < &vap->iv_nw_keys[IEEE80211_WEP_NKID]) {
		*keyix = ieee80211_crypto_get_key_wepidx(vap, k);
	} else {
		if (vap->iv_opmode != IEEE80211_M_STA) {
			*keyix = 0;
			/* TODO: obtain keyix from node id */
			is_checked = 1;
			k->wk_flags |= IEEE80211_KEY_SWCRYPT;
		} else
			*keyix = R92S_MACID_BSS;
	}

	if (!is_checked) {
		RSU_LOCK(sc);
		if (isset(sc->keys_bmap, *keyix)) {
			device_printf(sc->sc_dev,
			    "%s: key slot %d is already used!\n",
			    __func__, *keyix);
			RSU_UNLOCK(sc);
			return (0);
		}
		setbit(sc->keys_bmap, *keyix);
		RSU_UNLOCK(sc);
	}

	*rxkeyix = *keyix;

	return (1);
}

static int
rsu_process_key(struct ieee80211vap *vap, const struct ieee80211_key *k,
    int set)
{
	struct rsu_softc *sc = vap->iv_ic->ic_softc;
	int ret;

	if (k->wk_flags & IEEE80211_KEY_SWCRYPT) {
		/* Not for us. */
		return (1);
	}

	/* Handle group keys. */
	if (&vap->iv_nw_keys[0] <= k &&
	    k < &vap->iv_nw_keys[IEEE80211_WEP_NKID]) {
		KASSERT(k->wk_keyix < nitems(sc->group_keys),
		    ("keyix %u > %zu\n", k->wk_keyix, nitems(sc->group_keys)));

		RSU_LOCK(sc);
		sc->group_keys[k->wk_keyix] = (set ? k : NULL);
		if (!sc->sc_running) {
			/* Static keys will be set during device startup. */
			RSU_UNLOCK(sc);
			return (1);
		}

		if (set)
			ret = rsu_set_key_group(sc, k);
		else
			ret = rsu_delete_key(sc, k->wk_keyix);
		RSU_UNLOCK(sc);

		return (!ret);
	}

	if (set) {
		/* wait for pending key removal */
		taskqueue_drain(taskqueue_thread, &sc->del_key_task);

		RSU_LOCK(sc);
		ret = rsu_set_key_pair(sc, k);
		RSU_UNLOCK(sc);
	} else {
		RSU_DELKEY_BMAP_LOCK(sc);
		setbit(sc->free_keys_bmap, k->wk_keyix);
		RSU_DELKEY_BMAP_UNLOCK(sc);

		/* workaround ieee80211_node_delucastkey() locking */
		taskqueue_enqueue(taskqueue_thread, &sc->del_key_task);
		ret = 0;	/* fake success */
	}

	return (!ret);
}

static int
rsu_key_set(struct ieee80211vap *vap, const struct ieee80211_key *k)
{
	return (rsu_process_key(vap, k, 1));
}

static int
rsu_key_delete(struct ieee80211vap *vap, const struct ieee80211_key *k)
{
	return (rsu_process_key(vap, k, 0));
}

static int
rsu_cam_read(struct rsu_softc *sc, uint8_t addr, uint32_t *val)
{
	int ntries;

	rsu_write_4(sc, R92S_CAMCMD,
	    R92S_CAMCMD_POLLING | SM(R92S_CAMCMD_ADDR, addr));
	for (ntries = 0; ntries < 10; ntries++) {
		if (!(rsu_read_4(sc, R92S_CAMCMD) & R92S_CAMCMD_POLLING))
			break;

		usb_pause_mtx(&sc->sc_mtx, USB_MS_TO_TICKS(1));
	}
	if (ntries == 10) {
		device_printf(sc->sc_dev,
		    "%s: cannot read CAM entry at address %02X\n",
		    __func__, addr);
		return (ETIMEDOUT);
	}

	*val = rsu_read_4(sc, R92S_CAMREAD);

	return (0);
}

static void
rsu_cam_write(struct rsu_softc *sc, uint8_t addr, uint32_t data)
{

	rsu_write_4(sc, R92S_CAMWRITE, data);
	rsu_write_4(sc, R92S_CAMCMD,
	    R92S_CAMCMD_POLLING | R92S_CAMCMD_WRITE |
	    SM(R92S_CAMCMD_ADDR, addr));
}

static int
rsu_key_check(struct rsu_softc *sc, ieee80211_keyix keyix, int is_valid)
{
	uint32_t val;
	int error, ntries;

	for (ntries = 0; ntries < 20; ntries++) {
		usb_pause_mtx(&sc->sc_mtx, USB_MS_TO_TICKS(1));

		error = rsu_cam_read(sc, R92S_CAM_CTL0(keyix), &val);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: cannot check key status!\n", __func__);
			return (error);
		}
		if (((val & R92S_CAM_VALID) == 0) ^ is_valid)
			break;
	}
	if (ntries == 20) {
		device_printf(sc->sc_dev,
		    "%s: key %d is %s marked as valid, rejecting request\n",
		    __func__, keyix, is_valid ? "not" : "still");
		return (EIO);
	}

	return (0);
}

/*
 * Map net80211 cipher to RTL8712 security mode.
 */
static uint8_t
rsu_crypto_mode(struct rsu_softc *sc, u_int cipher, int keylen)
{
	switch (cipher) {
	case IEEE80211_CIPHER_WEP:
		return keylen < 8 ? R92S_KEY_ALGO_WEP40 : R92S_KEY_ALGO_WEP104;
	case IEEE80211_CIPHER_TKIP:
		return R92S_KEY_ALGO_TKIP;
	case IEEE80211_CIPHER_AES_CCM:
		return R92S_KEY_ALGO_AES;
	default:
		device_printf(sc->sc_dev, "unknown cipher %d\n", cipher);
		return R92S_KEY_ALGO_INVALID;
	}
}

static int
rsu_set_key_group(struct rsu_softc *sc, const struct ieee80211_key *k)
{
	struct r92s_fw_cmd_set_key key;
	uint8_t algo;
	int error;

	RSU_ASSERT_LOCKED(sc);

	/* Map net80211 cipher to HW crypto algorithm. */
	algo = rsu_crypto_mode(sc, k->wk_cipher->ic_cipher, k->wk_keylen);
	if (algo == R92S_KEY_ALGO_INVALID)
		return (EINVAL);

	memset(&key, 0, sizeof(key));
	key.algo = algo;
	key.cam_id = k->wk_keyix;
	key.grpkey = (k->wk_flags & IEEE80211_KEY_GROUP) != 0;
	memcpy(key.key, k->wk_key, MIN(k->wk_keylen, sizeof(key.key)));

	RSU_DPRINTF(sc, RSU_DEBUG_KEY | RSU_DEBUG_FWCMD,
	    "%s: keyix %u, group %u, algo %u/%u, flags %04X, len %u, "
	    "macaddr %s\n", __func__, key.cam_id, key.grpkey,
	    k->wk_cipher->ic_cipher, key.algo, k->wk_flags, k->wk_keylen,
	    ether_sprintf(k->wk_macaddr));

	error = rsu_fw_cmd(sc, R92S_CMD_SET_KEY, &key, sizeof(key));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: cannot send firmware command, error %d\n",
		    __func__, error);
		return (error);
	}

	return (rsu_key_check(sc, k->wk_keyix, 1));
}

static int
rsu_set_key_pair(struct rsu_softc *sc, const struct ieee80211_key *k)
{
	struct r92s_fw_cmd_set_key_mac key;
	uint8_t algo;
	int error;

	RSU_ASSERT_LOCKED(sc);

	if (!sc->sc_running)
		return (ESHUTDOWN);

	/* Map net80211 cipher to HW crypto algorithm. */
	algo = rsu_crypto_mode(sc, k->wk_cipher->ic_cipher, k->wk_keylen);
	if (algo == R92S_KEY_ALGO_INVALID)
		return (EINVAL);

	memset(&key, 0, sizeof(key));
	key.algo = algo;
	memcpy(key.macaddr, k->wk_macaddr, sizeof(key.macaddr));
	memcpy(key.key, k->wk_key, MIN(k->wk_keylen, sizeof(key.key)));

	RSU_DPRINTF(sc, RSU_DEBUG_KEY | RSU_DEBUG_FWCMD,
	    "%s: keyix %u, algo %u/%u, flags %04X, len %u, macaddr %s\n",
	    __func__, k->wk_keyix, k->wk_cipher->ic_cipher, key.algo,
	    k->wk_flags, k->wk_keylen, ether_sprintf(key.macaddr));

	error = rsu_fw_cmd(sc, R92S_CMD_SET_STA_KEY, &key, sizeof(key));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: cannot send firmware command, error %d\n",
		    __func__, error);
		return (error);
	}

	return (rsu_key_check(sc, k->wk_keyix, 1));
}

static int
rsu_reinit_static_keys(struct rsu_softc *sc)
{
	int i, error;

	for (i = 0; i < nitems(sc->group_keys); i++) {
		if (sc->group_keys[i] != NULL) {
			error = rsu_set_key_group(sc, sc->group_keys[i]);
			if (error != 0) {
				device_printf(sc->sc_dev,
				    "%s: failed to set static key %d, "
				    "error %d\n", __func__, i, error);
				return (error);
			}
		}
	}

	return (0);
}

static int
rsu_delete_key(struct rsu_softc *sc, ieee80211_keyix keyix)
{
	struct r92s_fw_cmd_set_key key;
	uint32_t val;
	int error;

	RSU_ASSERT_LOCKED(sc);

	if (!sc->sc_running)
		return (0);

	/* check if it was automatically removed by firmware */
	error = rsu_cam_read(sc, R92S_CAM_CTL0(keyix), &val);
	if (error == 0 && (val & R92S_CAM_VALID) == 0) {
		RSU_DPRINTF(sc, RSU_DEBUG_KEY,
		    "%s: key %u does not exist\n", __func__, keyix);
		clrbit(sc->keys_bmap, keyix);
		return (0);
	}

	memset(&key, 0, sizeof(key));
	key.cam_id = keyix;

	RSU_DPRINTF(sc, RSU_DEBUG_KEY | RSU_DEBUG_FWCMD,
	    "%s: removing key %u\n", __func__, key.cam_id);

	error = rsu_fw_cmd(sc, R92S_CMD_SET_KEY, &key, sizeof(key));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: cannot send firmware command, error %d\n",
		    __func__, error);
		goto finish;
	}

	usb_pause_mtx(&sc->sc_mtx, USB_MS_TO_TICKS(5));

	/*
	 * Clear 'valid' bit manually (cannot be done via firmware command).
	 * Used for key check + when firmware command cannot be sent.
	 */
finish:
	rsu_cam_write(sc, R92S_CAM_CTL0(keyix), 0);

	clrbit(sc->keys_bmap, keyix);

	return (rsu_key_check(sc, keyix, 0));
}

static void
rsu_delete_key_pair_cb(void *arg, int pending __unused)
{
	struct rsu_softc *sc = arg;
	int i;

	RSU_DELKEY_BMAP_LOCK(sc);
	for (i = IEEE80211_WEP_NKID; i < R92S_CAM_ENTRY_LIMIT; i++) {
		if (isset(sc->free_keys_bmap, i)) {
			RSU_DELKEY_BMAP_UNLOCK(sc);

			RSU_LOCK(sc);
			RSU_DPRINTF(sc, RSU_DEBUG_KEY,
			    "%s: calling rsu_delete_key() with keyix = %d\n",
			    __func__, i);
			(void) rsu_delete_key(sc, i);
			RSU_UNLOCK(sc);

			RSU_DELKEY_BMAP_LOCK(sc);
			clrbit(sc->free_keys_bmap, i);

			/* bmap can be changed */
			i = IEEE80211_WEP_NKID - 1;
			continue;
		}
	}
	RSU_DELKEY_BMAP_UNLOCK(sc);
}

static int
rsu_site_survey(struct rsu_softc *sc, struct ieee80211_scan_ssid *ssid)
{
	struct r92s_fw_cmd_sitesurvey cmd;

	RSU_ASSERT_LOCKED(sc);

	memset(&cmd, 0, sizeof(cmd));
	/* TODO: passive channels? */
	if (sc->sc_active_scan)
		cmd.active = htole32(1);
	cmd.limit = htole32(48);
	
	if (ssid != NULL) {
		sc->sc_extra_scan = 1;
		cmd.ssidlen = htole32(ssid->len);
		memcpy(cmd.ssid, ssid->ssid, ssid->len);
	}
#ifdef USB_DEBUG
	if (rsu_debug & (RSU_DEBUG_SCAN | RSU_DEBUG_FWCMD)) {
		device_printf(sc->sc_dev,
		    "sending site survey command, active %d",
		    le32toh(cmd.active));
		if (ssid != NULL) {
			printf(", ssid: ");
			ieee80211_print_essid(cmd.ssid, le32toh(cmd.ssidlen));
		}
		printf("\n");
	}
#endif
	return (rsu_fw_cmd(sc, R92S_CMD_SITE_SURVEY, &cmd, sizeof(cmd)));
}

static int
rsu_join_bss(struct rsu_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ndis_wlan_bssid_ex *bss;
	struct ndis_802_11_fixed_ies *fixed;
	struct r92s_fw_cmd_auth auth;
	uint8_t buf[sizeof(*bss) + 128] __aligned(4);
	uint8_t *frm;
	uint8_t opmode;
	int error;

	RSU_ASSERT_LOCKED(sc);

	/* Let the FW decide the opmode based on the capinfo field. */
	opmode = NDIS802_11AUTOUNKNOWN;
	RSU_DPRINTF(sc, RSU_DEBUG_RESET,
	    "%s: setting operating mode to %d\n",
	    __func__, opmode);
	error = rsu_fw_cmd(sc, R92S_CMD_SET_OPMODE, &opmode, sizeof(opmode));
	if (error != 0)
		return (error);

	memset(&auth, 0, sizeof(auth));
	if (vap->iv_flags & IEEE80211_F_WPA) {
		auth.mode = R92S_AUTHMODE_WPA;
		auth.dot1x = (ni->ni_authmode == IEEE80211_AUTH_8021X);
	} else
		auth.mode = R92S_AUTHMODE_OPEN;
	RSU_DPRINTF(sc, RSU_DEBUG_RESET,
	    "%s: setting auth mode to %d\n",
	    __func__, auth.mode);
	error = rsu_fw_cmd(sc, R92S_CMD_SET_AUTH, &auth, sizeof(auth));
	if (error != 0)
		return (error);

	memset(buf, 0, sizeof(buf));
	bss = (struct ndis_wlan_bssid_ex *)buf;
	IEEE80211_ADDR_COPY(bss->macaddr, ni->ni_bssid);
	bss->ssid.ssidlen = htole32(ni->ni_esslen);
	memcpy(bss->ssid.ssid, ni->ni_essid, ni->ni_esslen);
	if (vap->iv_flags & (IEEE80211_F_PRIVACY | IEEE80211_F_WPA))
		bss->privacy = htole32(1);
	bss->rssi = htole32(ni->ni_avgrssi);
	if (ic->ic_curmode == IEEE80211_MODE_11B)
		bss->networktype = htole32(NDIS802_11DS);
	else
		bss->networktype = htole32(NDIS802_11OFDM24);
	bss->config.len = htole32(sizeof(bss->config));
	bss->config.bintval = htole32(ni->ni_intval);
	bss->config.dsconfig = htole32(ieee80211_chan2ieee(ic, ni->ni_chan));
	bss->inframode = htole32(NDIS802_11INFRASTRUCTURE);
	/* XXX verify how this is supposed to look! */
	memcpy(bss->supprates, ni->ni_rates.rs_rates,
	    ni->ni_rates.rs_nrates);
	/* Write the fixed fields of the beacon frame. */
	fixed = (struct ndis_802_11_fixed_ies *)&bss[1];
	memcpy(&fixed->tstamp, ni->ni_tstamp.data, 8);
	fixed->bintval = htole16(ni->ni_intval);
	fixed->capabilities = htole16(ni->ni_capinfo);
	/* Write IEs to be included in the association request. */
	frm = (uint8_t *)&fixed[1];
	frm = ieee80211_add_rsn(frm, vap);
	frm = ieee80211_add_wpa(frm, vap);
	frm = ieee80211_add_qos(frm, ni);
	if ((ic->ic_flags & IEEE80211_F_WME) &&
	    (ni->ni_ies.wme_ie != NULL))
		frm = ieee80211_add_wme_info(frm, &ic->ic_wme);
	if (ni->ni_flags & IEEE80211_NODE_HT) {
		frm = ieee80211_add_htcap(frm, ni);
		frm = ieee80211_add_htinfo(frm, ni);
	}
	bss->ieslen = htole32(frm - (uint8_t *)fixed);
	bss->len = htole32(((frm - buf) + 3) & ~3);
	RSU_DPRINTF(sc, RSU_DEBUG_RESET | RSU_DEBUG_FWCMD,
	    "%s: sending join bss command to %s chan %d\n",
	    __func__,
	    ether_sprintf(bss->macaddr), le32toh(bss->config.dsconfig));
	return (rsu_fw_cmd(sc, R92S_CMD_JOIN_BSS, buf, sizeof(buf)));
}

static int
rsu_disconnect(struct rsu_softc *sc)
{
	uint32_t zero = 0;	/* :-) */

	/* Disassociate from our current BSS. */
	RSU_DPRINTF(sc, RSU_DEBUG_STATE | RSU_DEBUG_FWCMD,
	    "%s: sending disconnect command\n", __func__);
	return (rsu_fw_cmd(sc, R92S_CMD_DISCONNECT, &zero, sizeof(zero)));
}

/*
 * Map the hardware provided RSSI value to a signal level.
 * For the most part it's just something we divide by and cap
 * so it doesn't overflow the representation by net80211.
 */
static int
rsu_hwrssi_to_rssi(struct rsu_softc *sc, int hw_rssi)
{
	int v;

	if (hw_rssi == 0)
		return (0);
	v = hw_rssi >> 4;
	if (v > 80)
		v = 80;
	return (v);
}

CTASSERT(MCLBYTES > sizeof(struct ieee80211_frame));

static void
rsu_event_survey(struct rsu_softc *sc, uint8_t *buf, int len)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ndis_wlan_bssid_ex *bss;
	struct ieee80211_rx_stats rxs;
	struct mbuf *m;
	uint32_t ieslen;
	uint32_t pktlen;

	if (__predict_false(len < sizeof(*bss)))
		return;
	bss = (struct ndis_wlan_bssid_ex *)buf;
	ieslen = le32toh(bss->ieslen);
	/* range check length of information element */
	if (__predict_false(ieslen > (uint32_t)(len - sizeof(*bss))))
		return;

	RSU_DPRINTF(sc, RSU_DEBUG_SCAN,
	    "%s: found BSS %s: len=%d chan=%d inframode=%d "
	    "networktype=%d privacy=%d, RSSI=%d\n",
	    __func__,
	    ether_sprintf(bss->macaddr), ieslen,
	    le32toh(bss->config.dsconfig), le32toh(bss->inframode),
	    le32toh(bss->networktype), le32toh(bss->privacy),
	    le32toh(bss->rssi));

	/* Build a fake beacon frame to let net80211 do all the parsing. */
	/* XXX TODO: just call the new scan API methods! */
	if (__predict_false(ieslen > (size_t)(MCLBYTES - sizeof(*wh))))
		return;
	pktlen = sizeof(*wh) + ieslen;
	m = m_get2(pktlen, M_NOWAIT, MT_DATA, M_PKTHDR);
	if (__predict_false(m == NULL))
		return;
	wh = mtod(m, struct ieee80211_frame *);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_BEACON;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	USETW(wh->i_dur, 0);
	IEEE80211_ADDR_COPY(wh->i_addr1, ieee80211broadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, bss->macaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, bss->macaddr);
	*(uint16_t *)wh->i_seq = 0;
	memcpy(&wh[1], (uint8_t *)&bss[1], ieslen);

	/* Finalize mbuf. */
	m->m_pkthdr.len = m->m_len = pktlen;

	/* Set channel flags for input path */
	bzero(&rxs, sizeof(rxs));
	rxs.r_flags |= IEEE80211_R_IEEE | IEEE80211_R_FREQ;
	rxs.r_flags |= IEEE80211_R_NF | IEEE80211_R_RSSI;
	rxs.c_ieee = le32toh(bss->config.dsconfig);
	rxs.c_freq = ieee80211_ieee2mhz(rxs.c_ieee, IEEE80211_CHAN_2GHZ);
	/* This is a number from 0..100; so let's just divide it down a bit */
	rxs.c_rssi = le32toh(bss->rssi) / 2;
	rxs.c_nf = -96;
	if (ieee80211_add_rx_params(m, &rxs) == 0)
		return;

	/* XXX avoid a LOR */
	RSU_UNLOCK(sc);
	ieee80211_input_mimo_all(ic, m);
	RSU_LOCK(sc);
}

static void
rsu_event_join_bss(struct rsu_softc *sc, uint8_t *buf, int len)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct ieee80211_node *ni = vap->iv_bss;
	struct r92s_event_join_bss *rsp;
	uint32_t tmp;
	int res;

	if (__predict_false(len < sizeof(*rsp)))
		return;
	rsp = (struct r92s_event_join_bss *)buf;
	res = (int)le32toh(rsp->join_res);

	RSU_DPRINTF(sc, RSU_DEBUG_STATE | RSU_DEBUG_FWCMD,
	    "%s: Rx join BSS event len=%d res=%d\n",
	    __func__, len, res);

	/*
	 * XXX Don't do this; there's likely a better way to tell
	 * the caller we failed.
	 */
	if (res <= 0) {
		RSU_UNLOCK(sc);
		ieee80211_new_state(vap, IEEE80211_S_SCAN, -1);
		RSU_LOCK(sc);
		return;
	}

	tmp = le32toh(rsp->associd);
	if (tmp >= vap->iv_max_aid) {
		RSU_DPRINTF(sc, RSU_DEBUG_ANY, "Assoc ID overflow\n");
		tmp = 1;
	}
	RSU_DPRINTF(sc, RSU_DEBUG_STATE | RSU_DEBUG_FWCMD,
	    "%s: associated with %s associd=%d\n",
	    __func__, ether_sprintf(rsp->bss.macaddr), tmp);
	/* XXX is this required? What's the top two bits for again? */
	ni->ni_associd = tmp | 0xc000;

	/* Refresh Rx filter (was changed by firmware). */
	sc->sc_vap_is_running = 1;
	rsu_rxfilter_refresh(sc);

	RSU_UNLOCK(sc);
	ieee80211_new_state(vap, IEEE80211_S_RUN,
	    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	RSU_LOCK(sc);
}

static void
rsu_event_addba_req_report(struct rsu_softc *sc, uint8_t *buf, int len)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct r92s_add_ba_event *ba = (void *) buf;
	struct ieee80211_node *ni;

	if (len < sizeof(*ba)) {
		device_printf(sc->sc_dev, "%s: short read (%d)\n", __func__, len);
		return;
	}

	if (vap == NULL)
		return;

	RSU_DPRINTF(sc, RSU_DEBUG_AMPDU, "%s: mac=%s, tid=%d, ssn=%d\n",
	    __func__,
	    ether_sprintf(ba->mac_addr),
	    (int) ba->tid,
	    (int) le16toh(ba->ssn));

	/* XXX do node lookup; this is STA specific */

	ni = ieee80211_ref_node(vap->iv_bss);
	ieee80211_ampdu_rx_start_ext(ni, ba->tid, le16toh(ba->ssn) >> 4, 32);
	ieee80211_free_node(ni);
}

static void
rsu_rx_event(struct rsu_softc *sc, uint8_t code, uint8_t *buf, int len)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	RSU_DPRINTF(sc, RSU_DEBUG_RX | RSU_DEBUG_FWCMD,
	    "%s: Rx event code=%d len=%d\n", __func__, code, len);
	switch (code) {
	case R92S_EVT_SURVEY:
		rsu_event_survey(sc, buf, len);
		break;
	case R92S_EVT_SURVEY_DONE:
		RSU_DPRINTF(sc, RSU_DEBUG_SCAN,
		    "%s: %s scan done, found %d BSS\n",
		    __func__, sc->sc_extra_scan ? "direct" : "broadcast",
		    le32toh(*(uint32_t *)buf));
		if (sc->sc_extra_scan == 1) {
			/* Send broadcast probe request. */
			sc->sc_extra_scan = 0;
			if (vap != NULL && rsu_site_survey(sc, NULL) != 0) {
				RSU_UNLOCK(sc);
				ieee80211_cancel_scan(vap);
				RSU_LOCK(sc);
			}
			break;
		}
		if (vap != NULL) {
			RSU_UNLOCK(sc);
			ieee80211_scan_done(vap);
			RSU_LOCK(sc);
		}
		break;
	case R92S_EVT_JOIN_BSS:
		if (vap->iv_state == IEEE80211_S_AUTH)
			rsu_event_join_bss(sc, buf, len);
		break;
	case R92S_EVT_DEL_STA:
		RSU_DPRINTF(sc, RSU_DEBUG_FWCMD | RSU_DEBUG_STATE,
		    "%s: disassociated from %s\n", __func__,
		    ether_sprintf(buf));
		if (vap->iv_state == IEEE80211_S_RUN &&
		    IEEE80211_ADDR_EQ(vap->iv_bss->ni_bssid, buf)) {
			RSU_UNLOCK(sc);
			ieee80211_new_state(vap, IEEE80211_S_SCAN, -1);
			RSU_LOCK(sc);
		}
		break;
	case R92S_EVT_WPS_PBC:
		RSU_DPRINTF(sc, RSU_DEBUG_RX | RSU_DEBUG_FWCMD,
		    "%s: WPS PBC pushed.\n", __func__);
		break;
	case R92S_EVT_FWDBG:
		buf[60] = '\0';
		RSU_DPRINTF(sc, RSU_DEBUG_FWDBG, "FWDBG: %s\n", (char *)buf);
		break;
	case R92S_EVT_ADDBA_REQ_REPORT:
		rsu_event_addba_req_report(sc, buf, len);
		break;
	default:
		device_printf(sc->sc_dev, "%s: unhandled code (%d)\n", __func__, code);
		break;
	}
}

static void
rsu_rx_multi_event(struct rsu_softc *sc, uint8_t *buf, int len)
{
	struct r92s_fw_cmd_hdr *cmd;
	int cmdsz;

	RSU_DPRINTF(sc, RSU_DEBUG_RX, "%s: Rx events len=%d\n", __func__, len);

	/* Skip Rx status. */
	buf += sizeof(struct r92s_rx_stat);
	len -= sizeof(struct r92s_rx_stat);

	/* Process all events. */
	for (;;) {
		/* Check that command header fits. */
		if (__predict_false(len < sizeof(*cmd)))
			break;
		cmd = (struct r92s_fw_cmd_hdr *)buf;
		/* Check that command payload fits. */
		cmdsz = le16toh(cmd->len);
		if (__predict_false(len < sizeof(*cmd) + cmdsz))
			break;

		/* Process firmware event. */
		rsu_rx_event(sc, cmd->code, (uint8_t *)&cmd[1], cmdsz);

		if (!(cmd->seq & R92S_FW_CMD_MORE))
			break;
		buf += sizeof(*cmd) + cmdsz;
		len -= sizeof(*cmd) + cmdsz;
	}
}

static int8_t
rsu_get_rssi(struct rsu_softc *sc, int rate, void *physt)
{
	static const int8_t cckoff[] = { 14, -2, -20, -40 };
	struct r92s_rx_phystat *phy;
	struct r92s_rx_cck *cck;
	uint8_t rpt;
	int8_t rssi;

	if (rate <= 3) {
		cck = (struct r92s_rx_cck *)physt;
		rpt = (cck->agc_rpt >> 6) & 0x3;
		rssi = cck->agc_rpt & 0x3e;
		rssi = cckoff[rpt] - rssi;
	} else {	/* OFDM/HT. */
		phy = (struct r92s_rx_phystat *)physt;
		rssi = ((le32toh(phy->phydw1) >> 1) & 0x7f) - 106;
	}
	return (rssi);
}

static struct mbuf *
rsu_rx_copy_to_mbuf(struct rsu_softc *sc, struct r92s_rx_stat *stat,
    int totlen)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct mbuf *m;
	uint32_t rxdw0;
	int pktlen;

	rxdw0 = le32toh(stat->rxdw0);
	if (__predict_false(rxdw0 & (R92S_RXDW0_CRCERR | R92S_RXDW0_ICVERR))) {
		RSU_DPRINTF(sc, RSU_DEBUG_RX,
		    "%s: RX flags error (%s)\n", __func__,
		    rxdw0 & R92S_RXDW0_CRCERR ? "CRC" : "ICV");
		goto fail;
	}

	pktlen = MS(rxdw0, R92S_RXDW0_PKTLEN);
	if (__predict_false(pktlen < sizeof (struct ieee80211_frame_ack))) {
		RSU_DPRINTF(sc, RSU_DEBUG_RX,
		    "%s: frame is too short: %d\n", __func__, pktlen);
		goto fail;
	}

	m = m_get2(totlen, M_NOWAIT, MT_DATA, M_PKTHDR);
	if (__predict_false(m == NULL)) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate RX mbuf, totlen %d\n",
		    __func__, totlen);
		goto fail;
	}

	/* Finalize mbuf. */
	memcpy(mtod(m, uint8_t *), (uint8_t *)stat, totlen);
	m->m_pkthdr.len = m->m_len = totlen;
 
	return (m);
fail:
	counter_u64_add(ic->ic_ierrors, 1);
	return (NULL);
}

static uint32_t
rsu_get_tsf_low(struct rsu_softc *sc)
{
	return (rsu_read_4(sc, R92S_TSFTR));
}

static uint32_t
rsu_get_tsf_high(struct rsu_softc *sc)
{
	return (rsu_read_4(sc, R92S_TSFTR + 4));
}

static struct ieee80211_node *
rsu_rx_frame(struct rsu_softc *sc, struct mbuf *m)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame_min *wh;
	struct ieee80211_rx_stats rxs;
	struct r92s_rx_stat *stat;
	uint32_t rxdw0, rxdw3;
	uint8_t cipher, rate;
	int infosz;
	int rssi;

	stat = mtod(m, struct r92s_rx_stat *);
	rxdw0 = le32toh(stat->rxdw0);
	rxdw3 = le32toh(stat->rxdw3);

	rate = MS(rxdw3, R92S_RXDW3_RATE);
	cipher = MS(rxdw0, R92S_RXDW0_CIPHER);
	infosz = MS(rxdw0, R92S_RXDW0_INFOSZ) * 8;

	/* Get RSSI from PHY status descriptor if present. */
	if (infosz != 0 && (rxdw0 & R92S_RXDW0_PHYST))
		rssi = rsu_get_rssi(sc, rate, &stat[1]);
	else {
		/* Cheat and get the last calibrated RSSI */
		rssi = rsu_hwrssi_to_rssi(sc, sc->sc_currssi);
	}

	/* Hardware does Rx TCP checksum offload. */
	/*
	 * This flag can be set for some other
	 * (e.g., EAPOL) frame types, so don't rely on it.
	 */
	if (rxdw3 & R92S_RXDW3_TCPCHKVALID) {
		RSU_DPRINTF(sc, RSU_DEBUG_RX,
		    "%s: TCP/IP checksums: %schecked / %schecked\n",
		    __func__,
		    (rxdw3 & R92S_RXDW3_TCPCHKRPT) ? "" : "not ",
		    (rxdw3 & R92S_RXDW3_IPCHKRPT) ? "" : "not ");

		/*
		 * 'IP header checksum valid' bit will not be set if
		 * the frame was not checked / has incorrect checksum /
		 * does not have checksum (IPv6).
		 *
		 * NB: if DF bit is not set then frame will not be checked.
		 */
		if (rxdw3 & R92S_RXDW3_IPCHKRPT) {
			m->m_pkthdr.csum_flags = CSUM_IP_CHECKED;
			m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
		}

		/*
		 * This is independent of the above check.
		 */
		if (rxdw3 & R92S_RXDW3_TCPCHKRPT) {
			m->m_pkthdr.csum_flags |= CSUM_DATA_VALID;
			m->m_pkthdr.csum_flags |= CSUM_PSEUDO_HDR;
			m->m_pkthdr.csum_data = 0xffff;
		}
	}

	/* RX flags */

	/* Set channel flags for input path */
	bzero(&rxs, sizeof(rxs));

	/* normal RSSI */
	rxs.r_flags |= IEEE80211_R_NF | IEEE80211_R_RSSI;
	rxs.c_rssi = rssi;
	rxs.c_nf = -96;

	/* Rate */
	if (rate < 12) {
		rxs.c_rate = ridx2rate[rate];
		if (RSU_RATE_IS_CCK(rate))
			rxs.c_pktflags |= IEEE80211_RX_F_CCK;
		else
			rxs.c_pktflags |= IEEE80211_RX_F_OFDM;
	} else {
		rxs.c_rate = IEEE80211_RATE_MCS | (rate - 12);
		rxs.c_pktflags |= IEEE80211_RX_F_HT;
	}

	if (ieee80211_radiotap_active(ic)) {
		struct rsu_rx_radiotap_header *tap = &sc->sc_rxtap;

		/* Map HW rate index to 802.11 rate. */
		tap->wr_flags = 0;		/* TODO */
		tap->wr_tsft = rsu_get_tsf_high(sc);
		if (le32toh(stat->tsf_low) > rsu_get_tsf_low(sc))
			tap->wr_tsft--;
		tap->wr_tsft = (uint64_t)htole32(tap->wr_tsft) << 32;
		tap->wr_tsft += stat->tsf_low;

		tap->wr_rate = rxs.c_rate;
		tap->wr_dbm_antsignal = rssi;
	};

	(void) ieee80211_add_rx_params(m, &rxs);

	/* Drop descriptor. */
	m_adj(m, sizeof(*stat) + infosz);
	wh = mtod(m, struct ieee80211_frame_min *);
	if ((wh->i_fc[1] & IEEE80211_FC1_PROTECTED) &&
	    cipher != R92S_KEY_ALGO_NONE) {
		m->m_flags |= M_WEP;
	}

	RSU_DPRINTF(sc, RSU_DEBUG_RX,
	    "%s: Rx frame len %d, rate %d, infosz %d\n",
	    __func__, m->m_len, rate, infosz);

	if (m->m_len >= sizeof(*wh))
		return (ieee80211_find_rxnode(ic, wh));

	return (NULL);
}

static struct mbuf *
rsu_rx_multi_frame(struct rsu_softc *sc, uint8_t *buf, int len)
{
	struct r92s_rx_stat *stat;
	uint32_t rxdw0;
	int totlen, pktlen, infosz, npkts;
	struct mbuf *m, *m0 = NULL, *prevm = NULL;

	/*
	 * don't pass packets to the ieee80211 framework if the driver isn't
	 * RUNNING.
	 */
	if (!sc->sc_running)
		return (NULL);

	/* Get the number of encapsulated frames. */
	stat = (struct r92s_rx_stat *)buf;
	npkts = MS(le32toh(stat->rxdw2), R92S_RXDW2_PKTCNT);
	RSU_DPRINTF(sc, RSU_DEBUG_RX,
	    "%s: Rx %d frames in one chunk\n", __func__, npkts);

	/* Process all of them. */
	while (npkts-- > 0) {
		if (__predict_false(len < sizeof(*stat)))
			break;
		stat = (struct r92s_rx_stat *)buf;
		rxdw0 = le32toh(stat->rxdw0);

		pktlen = MS(rxdw0, R92S_RXDW0_PKTLEN);
		if (__predict_false(pktlen == 0))
			break;

		infosz = MS(rxdw0, R92S_RXDW0_INFOSZ) * 8;

		/* Make sure everything fits in xfer. */
		totlen = sizeof(*stat) + infosz + pktlen;
		if (__predict_false(totlen > len))
			break;

		/* Process 802.11 frame. */
		m = rsu_rx_copy_to_mbuf(sc, stat, totlen);
		if (m0 == NULL)
			m0 = m;
		if (prevm == NULL)
			prevm = m;
		else {
			prevm->m_next = m;
			prevm = m;
		}
		/* Next chunk is 128-byte aligned. */
		totlen = (totlen + 127) & ~127;
		buf += totlen;
		len -= totlen;
	}

	return (m0);
}

static struct mbuf *
rsu_rxeof(struct usb_xfer *xfer, struct rsu_data *data)
{
	struct rsu_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct r92s_rx_stat *stat;
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	if (__predict_false(len < sizeof(*stat))) {
		RSU_DPRINTF(sc, RSU_DEBUG_RX, "xfer too short %d\n", len);
		counter_u64_add(ic->ic_ierrors, 1);
		return (NULL);
	}
	/* Determine if it is a firmware C2H event or an 802.11 frame. */
	stat = (struct r92s_rx_stat *)data->buf;
	if ((le32toh(stat->rxdw1) & 0x1ff) == 0x1ff) {
		rsu_rx_multi_event(sc, data->buf, len);
		/* No packets to process. */
		return (NULL);
	} else
		return (rsu_rx_multi_frame(sc, data->buf, len));
}

static void
rsu_bulk_rx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct rsu_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m = NULL, *next;
	struct rsu_data *data;

	RSU_ASSERT_LOCKED(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		data = STAILQ_FIRST(&sc->sc_rx_active);
		if (data == NULL)
			goto tr_setup;
		STAILQ_REMOVE_HEAD(&sc->sc_rx_active, next);
		m = rsu_rxeof(xfer, data);
		STAILQ_INSERT_TAIL(&sc->sc_rx_inactive, data, next);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		data = STAILQ_FIRST(&sc->sc_rx_inactive);
		if (data == NULL) {
			KASSERT(m == NULL, ("mbuf isn't NULL"));
			return;
		}
		STAILQ_REMOVE_HEAD(&sc->sc_rx_inactive, next);
		STAILQ_INSERT_TAIL(&sc->sc_rx_active, data, next);
		usbd_xfer_set_frame_data(xfer, 0, data->buf,
		    usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		/*
		 * To avoid LOR we should unlock our private mutex here to call
		 * ieee80211_input() because here is at the end of a USB
		 * callback and safe to unlock.
		 */
		while (m != NULL) {
			next = m->m_next;
			m->m_next = NULL;

			ni = rsu_rx_frame(sc, m);
			RSU_UNLOCK(sc);

			if (ni != NULL) {
				if (ni->ni_flags & IEEE80211_NODE_HT)
					m->m_flags |= M_AMPDU;
				(void)ieee80211_input_mimo(ni, m);
				ieee80211_free_node(ni);
			} else
				(void)ieee80211_input_mimo_all(ic, m);

			RSU_LOCK(sc);
			m = next;
		}
		break;
	default:
		/* needs it to the inactive queue due to a error. */
		data = STAILQ_FIRST(&sc->sc_rx_active);
		if (data != NULL) {
			STAILQ_REMOVE_HEAD(&sc->sc_rx_active, next);
			STAILQ_INSERT_TAIL(&sc->sc_rx_inactive, data, next);
		}
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			counter_u64_add(ic->ic_ierrors, 1);
			goto tr_setup;
		}
		break;
	}

}

static void
rsu_txeof(struct usb_xfer *xfer, struct rsu_data *data)
{
#ifdef	USB_DEBUG
	struct rsu_softc *sc = usbd_xfer_softc(xfer);
#endif

	RSU_DPRINTF(sc, RSU_DEBUG_TXDONE, "%s: called; data=%p\n",
	    __func__,
	    data);

	if (data->m) {
		/* XXX status? */
		ieee80211_tx_complete(data->ni, data->m, 0);
		data->m = NULL;
		data->ni = NULL;
	}
}

static void
rsu_bulk_tx_callback_sub(struct usb_xfer *xfer, usb_error_t error,
    uint8_t which)
{
	struct rsu_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct rsu_data *data;

	RSU_ASSERT_LOCKED(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		data = STAILQ_FIRST(&sc->sc_tx_active[which]);
		if (data == NULL)
			goto tr_setup;
		RSU_DPRINTF(sc, RSU_DEBUG_TXDONE, "%s: transfer done %p\n",
		    __func__, data);
		STAILQ_REMOVE_HEAD(&sc->sc_tx_active[which], next);
		rsu_txeof(xfer, data);
		rsu_freebuf(sc, data);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		data = STAILQ_FIRST(&sc->sc_tx_pending[which]);
		if (data == NULL) {
			RSU_DPRINTF(sc, RSU_DEBUG_TXDONE,
			    "%s: empty pending queue sc %p\n", __func__, sc);
			return;
		}
		STAILQ_REMOVE_HEAD(&sc->sc_tx_pending[which], next);
		STAILQ_INSERT_TAIL(&sc->sc_tx_active[which], data, next);
		usbd_xfer_set_frame_data(xfer, 0, data->buf, data->buflen);
		RSU_DPRINTF(sc, RSU_DEBUG_TXDONE,
		    "%s: submitting transfer %p\n",
		    __func__,
		    data);
		usbd_transfer_submit(xfer);
		break;
	default:
		data = STAILQ_FIRST(&sc->sc_tx_active[which]);
		if (data != NULL) {
			STAILQ_REMOVE_HEAD(&sc->sc_tx_active[which], next);
			rsu_txeof(xfer, data);
			rsu_freebuf(sc, data);
		}
		counter_u64_add(ic->ic_oerrors, 1);

		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}

	/*
	 * XXX TODO: if the queue is low, flush out FF TX frames.
	 * Remember to unlock the driver for now; net80211 doesn't
	 * defer it for us.
	 */
}

static void
rsu_bulk_tx_callback_be_bk(struct usb_xfer *xfer, usb_error_t error)
{
	struct rsu_softc *sc = usbd_xfer_softc(xfer);

	rsu_bulk_tx_callback_sub(xfer, error, RSU_BULK_TX_BE_BK);

	/* This kicks the TX taskqueue */
	rsu_start(sc);
}

static void
rsu_bulk_tx_callback_vi_vo(struct usb_xfer *xfer, usb_error_t error)
{
	struct rsu_softc *sc = usbd_xfer_softc(xfer);

	rsu_bulk_tx_callback_sub(xfer, error, RSU_BULK_TX_VI_VO);

	/* This kicks the TX taskqueue */
	rsu_start(sc);
}

static void
rsu_bulk_tx_callback_h2c(struct usb_xfer *xfer, usb_error_t error)
{
	struct rsu_softc *sc = usbd_xfer_softc(xfer);

	rsu_bulk_tx_callback_sub(xfer, error, RSU_BULK_TX_H2C);

	/* This kicks the TX taskqueue */
	rsu_start(sc);
}

/*
 * Transmit the given frame.
 *
 * This doesn't free the node or mbuf upon failure.
 */
static int
rsu_tx_start(struct rsu_softc *sc, struct ieee80211_node *ni, 
    struct mbuf *m0, struct rsu_data *data)
{
	const struct ieee80211_txparam *tp = ni->ni_txparms;
        struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	struct r92s_tx_desc *txd;
	uint8_t rate, ridx, type, cipher, qos;
	int prio = 0;
	uint8_t which;
	int hasqos;
	int ismcast;
	int xferlen;
	int qid;

	RSU_ASSERT_LOCKED(sc);

	wh = mtod(m0, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);

	RSU_DPRINTF(sc, RSU_DEBUG_TX, "%s: data=%p, m=%p\n",
	    __func__, data, m0);

	/* Choose a TX rate index. */
	if (type == IEEE80211_FC0_TYPE_MGT ||
	    type == IEEE80211_FC0_TYPE_CTL ||
	    (m0->m_flags & M_EAPOL) != 0)
		rate = tp->mgmtrate;
	else if (ismcast)
		rate = tp->mcastrate;
	else if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE)
		rate = tp->ucastrate;
	else
		rate = 0;

	if (rate != 0)
		ridx = rate2ridx(rate);

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			device_printf(sc->sc_dev,
			    "ieee80211_crypto_encap returns NULL.\n");
			/* XXX we don't expect the fragmented frames */
			return (ENOBUFS);
		}
		wh = mtod(m0, struct ieee80211_frame *);
	}
	/* If we have QoS then use it */
	/* XXX TODO: mbuf WME/PRI versus TID? */
	if (IEEE80211_QOS_HAS_SEQ(wh)) {
		/* Has QoS */
		prio = M_WME_GETAC(m0);
		which = rsu_wme_ac_xfer_map[prio];
		hasqos = 1;
		qos = ((const struct ieee80211_qosframe *)wh)->i_qos[0];
	} else {
		/* Non-QoS TID */
		/* XXX TODO: tid=0 for non-qos TID? */
		which = rsu_wme_ac_xfer_map[WME_AC_BE];
		hasqos = 0;
		prio = 0;
		qos = 0;
	}

	qid = rsu_ac2qid[prio];
#if 0
	switch (type) {
	case IEEE80211_FC0_TYPE_CTL:
	case IEEE80211_FC0_TYPE_MGT:
		which = rsu_wme_ac_xfer_map[WME_AC_VO];
		break;
	default:
		which = rsu_wme_ac_xfer_map[M_WME_GETAC(m0)];
		break;
	}
	hasqos = 0;
#endif

	RSU_DPRINTF(sc, RSU_DEBUG_TX, "%s: pri=%d, which=%d, hasqos=%d\n",
	    __func__,
	    prio,
	    which,
	    hasqos);

	/* Fill Tx descriptor. */
	txd = (struct r92s_tx_desc *)data->buf;
	memset(txd, 0, sizeof(*txd));

	txd->txdw0 |= htole32(
	    SM(R92S_TXDW0_PKTLEN, m0->m_pkthdr.len) |
	    SM(R92S_TXDW0_OFFSET, sizeof(*txd)) |
	    R92S_TXDW0_OWN | R92S_TXDW0_FSG | R92S_TXDW0_LSG);

	txd->txdw1 |= htole32(
	    SM(R92S_TXDW1_MACID, R92S_MACID_BSS) | SM(R92S_TXDW1_QSEL, qid));
	if (!hasqos)
		txd->txdw1 |= htole32(R92S_TXDW1_NONQOS);
	if (k != NULL && !(k->wk_flags & IEEE80211_KEY_SWENCRYPT)) {
		switch (k->wk_cipher->ic_cipher) {
		case IEEE80211_CIPHER_WEP:
			cipher = R92S_TXDW1_CIPHER_WEP;
			break;
		case IEEE80211_CIPHER_TKIP:
			cipher = R92S_TXDW1_CIPHER_TKIP;
			break;
		case IEEE80211_CIPHER_AES_CCM:
			cipher = R92S_TXDW1_CIPHER_AES;
			break;
		default:
			cipher = R92S_TXDW1_CIPHER_NONE;
		}
		txd->txdw1 |= htole32(
		    SM(R92S_TXDW1_CIPHER, cipher) |
		    SM(R92S_TXDW1_KEYIDX, k->wk_keyix));
	}
	/* XXX todo: set AGGEN bit if appropriate? */
	txd->txdw2 |= htole32(R92S_TXDW2_BK);
	if (ismcast)
		txd->txdw2 |= htole32(R92S_TXDW2_BMCAST);

	if (!ismcast && (!qos || (qos & IEEE80211_QOS_ACKPOLICY) !=
	    IEEE80211_QOS_ACKPOLICY_NOACK)) {
		txd->txdw2 |= htole32(R92S_TXDW2_RTY_LMT_ENA);
		txd->txdw2 |= htole32(SM(R92S_TXDW2_RTY_LMT, tp->maxretry));
	}

	/* Force mgmt / mcast / ucast rate if needed. */
	if (rate != 0) {
		/* Data rate fallback limit (max). */
		txd->txdw5 |= htole32(SM(R92S_TXDW5_DATARATE_FB_LMT, 0x1f));
		txd->txdw5 |= htole32(SM(R92S_TXDW5_DATARATE, ridx));
		txd->txdw4 |= htole32(R92S_TXDW4_DRVRATE);
	}

	/*
	 * Firmware will use and increment the sequence number for the
	 * specified priority.
	 */
	txd->txdw3 |= htole32(SM(R92S_TXDW3_SEQ, prio));

	if (ieee80211_radiotap_active_vap(vap)) {
		struct rsu_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		ieee80211_radiotap_tx(vap, m0);
	}

	xferlen = sizeof(*txd) + m0->m_pkthdr.len;
	m_copydata(m0, 0, m0->m_pkthdr.len, (caddr_t)&txd[1]);

	data->buflen = xferlen;
	data->ni = ni;
	data->m = m0;
	STAILQ_INSERT_TAIL(&sc->sc_tx_pending[which], data, next);

	/* start transfer, if any */
	usbd_transfer_start(sc->sc_xfer[which]);
	return (0);
}

static int
rsu_transmit(struct ieee80211com *ic, struct mbuf *m)   
{
	struct rsu_softc *sc = ic->ic_softc;
	int error;

	RSU_LOCK(sc);
	if (!sc->sc_running) {
		RSU_UNLOCK(sc);
		return (ENXIO);
	}

	/*
	 * XXX TODO: ensure that we treat 'm' as a list of frames
	 * to transmit!
	 */
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		RSU_DPRINTF(sc, RSU_DEBUG_TX,
		    "%s: mbufq_enable: failed (%d)\n",
		    __func__,
		    error);
		RSU_UNLOCK(sc);
		return (error);
	}
	RSU_UNLOCK(sc);

	/* This kicks the TX taskqueue */
	rsu_start(sc);

	return (0);
}

static void
rsu_drain_mbufq(struct rsu_softc *sc)
{
	struct mbuf *m;
	struct ieee80211_node *ni;

	RSU_ASSERT_LOCKED(sc);
	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		m->m_pkthdr.rcvif = NULL;
		ieee80211_free_node(ni);
		m_freem(m);
	}
}

static void
_rsu_start(struct rsu_softc *sc)
{
	struct ieee80211_node *ni;
	struct rsu_data *bf;
	struct mbuf *m;

	RSU_ASSERT_LOCKED(sc);

	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		bf = rsu_getbuf(sc);
		if (bf == NULL) {
			RSU_DPRINTF(sc, RSU_DEBUG_TX,
			    "%s: failed to get buffer\n", __func__);
			mbufq_prepend(&sc->sc_snd, m);
			break;
		}

		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		m->m_pkthdr.rcvif = NULL;

		if (rsu_tx_start(sc, ni, m, bf) != 0) {
			RSU_DPRINTF(sc, RSU_DEBUG_TX,
			    "%s: failed to transmit\n", __func__);
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			rsu_freebuf(sc, bf);
			ieee80211_free_node(ni);
			m_freem(m);
			break;
		}
	}
}

static void
rsu_start(struct rsu_softc *sc)
{

	taskqueue_enqueue(taskqueue_thread, &sc->tx_task);
}

static int
rsu_ioctl_net(struct ieee80211com *ic, u_long cmd, void *data)
{
	struct rsu_softc *sc = ic->ic_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error;

	error = 0;
	switch (cmd) {
	case SIOCSIFCAP:
	{
		struct ieee80211vap *vap;
		int rxmask;

		rxmask = ifr->ifr_reqcap & (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6);

		RSU_LOCK(sc);
		/* Both RXCSUM bits must be set (or unset). */
		if (sc->sc_rx_checksum_enable &&
		    rxmask != (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6)) {
			rxmask = 0;
			sc->sc_rx_checksum_enable = 0;
			rsu_rxfilter_set(sc, R92S_RCR_TCP_OFFLD_EN, 0);
		} else if (!sc->sc_rx_checksum_enable && rxmask != 0) {
			rxmask = IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6;
			sc->sc_rx_checksum_enable = 1;
			rsu_rxfilter_set(sc, 0, R92S_RCR_TCP_OFFLD_EN);
		} else {
			/* Nothing to do. */
			RSU_UNLOCK(sc);
			break;
		}
		RSU_UNLOCK(sc);

		IEEE80211_LOCK(ic);	/* XXX */
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
			struct ifnet *ifp = vap->iv_ifp;

			ifp->if_capenable &=
			    ~(IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6);
			ifp->if_capenable |= rxmask;
		}
		IEEE80211_UNLOCK(ic);
		break;
	}
	default:
		error = ENOTTY;		/* for net80211 */
		break;
	}

	return (error);
}

static void
rsu_parent(struct ieee80211com *ic)
{
	struct rsu_softc *sc = ic->ic_softc;

	if (ic->ic_nrunning > 0) {
		if (rsu_init(sc) == 0)
			ieee80211_start_all(ic);
		else {
			struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
			if (vap != NULL)
				ieee80211_stop(vap);
		}
	} else
		rsu_stop(sc);
}

/*
 * Power on sequence for A-cut adapters.
 */
static void
rsu_power_on_acut(struct rsu_softc *sc)
{
	uint32_t reg;

	rsu_write_1(sc, R92S_SPS0_CTRL + 1, 0x53);
	rsu_write_1(sc, R92S_SPS0_CTRL + 0, 0x57);

	/* Enable AFE macro block's bandgap and Mbias. */
	rsu_write_1(sc, R92S_AFE_MISC,
	    rsu_read_1(sc, R92S_AFE_MISC) |
	    R92S_AFE_MISC_BGEN | R92S_AFE_MISC_MBEN);
	/* Enable LDOA15 block. */
	rsu_write_1(sc, R92S_LDOA15_CTRL,
	    rsu_read_1(sc, R92S_LDOA15_CTRL) | R92S_LDA15_EN);

	rsu_write_1(sc, R92S_SPS1_CTRL,
	    rsu_read_1(sc, R92S_SPS1_CTRL) | R92S_SPS1_LDEN);
	rsu_ms_delay(sc, 2000);
	/* Enable switch regulator block. */
	rsu_write_1(sc, R92S_SPS1_CTRL,
	    rsu_read_1(sc, R92S_SPS1_CTRL) | R92S_SPS1_SWEN);

	rsu_write_4(sc, R92S_SPS1_CTRL, 0x00a7b267);

	rsu_write_1(sc, R92S_SYS_ISO_CTRL + 1,
	    rsu_read_1(sc, R92S_SYS_ISO_CTRL + 1) | 0x08);

	rsu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    rsu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x20);

	rsu_write_1(sc, R92S_SYS_ISO_CTRL + 1,
	    rsu_read_1(sc, R92S_SYS_ISO_CTRL + 1) & ~0x90);

	/* Enable AFE clock. */
	rsu_write_1(sc, R92S_AFE_XTAL_CTRL + 1,
	    rsu_read_1(sc, R92S_AFE_XTAL_CTRL + 1) & ~0x04);
	/* Enable AFE PLL macro block. */
	rsu_write_1(sc, R92S_AFE_PLL_CTRL,
	    rsu_read_1(sc, R92S_AFE_PLL_CTRL) | 0x11);
	/* Attach AFE PLL to MACTOP/BB. */
	rsu_write_1(sc, R92S_SYS_ISO_CTRL,
	    rsu_read_1(sc, R92S_SYS_ISO_CTRL) & ~0x11);

	/* Switch to 40MHz clock instead of 80MHz. */
	rsu_write_2(sc, R92S_SYS_CLKR,
	    rsu_read_2(sc, R92S_SYS_CLKR) & ~R92S_SYS_CLKSEL);

	/* Enable MAC clock. */
	rsu_write_2(sc, R92S_SYS_CLKR,
	    rsu_read_2(sc, R92S_SYS_CLKR) |
	    R92S_MAC_CLK_EN | R92S_SYS_CLK_EN);

	rsu_write_1(sc, R92S_PMC_FSM, 0x02);

	/* Enable digital core and IOREG R/W. */
	rsu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    rsu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x08);

	rsu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    rsu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x80);

	/* Switch the control path to firmware. */
	reg = rsu_read_2(sc, R92S_SYS_CLKR);
	reg = (reg & ~R92S_SWHW_SEL) | R92S_FWHW_SEL;
	rsu_write_2(sc, R92S_SYS_CLKR, reg);

	rsu_write_2(sc, R92S_CR, 0x37fc);

	/* Fix USB RX FIFO issue. */
	rsu_write_1(sc, 0xfe5c,
	    rsu_read_1(sc, 0xfe5c) | 0x80);
	rsu_write_1(sc, 0x00ab,
	    rsu_read_1(sc, 0x00ab) | 0xc0);

	rsu_write_1(sc, R92S_SYS_CLKR,
	    rsu_read_1(sc, R92S_SYS_CLKR) & ~R92S_SYS_CPU_CLKSEL);
}

/*
 * Power on sequence for B-cut and C-cut adapters.
 */
static void
rsu_power_on_bcut(struct rsu_softc *sc)
{
	uint32_t reg;
	int ntries;

	/* Prevent eFuse leakage. */
	rsu_write_1(sc, 0x37, 0xb0);
	rsu_ms_delay(sc, 10);
	rsu_write_1(sc, 0x37, 0x30);

	/* Switch the control path to hardware. */
	reg = rsu_read_2(sc, R92S_SYS_CLKR);
	if (reg & R92S_FWHW_SEL) {
		rsu_write_2(sc, R92S_SYS_CLKR,
		    reg & ~(R92S_SWHW_SEL | R92S_FWHW_SEL));
	}
	rsu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    rsu_read_1(sc, R92S_SYS_FUNC_EN + 1) & ~0x8c);
	rsu_ms_delay(sc, 1);

	rsu_write_1(sc, R92S_SPS0_CTRL + 1, 0x53);
	rsu_write_1(sc, R92S_SPS0_CTRL + 0, 0x57);

	reg = rsu_read_1(sc, R92S_AFE_MISC);
	rsu_write_1(sc, R92S_AFE_MISC, reg | R92S_AFE_MISC_BGEN);
	rsu_write_1(sc, R92S_AFE_MISC, reg | R92S_AFE_MISC_BGEN |
	    R92S_AFE_MISC_MBEN | R92S_AFE_MISC_I32_EN);

	/* Enable PLL. */
	rsu_write_1(sc, R92S_LDOA15_CTRL,
	    rsu_read_1(sc, R92S_LDOA15_CTRL) | R92S_LDA15_EN);

	rsu_write_1(sc, R92S_LDOV12D_CTRL,
	    rsu_read_1(sc, R92S_LDOV12D_CTRL) | R92S_LDV12_EN);

	rsu_write_1(sc, R92S_SYS_ISO_CTRL + 1,
	    rsu_read_1(sc, R92S_SYS_ISO_CTRL + 1) | 0x08);

	rsu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    rsu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x20);

	/* Support 64KB IMEM. */
	rsu_write_1(sc, R92S_SYS_ISO_CTRL + 1,
	    rsu_read_1(sc, R92S_SYS_ISO_CTRL + 1) & ~0x97);

	/* Enable AFE clock. */
	rsu_write_1(sc, R92S_AFE_XTAL_CTRL + 1,
	    rsu_read_1(sc, R92S_AFE_XTAL_CTRL + 1) & ~0x04);
	/* Enable AFE PLL macro block. */
	reg = rsu_read_1(sc, R92S_AFE_PLL_CTRL);
	rsu_write_1(sc, R92S_AFE_PLL_CTRL, reg | 0x11);
	rsu_ms_delay(sc, 1);
	rsu_write_1(sc, R92S_AFE_PLL_CTRL, reg | 0x51);
	rsu_ms_delay(sc, 1);
	rsu_write_1(sc, R92S_AFE_PLL_CTRL, reg | 0x11);
	rsu_ms_delay(sc, 1);

	/* Attach AFE PLL to MACTOP/BB. */
	rsu_write_1(sc, R92S_SYS_ISO_CTRL,
	    rsu_read_1(sc, R92S_SYS_ISO_CTRL) & ~0x11);

	/* Switch to 40MHz clock. */
	rsu_write_1(sc, R92S_SYS_CLKR, 0x00);
	/* Disable CPU clock and 80MHz SSC. */
	rsu_write_1(sc, R92S_SYS_CLKR,
	    rsu_read_1(sc, R92S_SYS_CLKR) | 0xa0);
	/* Enable MAC clock. */
	rsu_write_2(sc, R92S_SYS_CLKR,
	    rsu_read_2(sc, R92S_SYS_CLKR) |
	    R92S_MAC_CLK_EN | R92S_SYS_CLK_EN);

	rsu_write_1(sc, R92S_PMC_FSM, 0x02);

	/* Enable digital core and IOREG R/W. */
	rsu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    rsu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x08);

	rsu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    rsu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x80);

	/* Switch the control path to firmware. */
	reg = rsu_read_2(sc, R92S_SYS_CLKR);
	reg = (reg & ~R92S_SWHW_SEL) | R92S_FWHW_SEL;
	rsu_write_2(sc, R92S_SYS_CLKR, reg);

	rsu_write_2(sc, R92S_CR, 0x37fc);

	/* Fix USB RX FIFO issue. */
	rsu_write_1(sc, 0xfe5c,
	    rsu_read_1(sc, 0xfe5c) | 0x80);

	rsu_write_1(sc, R92S_SYS_CLKR,
	    rsu_read_1(sc, R92S_SYS_CLKR) & ~R92S_SYS_CPU_CLKSEL);

	rsu_write_1(sc, 0xfe1c, 0x80);

	/* Make sure TxDMA is ready to download firmware. */
	for (ntries = 0; ntries < 20; ntries++) {
		reg = rsu_read_1(sc, R92S_TCR);
		if ((reg & (R92S_TCR_IMEM_CHK_RPT | R92S_TCR_EMEM_CHK_RPT)) ==
		    (R92S_TCR_IMEM_CHK_RPT | R92S_TCR_EMEM_CHK_RPT))
			break;
		rsu_ms_delay(sc, 1);
	}
	if (ntries == 20) {
		RSU_DPRINTF(sc, RSU_DEBUG_RESET | RSU_DEBUG_TX,
		    "%s: TxDMA is not ready\n",
		    __func__);
		/* Reset TxDMA. */
		reg = rsu_read_1(sc, R92S_CR);
		rsu_write_1(sc, R92S_CR, reg & ~R92S_CR_TXDMA_EN);
		rsu_ms_delay(sc, 1);
		rsu_write_1(sc, R92S_CR, reg | R92S_CR_TXDMA_EN);
	}
}

static void
rsu_power_off(struct rsu_softc *sc)
{
	/* Turn RF off. */
	rsu_write_1(sc, R92S_RF_CTRL, 0x00);
	rsu_ms_delay(sc, 5);

	/* Turn MAC off. */
	/* Switch control path. */
	rsu_write_1(sc, R92S_SYS_CLKR + 1, 0x38);
	/* Reset MACTOP. */
	rsu_write_1(sc, R92S_SYS_FUNC_EN + 1, 0x70);
	rsu_write_1(sc, R92S_PMC_FSM, 0x06);
	rsu_write_1(sc, R92S_SYS_ISO_CTRL + 0, 0xf9);
	rsu_write_1(sc, R92S_SYS_ISO_CTRL + 1, 0xe8);

	/* Disable AFE PLL. */
	rsu_write_1(sc, R92S_AFE_PLL_CTRL, 0x00);
	/* Disable A15V. */
	rsu_write_1(sc, R92S_LDOA15_CTRL, 0x54);
	/* Disable eFuse 1.2V. */
	rsu_write_1(sc, R92S_SYS_FUNC_EN + 1, 0x50);
	rsu_write_1(sc, R92S_LDOV12D_CTRL, 0x24);
	/* Enable AFE macro block's bandgap and Mbias. */
	rsu_write_1(sc, R92S_AFE_MISC, 0x30);
	/* Disable 1.6V LDO. */
	rsu_write_1(sc, R92S_SPS0_CTRL + 0, 0x56);
	rsu_write_1(sc, R92S_SPS0_CTRL + 1, 0x43);

	/* Firmware - tell it to switch things off */
	(void) rsu_set_fw_power_state(sc, RSU_PWR_OFF);
}

static int
rsu_fw_loadsection(struct rsu_softc *sc, const uint8_t *buf, int len)
{
	const uint8_t which = rsu_wme_ac_xfer_map[WME_AC_VO];
	struct rsu_data *data;
	struct r92s_tx_desc *txd;
	int mlen;

	while (len > 0) {
		data = rsu_getbuf(sc);
		if (data == NULL)
			return (ENOMEM);
		txd = (struct r92s_tx_desc *)data->buf;
		memset(txd, 0, sizeof(*txd));
		if (len <= RSU_TXBUFSZ - sizeof(*txd)) {
			/* Last chunk. */
			txd->txdw0 |= htole32(R92S_TXDW0_LINIP);
			mlen = len;
		} else
			mlen = RSU_TXBUFSZ - sizeof(*txd);
		txd->txdw0 |= htole32(SM(R92S_TXDW0_PKTLEN, mlen));
		memcpy(&txd[1], buf, mlen);
		data->buflen = sizeof(*txd) + mlen;
		RSU_DPRINTF(sc, RSU_DEBUG_TX | RSU_DEBUG_FW | RSU_DEBUG_RESET,
		    "%s: starting transfer %p\n",
		    __func__, data);
		STAILQ_INSERT_TAIL(&sc->sc_tx_pending[which], data, next);
		buf += mlen;
		len -= mlen;
	}
	usbd_transfer_start(sc->sc_xfer[which]);
	return (0);
}

CTASSERT(sizeof(size_t) >= sizeof(uint32_t));

static int
rsu_load_firmware(struct rsu_softc *sc)
{
	const struct r92s_fw_hdr *hdr;
	struct r92s_fw_priv *dmem;
	struct ieee80211com *ic = &sc->sc_ic;
	const uint8_t *imem, *emem;
	uint32_t imemsz, ememsz;
	const struct firmware *fw;
	size_t size;
	uint32_t reg;
	int ntries, error;

	if (rsu_read_1(sc, R92S_TCR) & R92S_TCR_FWRDY) {
		RSU_DPRINTF(sc, RSU_DEBUG_ANY,
		    "%s: Firmware already loaded\n",
		    __func__);
		return (0);
	}

	RSU_UNLOCK(sc);
	/* Read firmware image from the filesystem. */
	if ((fw = firmware_get("rsu-rtl8712fw")) == NULL) {
		device_printf(sc->sc_dev, 
		    "%s: failed load firmware of file rsu-rtl8712fw\n",
		    __func__);
		RSU_LOCK(sc);
		return (ENXIO);
	}
	RSU_LOCK(sc);
	size = fw->datasize;
	if (size < sizeof(*hdr)) {
		device_printf(sc->sc_dev, "firmware too short\n");
		error = EINVAL;
		goto fail;
	}
	hdr = (const struct r92s_fw_hdr *)fw->data;
	if (hdr->signature != htole16(0x8712) &&
	    hdr->signature != htole16(0x8192)) {
		device_printf(sc->sc_dev,
		    "invalid firmware signature 0x%x\n",
		    le16toh(hdr->signature));
		error = EINVAL;
		goto fail;
	}
	RSU_DPRINTF(sc, RSU_DEBUG_FW, "FW V%d %02x-%02x %02x:%02x\n",
	    le16toh(hdr->version), hdr->month, hdr->day, hdr->hour,
	    hdr->minute);

	/* Make sure that driver and firmware are in sync. */
	if (hdr->privsz != htole32(sizeof(*dmem))) {
		device_printf(sc->sc_dev, "unsupported firmware image\n");
		error = EINVAL;
		goto fail;
	}
	/* Get FW sections sizes. */
	imemsz = le32toh(hdr->imemsz);
	ememsz = le32toh(hdr->sramsz);
	/* Check that all FW sections fit in image. */
	if (imemsz > (size_t)(size - sizeof(*hdr)) ||
	    ememsz > (size_t)(size - sizeof(*hdr) - imemsz)) {
		device_printf(sc->sc_dev, "firmware too short\n");
		error = EINVAL;
		goto fail;
	}
	imem = (const uint8_t *)&hdr[1];
	emem = imem + imemsz;

	/* Load IMEM section. */
	error = rsu_fw_loadsection(sc, imem, imemsz);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not load firmware section %s\n", "IMEM");
		goto fail;
	}
	/* Wait for load to complete. */
	for (ntries = 0; ntries != 50; ntries++) {
		rsu_ms_delay(sc, 10);
		reg = rsu_read_1(sc, R92S_TCR);
		if (reg & R92S_TCR_IMEM_CODE_DONE)
			break;
	}
	if (ntries == 50) {
		device_printf(sc->sc_dev, "timeout waiting for IMEM transfer\n");
		error = ETIMEDOUT;
		goto fail;
	}
	/* Load EMEM section. */
	error = rsu_fw_loadsection(sc, emem, ememsz);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not load firmware section %s\n", "EMEM");
		goto fail;
	}
	/* Wait for load to complete. */
	for (ntries = 0; ntries != 50; ntries++) {
		rsu_ms_delay(sc, 10);
		reg = rsu_read_2(sc, R92S_TCR);
		if (reg & R92S_TCR_EMEM_CODE_DONE)
			break;
	}
	if (ntries == 50) {
		device_printf(sc->sc_dev, "timeout waiting for EMEM transfer\n");
		error = ETIMEDOUT;
		goto fail;
	}
	/* Enable CPU. */
	rsu_write_1(sc, R92S_SYS_CLKR,
	    rsu_read_1(sc, R92S_SYS_CLKR) | R92S_SYS_CPU_CLKSEL);
	if (!(rsu_read_1(sc, R92S_SYS_CLKR) & R92S_SYS_CPU_CLKSEL)) {
		device_printf(sc->sc_dev, "could not enable system clock\n");
		error = EIO;
		goto fail;
	}
	rsu_write_2(sc, R92S_SYS_FUNC_EN,
	    rsu_read_2(sc, R92S_SYS_FUNC_EN) | R92S_FEN_CPUEN);
	if (!(rsu_read_2(sc, R92S_SYS_FUNC_EN) & R92S_FEN_CPUEN)) {
		device_printf(sc->sc_dev, 
		    "could not enable microcontroller\n");
		error = EIO;
		goto fail;
	}
	/* Wait for CPU to initialize. */
	for (ntries = 0; ntries < 100; ntries++) {
		if (rsu_read_1(sc, R92S_TCR) & R92S_TCR_IMEM_RDY)
			break;
		rsu_ms_delay(sc, 1);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev,
		    "timeout waiting for microcontroller\n");
		error = ETIMEDOUT;
		goto fail;
	}

	/* Update DMEM section before loading. */
	dmem = __DECONST(struct r92s_fw_priv *, &hdr->priv);
	memset(dmem, 0, sizeof(*dmem));
	dmem->hci_sel = R92S_HCI_SEL_USB | R92S_HCI_SEL_8172;
	dmem->nendpoints = sc->sc_nendpoints;
	dmem->chip_version = sc->cut;
	dmem->rf_config = sc->sc_rftype;
	dmem->vcs_type = R92S_VCS_TYPE_AUTO;
	dmem->vcs_mode = R92S_VCS_MODE_RTS_CTS;
	dmem->turbo_mode = 0;
	dmem->bw40_en = !! (ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40);
	dmem->amsdu2ampdu_en = !! (sc->sc_ht);
	dmem->ampdu_en = !! (sc->sc_ht);
	dmem->agg_offload = !! (sc->sc_ht);
	dmem->qos_en = 1;
	dmem->ps_offload = 1;
	dmem->lowpower_mode = 1;	/* XXX TODO: configurable? */
	/* Load DMEM section. */
	error = rsu_fw_loadsection(sc, (uint8_t *)dmem, sizeof(*dmem));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not load firmware section %s\n", "DMEM");
		goto fail;
	}
	/* Wait for load to complete. */
	for (ntries = 0; ntries < 100; ntries++) {
		if (rsu_read_1(sc, R92S_TCR) & R92S_TCR_DMEM_CODE_DONE)
			break;
		rsu_ms_delay(sc, 1);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev, "timeout waiting for %s transfer\n",
		    "DMEM");
		error = ETIMEDOUT;
		goto fail;
	}
	/* Wait for firmware readiness. */
	for (ntries = 0; ntries < 60; ntries++) {
		if (!(rsu_read_1(sc, R92S_TCR) & R92S_TCR_FWRDY))
			break;
		rsu_ms_delay(sc, 1);
	}
	if (ntries == 60) {
		device_printf(sc->sc_dev, 
		    "timeout waiting for firmware readiness\n");
		error = ETIMEDOUT;
		goto fail;
	}
 fail:
	firmware_put(fw, FIRMWARE_UNLOAD);
	return (error);
}


static int	
rsu_raw_xmit(struct ieee80211_node *ni, struct mbuf *m, 
    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct rsu_softc *sc = ic->ic_softc;
	struct rsu_data *bf;

	/* prevent management frames from being sent if we're not ready */
	if (!sc->sc_running) {
		m_freem(m);
		return (ENETDOWN);
	}
	RSU_LOCK(sc);
	bf = rsu_getbuf(sc);
	if (bf == NULL) {
		m_freem(m);
		RSU_UNLOCK(sc);
		return (ENOBUFS);
	}
	if (rsu_tx_start(sc, ni, m, bf) != 0) {
		m_freem(m);
		rsu_freebuf(sc, bf);
		RSU_UNLOCK(sc);
		return (EIO);
	}
	RSU_UNLOCK(sc);

	return (0);
}

static void
rsu_rxfilter_init(struct rsu_softc *sc)
{
	uint32_t reg;

	RSU_ASSERT_LOCKED(sc);

	/* Setup multicast filter. */
	rsu_set_multi(sc);

	/* Adjust Rx filter. */
	reg = rsu_read_4(sc, R92S_RCR);
	reg &= ~R92S_RCR_AICV;
	reg |= R92S_RCR_APP_PHYSTS;
	if (sc->sc_rx_checksum_enable)
		reg |= R92S_RCR_TCP_OFFLD_EN;
	rsu_write_4(sc, R92S_RCR, reg);

	/* Update dynamic Rx filter parts. */
	rsu_rxfilter_refresh(sc);
}

static void
rsu_rxfilter_set(struct rsu_softc *sc, uint32_t clear, uint32_t set)
{
	/* NB: firmware can touch this register too. */
	rsu_write_4(sc, R92S_RCR,
	   (rsu_read_4(sc, R92S_RCR) & ~clear) | set);
}

static void
rsu_rxfilter_refresh(struct rsu_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t mask_all, mask_min;

	RSU_ASSERT_LOCKED(sc);

	/* NB: RCR_AMF / RXFLTMAP_MGT are used by firmware. */
	mask_all = R92S_RCR_ACF | R92S_RCR_AAP;
	mask_min = R92S_RCR_APM;
	if (sc->sc_vap_is_running)
		mask_min |= R92S_RCR_CBSSID;
	else
		mask_all |= R92S_RCR_ADF;

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		uint16_t rxfltmap;
		if (sc->sc_vap_is_running)
			rxfltmap = 0;
		else
			rxfltmap = R92S_RXFLTMAP_MGT_DEF;
		rsu_write_2(sc, R92S_RXFLTMAP_MGT, rxfltmap);
	}

	if (ic->ic_promisc == 0 && ic->ic_opmode != IEEE80211_M_MONITOR)
		rsu_rxfilter_set(sc, mask_all, mask_min);
	else
		rsu_rxfilter_set(sc, mask_min, mask_all);
}

static int
rsu_init(struct rsu_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint8_t macaddr[IEEE80211_ADDR_LEN];
	int error;
	int i;

	RSU_LOCK(sc);

	if (sc->sc_running) {
		RSU_UNLOCK(sc);
		return (0);
	}

	/* Ensure the mbuf queue is drained */
	rsu_drain_mbufq(sc);

	/* Reset power management state. */
	rsu_write_1(sc, R92S_USB_HRPWM, 0);

	/* Power on adapter. */
	if (sc->cut == 1)
		rsu_power_on_acut(sc);
	else
		rsu_power_on_bcut(sc);

	/* Load firmware. */
	error = rsu_load_firmware(sc);
	if (error != 0)
		goto fail;

	rsu_write_4(sc, R92S_CR,
	    rsu_read_4(sc, R92S_CR) & ~0xff000000);

	/* Use 128 bytes pages. */
	rsu_write_1(sc, 0x00b5,
	    rsu_read_1(sc, 0x00b5) | 0x01);
	/* Enable USB Rx aggregation. */
	rsu_write_1(sc, 0x00bd,
	    rsu_read_1(sc, 0x00bd) | 0x80);
	/* Set USB Rx aggregation threshold. */
	rsu_write_1(sc, 0x00d9, 0x01);
	/* Set USB Rx aggregation timeout (1.7ms/4). */
	rsu_write_1(sc, 0xfe5b, 0x04);
	/* Fix USB Rx FIFO issue. */
	rsu_write_1(sc, 0xfe5c,
	    rsu_read_1(sc, 0xfe5c) | 0x80);

	/* Set MAC address. */
	IEEE80211_ADDR_COPY(macaddr, vap ? vap->iv_myaddr : ic->ic_macaddr);
	rsu_write_region_1(sc, R92S_MACID, macaddr, IEEE80211_ADDR_LEN);

	/* It really takes 1.5 seconds for the firmware to boot: */
	usb_pause_mtx(&sc->sc_mtx, USB_MS_TO_TICKS(2000));

	RSU_DPRINTF(sc, RSU_DEBUG_RESET, "%s: setting MAC address to %s\n",
	    __func__,
	    ether_sprintf(macaddr));
	error = rsu_fw_cmd(sc, R92S_CMD_SET_MAC_ADDRESS, macaddr,
	    IEEE80211_ADDR_LEN);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not set MAC address\n");
		goto fail;
	}

	/* Initialize Rx filter. */
	rsu_rxfilter_init(sc);

	/* Set PS mode fully active */
	error = rsu_set_fw_power_state(sc, RSU_PWR_ACTIVE);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not set PS mode\n");
		goto fail;
	}

	/* Install static keys (if any). */
	error = rsu_reinit_static_keys(sc);
	if (error != 0)
		goto fail;

	sc->sc_extra_scan = 0;
	usbd_transfer_start(sc->sc_xfer[RSU_BULK_RX]);

	/* We're ready to go. */
	sc->sc_running = 1;
	RSU_UNLOCK(sc);

	return (0);
fail:
	/* Need to stop all failed transfers, if any */
	for (i = 0; i != RSU_N_TRANSFER; i++)
		usbd_transfer_stop(sc->sc_xfer[i]);
	RSU_UNLOCK(sc);

	return (error);
}

static void
rsu_stop(struct rsu_softc *sc)
{
	int i;

	RSU_LOCK(sc);
	if (!sc->sc_running) {
		RSU_UNLOCK(sc);
		return;
	}

	sc->sc_running = 0;
	sc->sc_vap_is_running = 0;
	sc->sc_calibrating = 0;
	taskqueue_cancel_timeout(taskqueue_thread, &sc->calib_task, NULL);
	taskqueue_cancel(taskqueue_thread, &sc->tx_task, NULL);

	/* Power off adapter. */
	rsu_power_off(sc);

	/*
	 * CAM is not accessible after shutdown;
	 * all entries are marked (by firmware?) as invalid.
	 */
	memset(sc->free_keys_bmap, 0, sizeof(sc->free_keys_bmap));
	memset(sc->keys_bmap, 0, sizeof(sc->keys_bmap));

	for (i = 0; i < RSU_N_TRANSFER; i++)
		usbd_transfer_stop(sc->sc_xfer[i]);

	/* Ensure the mbuf queue is drained */
	rsu_drain_mbufq(sc);
	RSU_UNLOCK(sc);
}

/*
 * Note: usb_pause_mtx() actually releases the mutex before calling pause(),
 * which breaks any kind of driver serialisation.
 */
static void
rsu_ms_delay(struct rsu_softc *sc, int ms)
{

	//usb_pause_mtx(&sc->sc_mtx, hz / 1000);
	DELAY(ms * 1000);
}

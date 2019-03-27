/*-
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD AND BSD-1-Clause)
 *
 * Copyright (c) 2006 Sam Leffler, Errno Consulting
 * Copyright (c) 2008-2009 Weongyo Jeong <weongyo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

/*
 * This driver is distantly derived from a driver of the same name
 * by Damien Bergamini.  The original copyright is included below:
 *
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
 * Driver for Atheros AR5523 USB parts.
 *
 * The driver requires firmware to be loaded into the device.  This
 * is done on device discovery from a user application (uathload)
 * that is launched by devd when a device with suitable product ID
 * is recognized.  Once firmware has been loaded the device will
 * reset the USB port and re-attach with the original product ID+1
 * and this driver will be attached.  The firmware is licensed for
 * general use (royalty free) and may be incorporated in products.
 * Note that the firmware normally packaged with the NDIS drivers
 * for these devices does not work in this way and so does not work
 * with this driver.
 */

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kdb.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#endif

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_input.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"

#include <dev/usb/wlan/if_uathreg.h>
#include <dev/usb/wlan/if_uathvar.h>

static SYSCTL_NODE(_hw_usb, OID_AUTO, uath, CTLFLAG_RW, 0, "USB Atheros");

static	int uath_countrycode = CTRY_DEFAULT;	/* country code */
SYSCTL_INT(_hw_usb_uath, OID_AUTO, countrycode, CTLFLAG_RWTUN, &uath_countrycode,
    0, "country code");
static	int uath_regdomain = 0;			/* regulatory domain */
SYSCTL_INT(_hw_usb_uath, OID_AUTO, regdomain, CTLFLAG_RD, &uath_regdomain,
    0, "regulatory domain");

#ifdef UATH_DEBUG
int uath_debug = 0;
SYSCTL_INT(_hw_usb_uath, OID_AUTO, debug, CTLFLAG_RWTUN, &uath_debug, 0,
    "uath debug level");
enum {
	UATH_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	UATH_DEBUG_XMIT_DUMP	= 0x00000002,	/* xmit dump */
	UATH_DEBUG_RECV		= 0x00000004,	/* basic recv operation */
	UATH_DEBUG_TX_PROC	= 0x00000008,	/* tx ISR proc */
	UATH_DEBUG_RX_PROC	= 0x00000010,	/* rx ISR proc */
	UATH_DEBUG_RECV_ALL	= 0x00000020,	/* trace all frames (beacons) */
	UATH_DEBUG_INIT		= 0x00000040,	/* initialization of dev */
	UATH_DEBUG_DEVCAP	= 0x00000080,	/* dev caps */
	UATH_DEBUG_CMDS		= 0x00000100,	/* commands */
	UATH_DEBUG_CMDS_DUMP	= 0x00000200,	/* command buffer dump */
	UATH_DEBUG_RESET	= 0x00000400,	/* reset processing */
	UATH_DEBUG_STATE	= 0x00000800,	/* 802.11 state transitions */
	UATH_DEBUG_MULTICAST	= 0x00001000,	/* multicast */
	UATH_DEBUG_WME		= 0x00002000,	/* WME */
	UATH_DEBUG_CHANNEL	= 0x00004000,	/* channel */
	UATH_DEBUG_RATES	= 0x00008000,	/* rates */
	UATH_DEBUG_CRYPTO	= 0x00010000,	/* crypto */
	UATH_DEBUG_LED		= 0x00020000,	/* LED */
	UATH_DEBUG_ANY		= 0xffffffff
};
#define	DPRINTF(sc, m, fmt, ...) do {				\
	if (sc->sc_debug & (m))					\
		printf(fmt, __VA_ARGS__);			\
} while (0)
#else
#define	DPRINTF(sc, m, fmt, ...) do {				\
	(void) sc;						\
} while (0)
#endif

/* recognized device vendors/products */
static const STRUCT_USB_HOST_ID uath_devs[] = {
#define	UATH_DEV(v,p) { USB_VP(USB_VENDOR_##v, USB_PRODUCT_##v##_##p) }
	UATH_DEV(ACCTON,		SMCWUSBTG2),
	UATH_DEV(ATHEROS,		AR5523),
	UATH_DEV(ATHEROS2,		AR5523_1),
	UATH_DEV(ATHEROS2,		AR5523_2),
	UATH_DEV(ATHEROS2,		AR5523_3),
	UATH_DEV(CONCEPTRONIC,		AR5523_1),
	UATH_DEV(CONCEPTRONIC,		AR5523_2),
	UATH_DEV(DLINK,			DWLAG122),
	UATH_DEV(DLINK,			DWLAG132),
	UATH_DEV(DLINK,			DWLG132),
	UATH_DEV(DLINK2,		DWA120),
	UATH_DEV(GIGASET,		AR5523),
	UATH_DEV(GIGASET,		SMCWUSBTG),
	UATH_DEV(GLOBALSUN,		AR5523_1),
	UATH_DEV(GLOBALSUN,		AR5523_2),
	UATH_DEV(NETGEAR,		WG111U),
	UATH_DEV(NETGEAR3,		WG111T),
	UATH_DEV(NETGEAR3,		WPN111),
	UATH_DEV(NETGEAR3,		WPN111_2),
	UATH_DEV(UMEDIA,		TEW444UBEU),
	UATH_DEV(UMEDIA,		AR5523_2),
	UATH_DEV(WISTRONNEWEB,		AR5523_1),
	UATH_DEV(WISTRONNEWEB,		AR5523_2),
	UATH_DEV(ZCOM,			AR5523)
#undef UATH_DEV
};

static usb_callback_t uath_intr_rx_callback;
static usb_callback_t uath_intr_tx_callback;
static usb_callback_t uath_bulk_rx_callback;
static usb_callback_t uath_bulk_tx_callback;

static const struct usb_config uath_usbconfig[UATH_N_XFERS] = {
	[UATH_INTR_RX] = {
		.type = UE_BULK,
		.endpoint = 0x1,
		.direction = UE_DIR_IN,
		.bufsize = UATH_MAX_CMDSZ,
		.flags = {
			.pipe_bof = 1,
			.short_xfer_ok = 1
		},
		.callback = uath_intr_rx_callback
	},
	[UATH_INTR_TX] = {
		.type = UE_BULK,
		.endpoint = 0x1,
		.direction = UE_DIR_OUT,
		.bufsize = UATH_MAX_CMDSZ * UATH_CMD_LIST_COUNT,
		.flags = {
			.force_short_xfer = 1,
			.pipe_bof = 1,
		},
		.callback = uath_intr_tx_callback,
		.timeout = UATH_CMD_TIMEOUT
	},
	[UATH_BULK_RX] = {
		.type = UE_BULK,
		.endpoint = 0x2,
		.direction = UE_DIR_IN,
		.bufsize = MCLBYTES,
		.flags = {
			.ext_buffer = 1,
			.pipe_bof = 1,
			.short_xfer_ok = 1
		},
		.callback = uath_bulk_rx_callback
	},
	[UATH_BULK_TX] = {
		.type = UE_BULK,
		.endpoint = 0x2,
		.direction = UE_DIR_OUT,
		.bufsize = UATH_MAX_TXBUFSZ * UATH_TX_DATA_LIST_COUNT,
		.flags = {
			.force_short_xfer = 1,
			.pipe_bof = 1
		},
		.callback = uath_bulk_tx_callback,
		.timeout = UATH_DATA_TIMEOUT
	}
};

static struct ieee80211vap *uath_vap_create(struct ieee80211com *,
		    const char [IFNAMSIZ], int, enum ieee80211_opmode, int,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    const uint8_t [IEEE80211_ADDR_LEN]);
static void	uath_vap_delete(struct ieee80211vap *);
static int	uath_alloc_cmd_list(struct uath_softc *, struct uath_cmd []);
static void	uath_free_cmd_list(struct uath_softc *, struct uath_cmd []);
static int	uath_host_available(struct uath_softc *);
static int	uath_get_capability(struct uath_softc *, uint32_t, uint32_t *);
static int	uath_get_devcap(struct uath_softc *);
static struct uath_cmd *
		uath_get_cmdbuf(struct uath_softc *);
static int	uath_cmd_read(struct uath_softc *, uint32_t, const void *,
		    int, void *, int, int);
static int	uath_cmd_write(struct uath_softc *, uint32_t, const void *,
		    int, int);
static void	uath_stat(void *);
#ifdef UATH_DEBUG
static void	uath_dump_cmd(const uint8_t *, int, char);
static const char *
		uath_codename(int);
#endif
static int	uath_get_devstatus(struct uath_softc *,
		    uint8_t macaddr[IEEE80211_ADDR_LEN]);
static int	uath_get_status(struct uath_softc *, uint32_t, void *, int);
static int	uath_alloc_rx_data_list(struct uath_softc *);
static int	uath_alloc_tx_data_list(struct uath_softc *);
static void	uath_free_rx_data_list(struct uath_softc *);
static void	uath_free_tx_data_list(struct uath_softc *);
static int	uath_init(struct uath_softc *);
static void	uath_stop(struct uath_softc *);
static void	uath_parent(struct ieee80211com *);
static int	uath_transmit(struct ieee80211com *, struct mbuf *);
static void	uath_start(struct uath_softc *);
static int	uath_raw_xmit(struct ieee80211_node *, struct mbuf *,
		    const struct ieee80211_bpf_params *);
static void	uath_scan_start(struct ieee80211com *);
static void	uath_scan_end(struct ieee80211com *);
static void	uath_set_channel(struct ieee80211com *);
static void	uath_update_mcast(struct ieee80211com *);
static void	uath_update_promisc(struct ieee80211com *);
static int	uath_config(struct uath_softc *, uint32_t, uint32_t);
static int	uath_config_multi(struct uath_softc *, uint32_t, const void *,
		    int);
static int	uath_switch_channel(struct uath_softc *,
		    struct ieee80211_channel *);
static int	uath_set_rxfilter(struct uath_softc *, uint32_t, uint32_t);
static void	uath_watchdog(void *);
static void	uath_abort_xfers(struct uath_softc *);
static int	uath_dataflush(struct uath_softc *);
static int	uath_cmdflush(struct uath_softc *);
static int	uath_flush(struct uath_softc *);
static int	uath_set_ledstate(struct uath_softc *, int);
static int	uath_set_chan(struct uath_softc *, struct ieee80211_channel *);
static int	uath_reset_tx_queues(struct uath_softc *);
static int	uath_wme_init(struct uath_softc *);
static struct uath_data *
		uath_getbuf(struct uath_softc *);
static int	uath_newstate(struct ieee80211vap *, enum ieee80211_state,
		    int);
static int	uath_set_key(struct uath_softc *,
		    const struct ieee80211_key *, int);
static int	uath_set_keys(struct uath_softc *, struct ieee80211vap *);
static void	uath_sysctl_node(struct uath_softc *);

static int
uath_match(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != UATH_CONFIG_INDEX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != UATH_IFACE_INDEX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(uath_devs, sizeof(uath_devs), uaa));
}

static int
uath_attach(device_t dev)
{
	struct uath_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t bands[IEEE80211_MODE_BYTES];
	uint8_t iface_index = UATH_IFACE_INDEX;		/* XXX */
	usb_error_t error;

	sc->sc_dev = dev;
	sc->sc_udev = uaa->device;
#ifdef UATH_DEBUG
	sc->sc_debug = uath_debug;
#endif
	device_set_usb_desc(dev);

	/*
	 * Only post-firmware devices here.
	 */
	mtx_init(&sc->sc_mtx, device_get_nameunit(sc->sc_dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init(&sc->stat_ch, 0);
	callout_init_mtx(&sc->watchdog_ch, &sc->sc_mtx, 0);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	error = usbd_transfer_setup(uaa->device, &iface_index, sc->sc_xfer,
	    uath_usbconfig, UATH_N_XFERS, sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "could not allocate USB transfers, "
		    "err=%s\n", usbd_errstr(error));
		goto fail;
	}

	sc->sc_cmd_dma_buf = 
	    usbd_xfer_get_frame_buffer(sc->sc_xfer[UATH_INTR_TX], 0);
	sc->sc_tx_dma_buf = 
	    usbd_xfer_get_frame_buffer(sc->sc_xfer[UATH_BULK_TX], 0);

	/*
	 * Setup buffers for firmware commands.
	 */
	error = uath_alloc_cmd_list(sc, sc->sc_cmd);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate Tx command list\n");
		goto fail1;
	}

	/*
	 * We're now ready to send+receive firmware commands.
	 */
	UATH_LOCK(sc);
	error = uath_host_available(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not initialize adapter\n");
		goto fail2;
	}
	error = uath_get_devcap(sc);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not get device capabilities\n");
		goto fail2;
	}
	UATH_UNLOCK(sc);

	/* Create device sysctl node. */
	uath_sysctl_node(sc);

	UATH_LOCK(sc);
	error = uath_get_devstatus(sc, ic->ic_macaddr);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not get device status\n");
		goto fail2;
	}

	/*
	 * Allocate xfers for Rx/Tx data pipes.
	 */
	error = uath_alloc_rx_data_list(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate Rx data list\n");
		goto fail2;
	}
	error = uath_alloc_tx_data_list(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate Tx data list\n");
		goto fail2;
	}
	UATH_UNLOCK(sc);

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(dev);
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_STA |		/* station mode */
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_TXPMGT |	/* tx power management */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WPA |		/* 802.11i */
	    IEEE80211_C_BGSCAN |	/* capable of bg scanning */
	    IEEE80211_C_TXFRAG;		/* handle tx frags */

	/* put a regulatory domain to reveal informations.  */
	uath_regdomain = sc->sc_devcap.regDomain;

	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	if ((sc->sc_devcap.analog5GhzRevision & 0xf0) == 0x30)
		setbit(bands, IEEE80211_MODE_11A);
	/* XXX turbo */
	ieee80211_init_channels(ic, NULL, bands);

	ieee80211_ifattach(ic);
	ic->ic_raw_xmit = uath_raw_xmit;
	ic->ic_scan_start = uath_scan_start;
	ic->ic_scan_end = uath_scan_end;
	ic->ic_set_channel = uath_set_channel;
	ic->ic_vap_create = uath_vap_create;
	ic->ic_vap_delete = uath_vap_delete;
	ic->ic_update_mcast = uath_update_mcast;
	ic->ic_update_promisc = uath_update_promisc;
	ic->ic_transmit = uath_transmit;
	ic->ic_parent = uath_parent;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_txtap.wt_ihdr, sizeof(sc->sc_txtap),
		UATH_TX_RADIOTAP_PRESENT,
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
		UATH_RX_RADIOTAP_PRESENT);

	if (bootverbose)
		ieee80211_announce(ic);

	return (0);

fail2:	UATH_UNLOCK(sc);
	uath_free_cmd_list(sc, sc->sc_cmd);
fail1:	usbd_transfer_unsetup(sc->sc_xfer, UATH_N_XFERS);
fail:
	return (error);
}

static int
uath_detach(device_t dev)
{
	struct uath_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	unsigned int x;

	/*
	 * Prevent further allocations from RX/TX/CMD
	 * data lists and ioctls
	 */
	UATH_LOCK(sc);
	sc->sc_flags |= UATH_FLAG_INVALID;

	STAILQ_INIT(&sc->sc_rx_active);
	STAILQ_INIT(&sc->sc_rx_inactive);

	STAILQ_INIT(&sc->sc_tx_active);
	STAILQ_INIT(&sc->sc_tx_inactive);
	STAILQ_INIT(&sc->sc_tx_pending);

	STAILQ_INIT(&sc->sc_cmd_active);
	STAILQ_INIT(&sc->sc_cmd_pending);
	STAILQ_INIT(&sc->sc_cmd_waiting);
	STAILQ_INIT(&sc->sc_cmd_inactive);

	uath_stop(sc);
	UATH_UNLOCK(sc);

	callout_drain(&sc->stat_ch);
	callout_drain(&sc->watchdog_ch);

	/* drain USB transfers */
	for (x = 0; x != UATH_N_XFERS; x++)
		usbd_transfer_drain(sc->sc_xfer[x]);

	/* free data buffers */
	UATH_LOCK(sc);
	uath_free_rx_data_list(sc);
	uath_free_tx_data_list(sc);
	uath_free_cmd_list(sc, sc->sc_cmd);
	UATH_UNLOCK(sc);

	/* free USB transfers and some data buffers */
	usbd_transfer_unsetup(sc->sc_xfer, UATH_N_XFERS);

	ieee80211_ifdetach(ic);
	mbufq_drain(&sc->sc_snd);
	mtx_destroy(&sc->sc_mtx);
	return (0);
}

static void
uath_free_cmd_list(struct uath_softc *sc, struct uath_cmd cmds[])
{
	int i;

	for (i = 0; i != UATH_CMD_LIST_COUNT; i++)
		cmds[i].buf = NULL;
}

static int
uath_alloc_cmd_list(struct uath_softc *sc, struct uath_cmd cmds[])
{
	int i;

	STAILQ_INIT(&sc->sc_cmd_active);
	STAILQ_INIT(&sc->sc_cmd_pending);
	STAILQ_INIT(&sc->sc_cmd_waiting);
	STAILQ_INIT(&sc->sc_cmd_inactive);

	for (i = 0; i != UATH_CMD_LIST_COUNT; i++) {
		struct uath_cmd *cmd = &cmds[i];

		cmd->sc = sc;	/* backpointer for callbacks */
		cmd->msgid = i;
		cmd->buf = ((uint8_t *)sc->sc_cmd_dma_buf) +
		    (i * UATH_MAX_CMDSZ);
		STAILQ_INSERT_TAIL(&sc->sc_cmd_inactive, cmd, next);
		UATH_STAT_INC(sc, st_cmd_inactive);
	}
	return (0);
}

static int
uath_host_available(struct uath_softc *sc)
{
	struct uath_cmd_host_available setup;

	UATH_ASSERT_LOCKED(sc);

	/* inform target the host is available */
	setup.sw_ver_major = htobe32(ATH_SW_VER_MAJOR);
	setup.sw_ver_minor = htobe32(ATH_SW_VER_MINOR);
	setup.sw_ver_patch = htobe32(ATH_SW_VER_PATCH);
	setup.sw_ver_build = htobe32(ATH_SW_VER_BUILD);
	return uath_cmd_read(sc, WDCMSG_HOST_AVAILABLE,
		&setup, sizeof setup, NULL, 0, 0);
}

#ifdef UATH_DEBUG
static void
uath_dump_cmd(const uint8_t *buf, int len, char prefix)
{
	const char *sep = "";
	int i;

	for (i = 0; i < len; i++) {
		if ((i % 16) == 0) {
			printf("%s%c ", sep, prefix);
			sep = "\n";
		}
		else if ((i % 4) == 0)
			printf(" ");
		printf("%02x", buf[i]);
	}
	printf("\n");
}

static const char *
uath_codename(int code)
{
	static const char *names[] = {
	    "0x00",
	    "HOST_AVAILABLE",
	    "BIND",
	    "TARGET_RESET",
	    "TARGET_GET_CAPABILITY",
	    "TARGET_SET_CONFIG",
	    "TARGET_GET_STATUS",
	    "TARGET_GET_STATS",
	    "TARGET_START",
	    "TARGET_STOP",
	    "TARGET_ENABLE",
	    "TARGET_DISABLE",
	    "CREATE_CONNECTION",
	    "UPDATE_CONNECT_ATTR",
	    "DELETE_CONNECT",
	    "SEND",
	    "FLUSH",
	    "STATS_UPDATE",
	    "BMISS",
	    "DEVICE_AVAIL",
	    "SEND_COMPLETE",
	    "DATA_AVAIL",
	    "SET_PWR_MODE",
	    "BMISS_ACK",
	    "SET_LED_STEADY",
	    "SET_LED_BLINK",
	    "SETUP_BEACON_DESC",
	    "BEACON_INIT",
	    "RESET_KEY_CACHE",
	    "RESET_KEY_CACHE_ENTRY",
	    "SET_KEY_CACHE_ENTRY",
	    "SET_DECOMP_MASK",
	    "SET_REGULATORY_DOMAIN",
	    "SET_LED_STATE",
	    "WRITE_ASSOCID",
	    "SET_STA_BEACON_TIMERS",
	    "GET_TSF",
	    "RESET_TSF",
	    "SET_ADHOC_MODE",
	    "SET_BASIC_RATE",
	    "MIB_CONTROL",
	    "GET_CHANNEL_DATA",
	    "GET_CUR_RSSI",
	    "SET_ANTENNA_SWITCH",
	    "0x2c", "0x2d", "0x2e",
	    "USE_SHORT_SLOT_TIME",
	    "SET_POWER_MODE",
	    "SETUP_PSPOLL_DESC",
	    "SET_RX_MULTICAST_FILTER",
	    "RX_FILTER",
	    "PER_CALIBRATION",
	    "RESET",
	    "DISABLE",
	    "PHY_DISABLE",
	    "SET_TX_POWER_LIMIT",
	    "SET_TX_QUEUE_PARAMS",
	    "SETUP_TX_QUEUE",
	    "RELEASE_TX_QUEUE",
	};
	static char buf[8];

	if (code < nitems(names))
		return names[code];
	if (code == WDCMSG_SET_DEFAULT_KEY)
		return "SET_DEFAULT_KEY";
	snprintf(buf, sizeof(buf), "0x%02x", code);
	return buf;
}
#endif

/*
 * Low-level function to send read or write commands to the firmware.
 */
static int
uath_cmdsend(struct uath_softc *sc, uint32_t code, const void *idata, int ilen,
    void *odata, int olen, int flags)
{
	struct uath_cmd_hdr *hdr;
	struct uath_cmd *cmd;
	int error;

	UATH_ASSERT_LOCKED(sc);

	/* grab a xfer */
	cmd = uath_get_cmdbuf(sc);
	if (cmd == NULL) {
		device_printf(sc->sc_dev, "%s: empty inactive queue\n",
		    __func__);
		return (ENOBUFS);
	}
	cmd->flags = flags;
	/* always bulk-out a multiple of 4 bytes */
	cmd->buflen = roundup2(sizeof(struct uath_cmd_hdr) + ilen, 4);

	hdr = (struct uath_cmd_hdr *)cmd->buf;
	memset(hdr, 0, sizeof(struct uath_cmd_hdr));
	hdr->len   = htobe32(cmd->buflen);
	hdr->code  = htobe32(code);
	hdr->msgid = cmd->msgid;	/* don't care about endianness */
	hdr->magic = htobe32((cmd->flags & UATH_CMD_FLAG_MAGIC) ? 1 << 24 : 0);
	memcpy((uint8_t *)(hdr + 1), idata, ilen);

#ifdef UATH_DEBUG
	if (sc->sc_debug & UATH_DEBUG_CMDS) {
		printf("%s: send  %s [flags 0x%x] olen %d\n",
		    __func__, uath_codename(code), cmd->flags, olen);
		if (sc->sc_debug & UATH_DEBUG_CMDS_DUMP)
			uath_dump_cmd(cmd->buf, cmd->buflen, '+');
	}
#endif
	cmd->odata = odata;
	KASSERT(odata == NULL ||
	    olen < UATH_MAX_CMDSZ - sizeof(*hdr) + sizeof(uint32_t),
	    ("odata %p olen %u", odata, olen));
	cmd->olen = olen;

	STAILQ_INSERT_TAIL(&sc->sc_cmd_pending, cmd, next);
	UATH_STAT_INC(sc, st_cmd_pending);
	usbd_transfer_start(sc->sc_xfer[UATH_INTR_TX]);

	if (cmd->flags & UATH_CMD_FLAG_READ) {
		usbd_transfer_start(sc->sc_xfer[UATH_INTR_RX]);

		/* wait at most two seconds for command reply */
		error = mtx_sleep(cmd, &sc->sc_mtx, 0, "uathcmd", 2 * hz);
		cmd->odata = NULL;	/* in case reply comes too late */
		if (error != 0) {
			device_printf(sc->sc_dev, "timeout waiting for reply "
			    "to cmd 0x%x (%u)\n", code, code);
		} else if (cmd->olen != olen) {
			device_printf(sc->sc_dev, "unexpected reply data count "
			    "to cmd 0x%x (%u), got %u, expected %u\n",
			    code, code, cmd->olen, olen);
			error = EINVAL;
		}
		return (error);
	}
	return (0);
}

static int
uath_cmd_read(struct uath_softc *sc, uint32_t code, const void *idata,
    int ilen, void *odata, int olen, int flags)
{

	flags |= UATH_CMD_FLAG_READ;
	return uath_cmdsend(sc, code, idata, ilen, odata, olen, flags);
}

static int
uath_cmd_write(struct uath_softc *sc, uint32_t code, const void *data, int len,
    int flags)
{

	flags &= ~UATH_CMD_FLAG_READ;
	return uath_cmdsend(sc, code, data, len, NULL, 0, flags);
}

static struct uath_cmd *
uath_get_cmdbuf(struct uath_softc *sc)
{
	struct uath_cmd *uc;

	UATH_ASSERT_LOCKED(sc);

	uc = STAILQ_FIRST(&sc->sc_cmd_inactive);
	if (uc != NULL) {
		STAILQ_REMOVE_HEAD(&sc->sc_cmd_inactive, next);
		UATH_STAT_DEC(sc, st_cmd_inactive);
	} else
		uc = NULL;
	if (uc == NULL)
		DPRINTF(sc, UATH_DEBUG_XMIT, "%s: %s\n", __func__,
		    "out of command xmit buffers");
	return (uc);
}

/*
 * This function is called periodically (every second) when associated to
 * query device statistics.
 */
static void
uath_stat(void *arg)
{
	struct uath_softc *sc = arg;
	int error;

	UATH_LOCK(sc);
	/*
	 * Send request for statistics asynchronously. The timer will be
	 * restarted when we'll get the stats notification.
	 */
	error = uath_cmd_write(sc, WDCMSG_TARGET_GET_STATS, NULL, 0,
	    UATH_CMD_FLAG_ASYNC);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not query stats, error %d\n", error);
	}
	UATH_UNLOCK(sc);
}

static int
uath_get_capability(struct uath_softc *sc, uint32_t cap, uint32_t *val)
{
	int error;

	cap = htobe32(cap);
	error = uath_cmd_read(sc, WDCMSG_TARGET_GET_CAPABILITY,
	    &cap, sizeof cap, val, sizeof(uint32_t), UATH_CMD_FLAG_MAGIC);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not read capability %u\n",
		    be32toh(cap));
		return (error);
	}
	*val = be32toh(*val);
	return (error);
}

static int
uath_get_devcap(struct uath_softc *sc)
{
#define	GETCAP(x, v) do {				\
	error = uath_get_capability(sc, x, &v);		\
	if (error != 0)					\
		return (error);				\
	DPRINTF(sc, UATH_DEBUG_DEVCAP,			\
	    "%s: %s=0x%08x\n", __func__, #x, v);	\
} while (0)
	struct uath_devcap *cap = &sc->sc_devcap;
	int error;

	/* collect device capabilities */
	GETCAP(CAP_TARGET_VERSION, cap->targetVersion);
	GETCAP(CAP_TARGET_REVISION, cap->targetRevision);
	GETCAP(CAP_MAC_VERSION, cap->macVersion);
	GETCAP(CAP_MAC_REVISION, cap->macRevision);
	GETCAP(CAP_PHY_REVISION, cap->phyRevision);
	GETCAP(CAP_ANALOG_5GHz_REVISION, cap->analog5GhzRevision);
	GETCAP(CAP_ANALOG_2GHz_REVISION, cap->analog2GhzRevision);

	GETCAP(CAP_REG_DOMAIN, cap->regDomain);
	GETCAP(CAP_REG_CAP_BITS, cap->regCapBits);
#if 0
	/* NB: not supported in rev 1.5 */
	GETCAP(CAP_COUNTRY_CODE, cap->countryCode);
#endif
	GETCAP(CAP_WIRELESS_MODES, cap->wirelessModes);
	GETCAP(CAP_CHAN_SPREAD_SUPPORT, cap->chanSpreadSupport);
	GETCAP(CAP_COMPRESS_SUPPORT, cap->compressSupport);
	GETCAP(CAP_BURST_SUPPORT, cap->burstSupport);
	GETCAP(CAP_FAST_FRAMES_SUPPORT, cap->fastFramesSupport);
	GETCAP(CAP_CHAP_TUNING_SUPPORT, cap->chapTuningSupport);
	GETCAP(CAP_TURBOG_SUPPORT, cap->turboGSupport);
	GETCAP(CAP_TURBO_PRIME_SUPPORT, cap->turboPrimeSupport);
	GETCAP(CAP_DEVICE_TYPE, cap->deviceType);
	GETCAP(CAP_WME_SUPPORT, cap->wmeSupport);
	GETCAP(CAP_TOTAL_QUEUES, cap->numTxQueues);
	GETCAP(CAP_CONNECTION_ID_MAX, cap->connectionIdMax);

	GETCAP(CAP_LOW_5GHZ_CHAN, cap->low5GhzChan);
	GETCAP(CAP_HIGH_5GHZ_CHAN, cap->high5GhzChan);
	GETCAP(CAP_LOW_2GHZ_CHAN, cap->low2GhzChan);
	GETCAP(CAP_HIGH_2GHZ_CHAN, cap->high2GhzChan);
	GETCAP(CAP_TWICE_ANTENNAGAIN_5G, cap->twiceAntennaGain5G);
	GETCAP(CAP_TWICE_ANTENNAGAIN_2G, cap->twiceAntennaGain2G);

	GETCAP(CAP_CIPHER_AES_CCM, cap->supportCipherAES_CCM);
	GETCAP(CAP_CIPHER_TKIP, cap->supportCipherTKIP);
	GETCAP(CAP_MIC_TKIP, cap->supportMicTKIP);

	cap->supportCipherWEP = 1;	/* NB: always available */

	return (0);
}

static int
uath_get_devstatus(struct uath_softc *sc, uint8_t macaddr[IEEE80211_ADDR_LEN])
{
	int error;

	/* retrieve MAC address */
	error = uath_get_status(sc, ST_MAC_ADDR, macaddr, IEEE80211_ADDR_LEN);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not read MAC address\n");
		return (error);
	}

	error = uath_get_status(sc, ST_SERIAL_NUMBER,
	    &sc->sc_serial[0], sizeof(sc->sc_serial));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not read device serial number\n");
		return (error);
	}
	return (0);
}

static int
uath_get_status(struct uath_softc *sc, uint32_t which, void *odata, int olen)
{
	int error;

	which = htobe32(which);
	error = uath_cmd_read(sc, WDCMSG_TARGET_GET_STATUS,
	    &which, sizeof(which), odata, olen, UATH_CMD_FLAG_MAGIC);
	if (error != 0)
		device_printf(sc->sc_dev,
		    "could not read EEPROM offset 0x%02x\n", be32toh(which));
	return (error);
}

static void
uath_free_data_list(struct uath_softc *sc, struct uath_data data[], int ndata,
    int fillmbuf)
{
	int i;

	for (i = 0; i < ndata; i++) {
		struct uath_data *dp = &data[i];

		if (fillmbuf == 1) {
			if (dp->m != NULL) {
				m_freem(dp->m);
				dp->m = NULL;
				dp->buf = NULL;
			}
		} else {
			dp->buf = NULL;
		}
		if (dp->ni != NULL) {
			ieee80211_free_node(dp->ni);
			dp->ni = NULL;
		}
	}
}

static int
uath_alloc_data_list(struct uath_softc *sc, struct uath_data data[],
    int ndata, int maxsz, void *dma_buf)
{
	int i, error;

	for (i = 0; i < ndata; i++) {
		struct uath_data *dp = &data[i];

		dp->sc = sc;
		if (dma_buf == NULL) {
			/* XXX check maxsz */
			dp->m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
			if (dp->m == NULL) {
				device_printf(sc->sc_dev,
				    "could not allocate rx mbuf\n");
				error = ENOMEM;
				goto fail;
			}
			dp->buf = mtod(dp->m, uint8_t *);
		} else {
			dp->m = NULL;
			dp->buf = ((uint8_t *)dma_buf) + (i * maxsz);
		}
		dp->ni = NULL;
	}

	return (0);

fail:	uath_free_data_list(sc, data, ndata, 1 /* free mbufs */);
	return (error);
}

static int
uath_alloc_rx_data_list(struct uath_softc *sc)
{
	int error, i;

	/* XXX is it enough to store the RX packet with MCLBYTES bytes?  */
	error = uath_alloc_data_list(sc,
	    sc->sc_rx, UATH_RX_DATA_LIST_COUNT, MCLBYTES,
	    NULL /* setup mbufs */);
	if (error != 0)
		return (error);

	STAILQ_INIT(&sc->sc_rx_active);
	STAILQ_INIT(&sc->sc_rx_inactive);

	for (i = 0; i < UATH_RX_DATA_LIST_COUNT; i++) {
		STAILQ_INSERT_HEAD(&sc->sc_rx_inactive, &sc->sc_rx[i],
		    next);
		UATH_STAT_INC(sc, st_rx_inactive);
	}

	return (0);
}

static int
uath_alloc_tx_data_list(struct uath_softc *sc)
{
	int error, i;

	error = uath_alloc_data_list(sc,
	    sc->sc_tx, UATH_TX_DATA_LIST_COUNT, UATH_MAX_TXBUFSZ,
	    sc->sc_tx_dma_buf);
	if (error != 0)
		return (error);

	STAILQ_INIT(&sc->sc_tx_active);
	STAILQ_INIT(&sc->sc_tx_inactive);
	STAILQ_INIT(&sc->sc_tx_pending);

	for (i = 0; i < UATH_TX_DATA_LIST_COUNT; i++) {
		STAILQ_INSERT_HEAD(&sc->sc_tx_inactive, &sc->sc_tx[i],
		    next);
		UATH_STAT_INC(sc, st_tx_inactive);
	}

	return (0);
}

static void
uath_free_rx_data_list(struct uath_softc *sc)
{
	uath_free_data_list(sc, sc->sc_rx, UATH_RX_DATA_LIST_COUNT,
	    1 /* free mbufs */);
}

static void
uath_free_tx_data_list(struct uath_softc *sc)
{
	uath_free_data_list(sc, sc->sc_tx, UATH_TX_DATA_LIST_COUNT,
	    0 /* no mbufs */);
}

static struct ieee80211vap *
uath_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct uath_vap *uvp;
	struct ieee80211vap *vap;

	if (!TAILQ_EMPTY(&ic->ic_vaps))		/* only one at a time */
		return (NULL);
	uvp =  malloc(sizeof(struct uath_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &uvp->vap;
	/* enable s/w bmiss handling for sta mode */

	if (ieee80211_vap_setup(ic, vap, name, unit, opmode,
	    flags | IEEE80211_CLONE_NOBEACONS, bssid) != 0) {
		/* out of memory */
		free(uvp, M_80211_VAP);
		return (NULL);
	}

	/* override state transition machine */
	uvp->newstate = vap->iv_newstate;
	vap->iv_newstate = uath_newstate;

	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change,
	    ieee80211_media_status, mac);
	ic->ic_opmode = opmode;
	return (vap);
}

static void
uath_vap_delete(struct ieee80211vap *vap)
{
	struct uath_vap *uvp = UATH_VAP(vap);

	ieee80211_vap_detach(vap);
	free(uvp, M_80211_VAP);
}

static int
uath_init(struct uath_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t val;
	int error;

	UATH_ASSERT_LOCKED(sc);

	if (sc->sc_flags & UATH_FLAG_INITDONE)
		uath_stop(sc);

	/* reset variables */
	sc->sc_intrx_nextnum = sc->sc_msgid = 0;

	val = htobe32(0);
	uath_cmd_write(sc, WDCMSG_BIND, &val, sizeof val, 0);

	/* set MAC address */
	uath_config_multi(sc, CFG_MAC_ADDR,
	    vap ? vap->iv_myaddr : ic->ic_macaddr, IEEE80211_ADDR_LEN);

	/* XXX honor net80211 state */
	uath_config(sc, CFG_RATE_CONTROL_ENABLE, 0x00000001);
	uath_config(sc, CFG_DIVERSITY_CTL, 0x00000001);
	uath_config(sc, CFG_ABOLT, 0x0000003f);
	uath_config(sc, CFG_WME_ENABLED, 0x00000001);

	uath_config(sc, CFG_SERVICE_TYPE, 1);
	uath_config(sc, CFG_TP_SCALE, 0x00000000);
	uath_config(sc, CFG_TPC_HALF_DBM5, 0x0000003c);
	uath_config(sc, CFG_TPC_HALF_DBM2, 0x0000003c);
	uath_config(sc, CFG_OVERRD_TX_POWER, 0x00000000);
	uath_config(sc, CFG_GMODE_PROTECTION, 0x00000000);
	uath_config(sc, CFG_GMODE_PROTECT_RATE_INDEX, 0x00000003);
	uath_config(sc, CFG_PROTECTION_TYPE, 0x00000000);
	uath_config(sc, CFG_MODE_CTS, 0x00000002);

	error = uath_cmd_read(sc, WDCMSG_TARGET_START, NULL, 0,
	    &val, sizeof(val), UATH_CMD_FLAG_MAGIC);
	if (error) {
		device_printf(sc->sc_dev,
		    "could not start target, error %d\n", error);
		goto fail;
	}
	DPRINTF(sc, UATH_DEBUG_INIT, "%s returns handle: 0x%x\n",
	    uath_codename(WDCMSG_TARGET_START), be32toh(val));

	/* set default channel */
	error = uath_switch_channel(sc, ic->ic_curchan);
	if (error) {
		device_printf(sc->sc_dev,
		    "could not switch channel, error %d\n", error);
		goto fail;
	}

	val = htobe32(TARGET_DEVICE_AWAKE);
	uath_cmd_write(sc, WDCMSG_SET_PWR_MODE, &val, sizeof val, 0);
	/* XXX? check */
	uath_cmd_write(sc, WDCMSG_RESET_KEY_CACHE, NULL, 0, 0);

	usbd_transfer_start(sc->sc_xfer[UATH_BULK_RX]);
	/* enable Rx */
	uath_set_rxfilter(sc, 0x0, UATH_FILTER_OP_INIT);
	uath_set_rxfilter(sc,
	    UATH_FILTER_RX_UCAST | UATH_FILTER_RX_MCAST |
	    UATH_FILTER_RX_BCAST | UATH_FILTER_RX_BEACON,
	    UATH_FILTER_OP_SET);

	sc->sc_flags |= UATH_FLAG_INITDONE;

	callout_reset(&sc->watchdog_ch, hz, uath_watchdog, sc);

	return (0);

fail:
	uath_stop(sc);
	return (error);
}

static void
uath_stop(struct uath_softc *sc)
{

	UATH_ASSERT_LOCKED(sc);

	sc->sc_flags &= ~UATH_FLAG_INITDONE;

	callout_stop(&sc->stat_ch);
	callout_stop(&sc->watchdog_ch);
	sc->sc_tx_timer = 0;
	/* abort pending transmits  */
	uath_abort_xfers(sc);
	/* flush data & control requests into the target  */
	(void)uath_flush(sc);
	/* set a LED status to the disconnected.  */
	uath_set_ledstate(sc, 0);
	/* stop the target  */
	uath_cmd_write(sc, WDCMSG_TARGET_STOP, NULL, 0, 0);
}

static int
uath_config(struct uath_softc *sc, uint32_t reg, uint32_t val)
{
	struct uath_write_mac write;
	int error;

	write.reg = htobe32(reg);
	write.len = htobe32(0);	/* 0 = single write */
	*(uint32_t *)write.data = htobe32(val);

	error = uath_cmd_write(sc, WDCMSG_TARGET_SET_CONFIG, &write,
	    3 * sizeof (uint32_t), 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not write register 0x%02x\n",
		    reg);
	}
	return (error);
}

static int
uath_config_multi(struct uath_softc *sc, uint32_t reg, const void *data,
    int len)
{
	struct uath_write_mac write;
	int error;

	write.reg = htobe32(reg);
	write.len = htobe32(len);
	bcopy(data, write.data, len);

	/* properly handle the case where len is zero (reset) */
	error = uath_cmd_write(sc, WDCMSG_TARGET_SET_CONFIG, &write,
	    (len == 0) ? sizeof (uint32_t) : 2 * sizeof (uint32_t) + len, 0);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not write %d bytes to register 0x%02x\n", len, reg);
	}
	return (error);
}

static int
uath_switch_channel(struct uath_softc *sc, struct ieee80211_channel *c)
{
	int error;

	UATH_ASSERT_LOCKED(sc);

	/* set radio frequency */
	error = uath_set_chan(sc, c);
	if (error) {
		device_printf(sc->sc_dev,
		    "could not set channel, error %d\n", error);
		goto failed;
	}
	/* reset Tx rings */
	error = uath_reset_tx_queues(sc);
	if (error) {
		device_printf(sc->sc_dev,
		    "could not reset Tx queues, error %d\n", error);
		goto failed;
	}
	/* set Tx rings WME properties */
	error = uath_wme_init(sc);
	if (error) {
		device_printf(sc->sc_dev,
		    "could not init Tx queues, error %d\n", error);
		goto failed;
	}
	error = uath_set_ledstate(sc, 0);
	if (error) {
		device_printf(sc->sc_dev,
		    "could not set led state, error %d\n", error);
		goto failed;
	}
	error = uath_flush(sc);
	if (error) {
		device_printf(sc->sc_dev,
		    "could not flush pipes, error %d\n", error);
		goto failed;
	}
failed:
	return (error);
}

static int
uath_set_rxfilter(struct uath_softc *sc, uint32_t bits, uint32_t op)
{
	struct uath_cmd_rx_filter rxfilter;

	rxfilter.bits = htobe32(bits);
	rxfilter.op = htobe32(op);

	DPRINTF(sc, UATH_DEBUG_RECV | UATH_DEBUG_RECV_ALL,
	    "setting Rx filter=0x%x flags=0x%x\n", bits, op);
	return uath_cmd_write(sc, WDCMSG_RX_FILTER, &rxfilter,
	    sizeof rxfilter, 0);
}

static void
uath_watchdog(void *arg)
{
	struct uath_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			device_printf(sc->sc_dev, "device timeout\n");
			/*uath_init(sc); XXX needs a process context! */
			counter_u64_add(ic->ic_oerrors, 1);
			return;
		}
		callout_reset(&sc->watchdog_ch, hz, uath_watchdog, sc);
	}
}

static void
uath_abort_xfers(struct uath_softc *sc)
{
	int i;

	UATH_ASSERT_LOCKED(sc);
	/* abort any pending transfers */
	for (i = 0; i < UATH_N_XFERS; i++)
		usbd_transfer_stop(sc->sc_xfer[i]);
}

static int
uath_flush(struct uath_softc *sc)
{
	int error;

	error = uath_dataflush(sc);
	if (error != 0)
		goto failed;

	error = uath_cmdflush(sc);
	if (error != 0)
		goto failed;

failed:
	return (error);
}

static int
uath_cmdflush(struct uath_softc *sc)
{

	return uath_cmd_write(sc, WDCMSG_FLUSH, NULL, 0, 0);
}

static int
uath_dataflush(struct uath_softc *sc)
{
	struct uath_data *data;
	struct uath_chunk *chunk;
	struct uath_tx_desc *desc;

	UATH_ASSERT_LOCKED(sc);

	data = uath_getbuf(sc);
	if (data == NULL)
		return (ENOBUFS);
	data->buflen = sizeof(struct uath_chunk) + sizeof(struct uath_tx_desc);
	data->m = NULL;
	data->ni = NULL;
	chunk = (struct uath_chunk *)data->buf;
	desc = (struct uath_tx_desc *)(chunk + 1);

	/* one chunk only */
	chunk->seqnum = 0;
	chunk->flags = UATH_CFLAGS_FINAL;
	chunk->length = htobe16(sizeof (struct uath_tx_desc));

	memset(desc, 0, sizeof(struct uath_tx_desc));
	desc->msglen = htobe32(sizeof(struct uath_tx_desc));
	desc->msgid  = (sc->sc_msgid++) + 1; /* don't care about endianness */
	desc->type   = htobe32(WDCMSG_FLUSH);
	desc->txqid  = htobe32(0);
	desc->connid = htobe32(0);
	desc->flags  = htobe32(0);

#ifdef UATH_DEBUG
	if (sc->sc_debug & UATH_DEBUG_CMDS) {
		DPRINTF(sc, UATH_DEBUG_RESET, "send flush ix %d\n",
		    desc->msgid);
		if (sc->sc_debug & UATH_DEBUG_CMDS_DUMP)
			uath_dump_cmd(data->buf, data->buflen, '+');
	}
#endif

	STAILQ_INSERT_TAIL(&sc->sc_tx_pending, data, next);
	UATH_STAT_INC(sc, st_tx_pending);
	sc->sc_tx_timer = 5;
	usbd_transfer_start(sc->sc_xfer[UATH_BULK_TX]);

	return (0);
}

static struct uath_data *
_uath_getbuf(struct uath_softc *sc)
{
	struct uath_data *bf;

	bf = STAILQ_FIRST(&sc->sc_tx_inactive);
	if (bf != NULL) {
		STAILQ_REMOVE_HEAD(&sc->sc_tx_inactive, next);
		UATH_STAT_DEC(sc, st_tx_inactive);
	} else
		bf = NULL;
	if (bf == NULL)
		DPRINTF(sc, UATH_DEBUG_XMIT, "%s: %s\n", __func__,
		    "out of xmit buffers");
	return (bf);
}

static struct uath_data *
uath_getbuf(struct uath_softc *sc)
{
	struct uath_data *bf;

	UATH_ASSERT_LOCKED(sc);

	bf = _uath_getbuf(sc);
	if (bf == NULL)
		DPRINTF(sc, UATH_DEBUG_XMIT, "%s: stop queue\n", __func__);
	return (bf);
}

static int
uath_set_ledstate(struct uath_softc *sc, int connected)
{

	DPRINTF(sc, UATH_DEBUG_LED,
	    "set led state %sconnected\n", connected ? "" : "!");
	connected = htobe32(connected);
	return uath_cmd_write(sc, WDCMSG_SET_LED_STATE,
	     &connected, sizeof connected, 0);
}

static int
uath_set_chan(struct uath_softc *sc, struct ieee80211_channel *c)
{
#ifdef UATH_DEBUG
	struct ieee80211com *ic = &sc->sc_ic;
#endif
	struct uath_cmd_reset reset;

	memset(&reset, 0, sizeof(reset));
	if (IEEE80211_IS_CHAN_2GHZ(c))
		reset.flags |= htobe32(UATH_CHAN_2GHZ);
	if (IEEE80211_IS_CHAN_5GHZ(c))
		reset.flags |= htobe32(UATH_CHAN_5GHZ);
	/* NB: 11g =>'s 11b so don't specify both OFDM and CCK */
	if (IEEE80211_IS_CHAN_OFDM(c))
		reset.flags |= htobe32(UATH_CHAN_OFDM);
	else if (IEEE80211_IS_CHAN_CCK(c))
		reset.flags |= htobe32(UATH_CHAN_CCK);
	/* turbo can be used in either 2GHz or 5GHz */
	if (c->ic_flags & IEEE80211_CHAN_TURBO)
		reset.flags |= htobe32(UATH_CHAN_TURBO);
	reset.freq = htobe32(c->ic_freq);
	reset.maxrdpower = htobe32(50);	/* XXX */
	reset.channelchange = htobe32(1);
	reset.keeprccontent = htobe32(0);

	DPRINTF(sc, UATH_DEBUG_CHANNEL, "set channel %d, flags 0x%x freq %u\n",
	    ieee80211_chan2ieee(ic, c),
	    be32toh(reset.flags), be32toh(reset.freq));
	return uath_cmd_write(sc, WDCMSG_RESET, &reset, sizeof reset, 0);
}

static int
uath_reset_tx_queues(struct uath_softc *sc)
{
	int ac, error;

	DPRINTF(sc, UATH_DEBUG_RESET, "%s: reset Tx queues\n", __func__);
	for (ac = 0; ac < 4; ac++) {
		const uint32_t qid = htobe32(ac);

		error = uath_cmd_write(sc, WDCMSG_RELEASE_TX_QUEUE, &qid,
		    sizeof qid, 0);
		if (error != 0)
			break;
	}
	return (error);
}

static int
uath_wme_init(struct uath_softc *sc)
{
	/* XXX get from net80211 */
	static const struct uath_wme_settings uath_wme_11g[4] = {
		{ 7, 4, 10,  0, 0 },	/* Background */
		{ 3, 4, 10,  0, 0 },	/* Best-Effort */
		{ 3, 3,  4, 26, 0 },	/* Video */
		{ 2, 2,  3, 47, 0 }	/* Voice */
	};
	struct uath_cmd_txq_setup qinfo;
	int ac, error;

	DPRINTF(sc, UATH_DEBUG_WME, "%s: setup Tx queues\n", __func__);
	for (ac = 0; ac < 4; ac++) {
		qinfo.qid		= htobe32(ac);
		qinfo.len		= htobe32(sizeof(qinfo.attr));
		qinfo.attr.priority	= htobe32(ac);	/* XXX */
		qinfo.attr.aifs		= htobe32(uath_wme_11g[ac].aifsn);
		qinfo.attr.logcwmin	= htobe32(uath_wme_11g[ac].logcwmin);
		qinfo.attr.logcwmax	= htobe32(uath_wme_11g[ac].logcwmax);
		qinfo.attr.bursttime	= htobe32(IEEE80211_TXOP_TO_US(
					    uath_wme_11g[ac].txop));
		qinfo.attr.mode		= htobe32(uath_wme_11g[ac].acm);/*XXX? */
		qinfo.attr.qflags	= htobe32(1);	/* XXX? */

		error = uath_cmd_write(sc, WDCMSG_SETUP_TX_QUEUE, &qinfo,
		    sizeof qinfo, 0);
		if (error != 0)
			break;
	}
	return (error);
}

static void
uath_parent(struct ieee80211com *ic)
{
	struct uath_softc *sc = ic->ic_softc;
	int startall = 0;

	UATH_LOCK(sc);
	if (sc->sc_flags & UATH_FLAG_INVALID) {
		UATH_UNLOCK(sc);
		return;
	}

	if (ic->ic_nrunning > 0) {
		if (!(sc->sc_flags & UATH_FLAG_INITDONE)) {
			uath_init(sc);
			startall = 1;
		}
	} else if (sc->sc_flags & UATH_FLAG_INITDONE)
		uath_stop(sc);
	UATH_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

static int
uath_tx_start(struct uath_softc *sc, struct mbuf *m0, struct ieee80211_node *ni,
    struct uath_data *data)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct uath_chunk *chunk;
	struct uath_tx_desc *desc;
	const struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	int framelen, msglen;

	UATH_ASSERT_LOCKED(sc);

	data->ni = ni;
	data->m = m0;
	chunk = (struct uath_chunk *)data->buf;
	desc = (struct uath_tx_desc *)(chunk + 1);

	if (ieee80211_radiotap_active_vap(vap)) {
		struct uath_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		if (m0->m_flags & M_FRAG)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_FRAG;

		ieee80211_radiotap_tx(vap, m0);
	}

	wh = mtod(m0, struct ieee80211_frame *);
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			m_freem(m0);
			return (ENOBUFS);
		}

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}
	m_copydata(m0, 0, m0->m_pkthdr.len, (uint8_t *)(desc + 1));

	framelen = m0->m_pkthdr.len + IEEE80211_CRC_LEN;
	msglen = framelen + sizeof (struct uath_tx_desc);
	data->buflen = msglen + sizeof (struct uath_chunk);

	/* one chunk only for now */
	chunk->seqnum = sc->sc_seqnum++;
	chunk->flags = (m0->m_flags & M_FRAG) ? 0 : UATH_CFLAGS_FINAL;
	if (m0->m_flags & M_LASTFRAG)
		chunk->flags |= UATH_CFLAGS_FINAL;
	chunk->flags = UATH_CFLAGS_FINAL;
	chunk->length = htobe16(msglen);

	/* fill Tx descriptor */
	desc->msglen = htobe32(msglen);
	/* NB: to get UATH_TX_NOTIFY reply, `msgid' must be larger than 0  */
	desc->msgid  = (sc->sc_msgid++) + 1; /* don't care about endianness */
	desc->type   = htobe32(WDCMSG_SEND);
	switch (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
	case IEEE80211_FC0_TYPE_CTL:
	case IEEE80211_FC0_TYPE_MGT:
		/* NB: force all management frames to highest queue */
		if (ni->ni_flags & IEEE80211_NODE_QOS) {
			/* NB: force all management frames to highest queue */
			desc->txqid = htobe32(WME_AC_VO | UATH_TXQID_MINRATE);
		} else
			desc->txqid = htobe32(WME_AC_BE | UATH_TXQID_MINRATE);
		break;
	case IEEE80211_FC0_TYPE_DATA:
		/* XXX multicast frames should honor mcastrate */
		desc->txqid = htobe32(M_WME_GETAC(m0));
		break;
	default:
		device_printf(sc->sc_dev, "bogus frame type 0x%x (%s)\n",
			wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK, __func__);
		m_freem(m0);
		return (EIO);
	}
	if (vap->iv_state == IEEE80211_S_AUTH ||
	    vap->iv_state == IEEE80211_S_ASSOC ||
	    vap->iv_state == IEEE80211_S_RUN)
		desc->connid = htobe32(UATH_ID_BSS);
	else
		desc->connid = htobe32(UATH_ID_INVALID);
	desc->flags  = htobe32(0 /* no UATH_TX_NOTIFY */);
	desc->buflen = htobe32(m0->m_pkthdr.len);

#ifdef UATH_DEBUG
	DPRINTF(sc, UATH_DEBUG_XMIT,
	    "send frame ix %u framelen %d msglen %d connid 0x%x txqid 0x%x\n",
	    desc->msgid, framelen, msglen, be32toh(desc->connid),
	    be32toh(desc->txqid));
	if (sc->sc_debug & UATH_DEBUG_XMIT_DUMP)
		uath_dump_cmd(data->buf, data->buflen, '+');
#endif

	STAILQ_INSERT_TAIL(&sc->sc_tx_pending, data, next);
	UATH_STAT_INC(sc, st_tx_pending);
	usbd_transfer_start(sc->sc_xfer[UATH_BULK_TX]);

	return (0);
}

/*
 * Cleanup driver resources when we run out of buffers while processing
 * fragments; return the tx buffers allocated and drop node references.
 */
static void
uath_txfrag_cleanup(struct uath_softc *sc,
    uath_datahead *frags, struct ieee80211_node *ni)
{
	struct uath_data *bf, *next;

	UATH_ASSERT_LOCKED(sc);

	STAILQ_FOREACH_SAFE(bf, frags, next, next) {
		/* NB: bf assumed clean */
		STAILQ_REMOVE_HEAD(frags, next);
		STAILQ_INSERT_HEAD(&sc->sc_tx_inactive, bf, next);
		UATH_STAT_INC(sc, st_tx_inactive);
		ieee80211_node_decref(ni);
	}
}

/*
 * Setup xmit of a fragmented frame.  Allocate a buffer for each frag and bump
 * the node reference count to reflect the held reference to be setup by
 * uath_tx_start.
 */
static int
uath_txfrag_setup(struct uath_softc *sc, uath_datahead *frags,
    struct mbuf *m0, struct ieee80211_node *ni)
{
	struct mbuf *m;
	struct uath_data *bf;

	UATH_ASSERT_LOCKED(sc);
	for (m = m0->m_nextpkt; m != NULL; m = m->m_nextpkt) {
		bf = uath_getbuf(sc);
		if (bf == NULL) {       /* out of buffers, cleanup */
			uath_txfrag_cleanup(sc, frags, ni);
			break;
		}
		ieee80211_node_incref(ni);
		STAILQ_INSERT_TAIL(frags, bf, next);
	}

	return !STAILQ_EMPTY(frags);
}

static int
uath_transmit(struct ieee80211com *ic, struct mbuf *m)   
{
	struct uath_softc *sc = ic->ic_softc;
	int error;

	UATH_LOCK(sc);
	if ((sc->sc_flags & UATH_FLAG_INITDONE) == 0) {
		UATH_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		UATH_UNLOCK(sc);
		return (error);
	}
	uath_start(sc);
	UATH_UNLOCK(sc);

	return (0);
}

static void
uath_start(struct uath_softc *sc)
{
	struct uath_data *bf;
	struct ieee80211_node *ni;
	struct mbuf *m, *next;
	uath_datahead frags;

	UATH_ASSERT_LOCKED(sc);

	if ((sc->sc_flags & UATH_FLAG_INITDONE) == 0 ||
	    (sc->sc_flags & UATH_FLAG_INVALID))
		return;

	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		bf = uath_getbuf(sc);
		if (bf == NULL) {
			mbufq_prepend(&sc->sc_snd, m);
			break;
		}

		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		m->m_pkthdr.rcvif = NULL;

		/*
		 * Check for fragmentation.  If this frame has been broken up
		 * verify we have enough buffers to send all the fragments
		 * so all go out or none...
		 */
		STAILQ_INIT(&frags);
		if ((m->m_flags & M_FRAG) && 
		    !uath_txfrag_setup(sc, &frags, m, ni)) {
			DPRINTF(sc, UATH_DEBUG_XMIT,
			    "%s: out of txfrag buffers\n", __func__);
			ieee80211_free_mbuf(m);
			goto bad;
		}
		sc->sc_seqnum = 0;
	nextfrag:
		/*
		 * Pass the frame to the h/w for transmission.
		 * Fragmented frames have each frag chained together
		 * with m_nextpkt.  We know there are sufficient uath_data's
		 * to send all the frags because of work done by
		 * uath_txfrag_setup.
		 */
		next = m->m_nextpkt;
		if (uath_tx_start(sc, m, ni, bf) != 0) {
	bad:
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
	reclaim:
			STAILQ_INSERT_HEAD(&sc->sc_tx_inactive, bf, next);
			UATH_STAT_INC(sc, st_tx_inactive);
			uath_txfrag_cleanup(sc, &frags, ni);
			ieee80211_free_node(ni);
			continue;
		}

		if (next != NULL) {
			/*
			 * Beware of state changing between frags.
			 XXX check sta power-save state?
			*/
			if (ni->ni_vap->iv_state != IEEE80211_S_RUN) {
				DPRINTF(sc, UATH_DEBUG_XMIT,
				    "%s: flush fragmented packet, state %s\n",
				    __func__,
				    ieee80211_state_name[ni->ni_vap->iv_state]);
				ieee80211_free_mbuf(next);
				goto reclaim;
			}
			m = next;
			bf = STAILQ_FIRST(&frags);
			KASSERT(bf != NULL, ("no buf for txfrag"));
			STAILQ_REMOVE_HEAD(&frags, next);
			goto nextfrag;
		}

		sc->sc_tx_timer = 5;
	}
}

static int
uath_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct uath_data *bf;
	struct uath_softc *sc = ic->ic_softc;

	UATH_LOCK(sc);
	/* prevent management frames from being sent if we're not ready */
	if ((sc->sc_flags & UATH_FLAG_INVALID) ||
	    !(sc->sc_flags & UATH_FLAG_INITDONE)) {
		m_freem(m);
		UATH_UNLOCK(sc);
		return (ENETDOWN);
	}

	/* grab a TX buffer  */
	bf = uath_getbuf(sc);
	if (bf == NULL) {
		m_freem(m);
		UATH_UNLOCK(sc);
		return (ENOBUFS);
	}

	sc->sc_seqnum = 0;
	if (uath_tx_start(sc, m, ni, bf) != 0) {
		STAILQ_INSERT_HEAD(&sc->sc_tx_inactive, bf, next);
		UATH_STAT_INC(sc, st_tx_inactive);
		UATH_UNLOCK(sc);
		return (EIO);
	}
	UATH_UNLOCK(sc);

	sc->sc_tx_timer = 5;
	return (0);
}

static void
uath_scan_start(struct ieee80211com *ic)
{
	/* do nothing  */
}

static void
uath_scan_end(struct ieee80211com *ic)
{
	/* do nothing  */
}

static void
uath_set_channel(struct ieee80211com *ic)
{
	struct uath_softc *sc = ic->ic_softc;

	UATH_LOCK(sc);
	if ((sc->sc_flags & UATH_FLAG_INVALID) ||
	    (sc->sc_flags & UATH_FLAG_INITDONE) == 0) {
		UATH_UNLOCK(sc);
		return;
	}
	(void)uath_switch_channel(sc, ic->ic_curchan);
	UATH_UNLOCK(sc);
}

static int
uath_set_rxmulti_filter(struct uath_softc *sc)
{
	/* XXX broken */
	return (0);
}
static void
uath_update_mcast(struct ieee80211com *ic)
{
	struct uath_softc *sc = ic->ic_softc;

	UATH_LOCK(sc);
	if ((sc->sc_flags & UATH_FLAG_INVALID) ||
	    (sc->sc_flags & UATH_FLAG_INITDONE) == 0) {
		UATH_UNLOCK(sc);
		return;
	}
	/*
	 * this is for avoiding the race condition when we're try to
	 * connect to the AP with WPA.
	 */
	if (sc->sc_flags & UATH_FLAG_INITDONE)
		(void)uath_set_rxmulti_filter(sc);
	UATH_UNLOCK(sc);
}

static void
uath_update_promisc(struct ieee80211com *ic)
{
	struct uath_softc *sc = ic->ic_softc;

	UATH_LOCK(sc);
	if ((sc->sc_flags & UATH_FLAG_INVALID) ||
	    (sc->sc_flags & UATH_FLAG_INITDONE) == 0) {
		UATH_UNLOCK(sc);
		return;
	}
	if (sc->sc_flags & UATH_FLAG_INITDONE) {
		uath_set_rxfilter(sc,
		    UATH_FILTER_RX_UCAST | UATH_FILTER_RX_MCAST |
		    UATH_FILTER_RX_BCAST | UATH_FILTER_RX_BEACON |
		    UATH_FILTER_RX_PROM, UATH_FILTER_OP_SET);
	}
	UATH_UNLOCK(sc);
}

static int
uath_create_connection(struct uath_softc *sc, uint32_t connid)
{
	const struct ieee80211_rateset *rs;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct ieee80211_node *ni;
	struct uath_cmd_create_connection create;

	ni = ieee80211_ref_node(vap->iv_bss);
	memset(&create, 0, sizeof(create));
	create.connid = htobe32(connid);
	create.bssid = htobe32(0);
	/* XXX packed or not?  */
	create.size = htobe32(sizeof(struct uath_cmd_rateset));

	rs = &ni->ni_rates;
	create.connattr.rateset.length = rs->rs_nrates;
	bcopy(rs->rs_rates, &create.connattr.rateset.set[0],
	    rs->rs_nrates);

	/* XXX turbo */
	if (IEEE80211_IS_CHAN_A(ni->ni_chan))
		create.connattr.wlanmode = htobe32(WLAN_MODE_11a);
	else if (IEEE80211_IS_CHAN_ANYG(ni->ni_chan))
		create.connattr.wlanmode = htobe32(WLAN_MODE_11g);
	else
		create.connattr.wlanmode = htobe32(WLAN_MODE_11b);
	ieee80211_free_node(ni);

	return uath_cmd_write(sc, WDCMSG_CREATE_CONNECTION, &create,
	    sizeof create, 0);
}

static int
uath_set_rates(struct uath_softc *sc, const struct ieee80211_rateset *rs)
{
	struct uath_cmd_rates rates;

	memset(&rates, 0, sizeof(rates));
	rates.connid = htobe32(UATH_ID_BSS);		/* XXX */
	rates.size   = htobe32(sizeof(struct uath_cmd_rateset));
	/* XXX bounds check rs->rs_nrates */
	rates.rateset.length = rs->rs_nrates;
	bcopy(rs->rs_rates, &rates.rateset.set[0], rs->rs_nrates);

	DPRINTF(sc, UATH_DEBUG_RATES,
	    "setting supported rates nrates=%d\n", rs->rs_nrates);
	return uath_cmd_write(sc, WDCMSG_SET_BASIC_RATE,
	    &rates, sizeof rates, 0);
}

static int
uath_write_associd(struct uath_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct ieee80211_node *ni;
	struct uath_cmd_set_associd associd;

	ni = ieee80211_ref_node(vap->iv_bss);
	memset(&associd, 0, sizeof(associd));
	associd.defaultrateix = htobe32(1);	/* XXX */
	associd.associd = htobe32(ni->ni_associd);
	associd.timoffset = htobe32(0x3b);	/* XXX */
	IEEE80211_ADDR_COPY(associd.bssid, ni->ni_bssid);
	ieee80211_free_node(ni);
	return uath_cmd_write(sc, WDCMSG_WRITE_ASSOCID, &associd,
	    sizeof associd, 0);
}

static int
uath_set_ledsteady(struct uath_softc *sc, int lednum, int ledmode)
{
	struct uath_cmd_ledsteady led;

	led.lednum = htobe32(lednum);
	led.ledmode = htobe32(ledmode);

	DPRINTF(sc, UATH_DEBUG_LED, "set %s led %s (steady)\n",
	    (lednum == UATH_LED_LINK) ? "link" : "activity",
	    ledmode ? "on" : "off");
	return uath_cmd_write(sc, WDCMSG_SET_LED_STEADY, &led, sizeof led, 0);
}

static int
uath_set_ledblink(struct uath_softc *sc, int lednum, int ledmode,
	int blinkrate, int slowmode)
{
	struct uath_cmd_ledblink led;

	led.lednum = htobe32(lednum);
	led.ledmode = htobe32(ledmode);
	led.blinkrate = htobe32(blinkrate);
	led.slowmode = htobe32(slowmode);

	DPRINTF(sc, UATH_DEBUG_LED, "set %s led %s (blink)\n",
	    (lednum == UATH_LED_LINK) ? "link" : "activity",
	    ledmode ? "on" : "off");
	return uath_cmd_write(sc, WDCMSG_SET_LED_BLINK, &led, sizeof led, 0);
}

static int
uath_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	enum ieee80211_state ostate = vap->iv_state;
	int error;
	struct ieee80211_node *ni;
	struct ieee80211com *ic = vap->iv_ic;
	struct uath_softc *sc = ic->ic_softc;
	struct uath_vap *uvp = UATH_VAP(vap);

	DPRINTF(sc, UATH_DEBUG_STATE,
	    "%s: %s -> %s\n", __func__, ieee80211_state_name[vap->iv_state],
	    ieee80211_state_name[nstate]);

	IEEE80211_UNLOCK(ic);
	UATH_LOCK(sc);
	callout_stop(&sc->stat_ch);
	callout_stop(&sc->watchdog_ch);
	ni = ieee80211_ref_node(vap->iv_bss);

	switch (nstate) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* turn link and activity LEDs off */
			uath_set_ledstate(sc, 0);
		}
		break;

	case IEEE80211_S_SCAN:
		break;

	case IEEE80211_S_AUTH:
		/* XXX good place?  set RTS threshold  */
		uath_config(sc, CFG_USER_RTS_THRESHOLD, vap->iv_rtsthreshold);
		/* XXX bad place  */
		error = uath_set_keys(sc, vap);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not set crypto keys, error %d\n", error);
			break;
		}
		if (uath_switch_channel(sc, ni->ni_chan) != 0) {
			device_printf(sc->sc_dev, "could not switch channel\n");
			break;
		}
		if (uath_create_connection(sc, UATH_ID_BSS) != 0) {
			device_printf(sc->sc_dev,
			    "could not create connection\n");
			break;
		}
		break;

	case IEEE80211_S_ASSOC:
		if (uath_set_rates(sc, &ni->ni_rates) != 0) {
			device_printf(sc->sc_dev,
			    "could not set negotiated rate set\n");
			break;
		}
		break;

	case IEEE80211_S_RUN:
		/* XXX monitor mode doesn't be tested  */
		if (ic->ic_opmode == IEEE80211_M_MONITOR) {
			uath_set_ledstate(sc, 1);
			break;
		}

		/*
		 * Tx rate is controlled by firmware, report the maximum
		 * negotiated rate in ifconfig output.
		 */
		ni->ni_txrate = ni->ni_rates.rs_rates[ni->ni_rates.rs_nrates-1];

		if (uath_write_associd(sc) != 0) {
			device_printf(sc->sc_dev,
			    "could not write association id\n");
			break;
		}
		/* turn link LED on */
		uath_set_ledsteady(sc, UATH_LED_LINK, UATH_LED_ON);
		/* make activity LED blink */
		uath_set_ledblink(sc, UATH_LED_ACTIVITY, UATH_LED_ON, 1, 2);
		/* set state to associated */
		uath_set_ledstate(sc, 1);

		/* start statistics timer */
		callout_reset(&sc->stat_ch, hz, uath_stat, sc);
		break;
	default:
		break;
	}
	ieee80211_free_node(ni);
	UATH_UNLOCK(sc);
	IEEE80211_LOCK(ic);
	return (uvp->newstate(vap, nstate, arg));
}

static int
uath_set_key(struct uath_softc *sc, const struct ieee80211_key *wk,
    int index)
{
#if 0
	struct uath_cmd_crypto crypto;
	int i;

	memset(&crypto, 0, sizeof(crypto));
	crypto.keyidx = htobe32(index);
	crypto.magic1 = htobe32(1);
	crypto.size   = htobe32(368);
	crypto.mask   = htobe32(0xffff);
	crypto.flags  = htobe32(0x80000068);
	if (index != UATH_DEFAULT_KEY)
		crypto.flags |= htobe32(index << 16);
	memset(crypto.magic2, 0xff, sizeof(crypto.magic2));

	/*
	 * Each byte of the key must be XOR'ed with 10101010 before being
	 * transmitted to the firmware.
	 */
	for (i = 0; i < wk->wk_keylen; i++)
		crypto.key[i] = wk->wk_key[i] ^ 0xaa;

	DPRINTF(sc, UATH_DEBUG_CRYPTO,
	    "setting crypto key index=%d len=%d\n", index, wk->wk_keylen);
	return uath_cmd_write(sc, WDCMSG_SET_KEY_CACHE_ENTRY, &crypto,
	    sizeof crypto, 0);
#else
	/* XXX support H/W cryto  */
	return (0);
#endif
}

static int
uath_set_keys(struct uath_softc *sc, struct ieee80211vap *vap)
{
	int i, error;

	error = 0;
	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		const struct ieee80211_key *wk = &vap->iv_nw_keys[i];

		if (wk->wk_flags & (IEEE80211_KEY_XMIT|IEEE80211_KEY_RECV)) {
			error = uath_set_key(sc, wk, i);
			if (error)
				return (error);
		}
	}
	if (vap->iv_def_txkey != IEEE80211_KEYIX_NONE) {
		error = uath_set_key(sc, &vap->iv_nw_keys[vap->iv_def_txkey],
			UATH_DEFAULT_KEY);
	}
	return (error);
}

#define	UATH_SYSCTL_STAT_ADD32(c, h, n, p, d)	\
	    SYSCTL_ADD_UINT(c, h, OID_AUTO, n, CTLFLAG_RD, p, 0, d)

static void
uath_sysctl_node(struct uath_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child;
	struct sysctl_oid *tree;
	struct uath_stat *stats;

	stats = &sc->sc_stat;
	ctx = device_get_sysctl_ctx(sc->sc_dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev));

	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "UATH statistics");
	child = SYSCTL_CHILDREN(tree);
	UATH_SYSCTL_STAT_ADD32(ctx, child, "badchunkseqnum",
	    &stats->st_badchunkseqnum, "Bad chunk sequence numbers");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "invalidlen", &stats->st_invalidlen,
	    "Invalid length");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "multichunk", &stats->st_multichunk,
	    "Multi chunks");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "toobigrxpkt",
	    &stats->st_toobigrxpkt, "Too big rx packets");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "stopinprogress",
	    &stats->st_stopinprogress, "Stop in progress");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "crcerrs", &stats->st_crcerr,
	    "CRC errors");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "phyerr", &stats->st_phyerr,
	    "PHY errors");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "decrypt_crcerr",
	    &stats->st_decrypt_crcerr, "Decryption CRC errors");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "decrypt_micerr",
	    &stats->st_decrypt_micerr, "Decryption Misc errors");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "decomperr", &stats->st_decomperr,
	    "Decomp errors");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "keyerr", &stats->st_keyerr,
	    "Key errors");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "err", &stats->st_err,
	    "Unknown errors");

	UATH_SYSCTL_STAT_ADD32(ctx, child, "cmd_active",
	    &stats->st_cmd_active, "Active numbers in Command queue");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "cmd_inactive",
	    &stats->st_cmd_inactive, "Inactive numbers in Command queue");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "cmd_pending",
	    &stats->st_cmd_pending, "Pending numbers in Command queue");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "cmd_waiting",
	    &stats->st_cmd_waiting, "Waiting numbers in Command queue");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "rx_active",
	    &stats->st_rx_active, "Active numbers in RX queue");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "rx_inactive",
	    &stats->st_rx_inactive, "Inactive numbers in RX queue");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "tx_active",
	    &stats->st_tx_active, "Active numbers in TX queue");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "tx_inactive",
	    &stats->st_tx_inactive, "Inactive numbers in TX queue");
	UATH_SYSCTL_STAT_ADD32(ctx, child, "tx_pending",
	    &stats->st_tx_pending, "Pending numbers in TX queue");
}

#undef UATH_SYSCTL_STAT_ADD32

CTASSERT(sizeof(u_int) >= sizeof(uint32_t));

static void
uath_cmdeof(struct uath_softc *sc, struct uath_cmd *cmd)
{
	struct uath_cmd_hdr *hdr;
	uint32_t dlen;

	hdr = (struct uath_cmd_hdr *)cmd->buf;
	/* NB: msgid is passed thru w/o byte swapping */
#ifdef UATH_DEBUG
	if (sc->sc_debug & UATH_DEBUG_CMDS) {
		uint32_t len = be32toh(hdr->len);
		printf("%s: %s [ix %u] len %u status %u\n",
		    __func__, uath_codename(be32toh(hdr->code)),
		    hdr->msgid, len, be32toh(hdr->magic));
		if (sc->sc_debug & UATH_DEBUG_CMDS_DUMP)
			uath_dump_cmd(cmd->buf,
			    len > UATH_MAX_CMDSZ ? sizeof(*hdr) : len, '-');
	}
#endif
	hdr->code = be32toh(hdr->code);
	hdr->len = be32toh(hdr->len);
	hdr->magic = be32toh(hdr->magic);	/* target status on return */

	switch (hdr->code & 0xff) {
	/* reply to a read command */
	default:
		DPRINTF(sc, UATH_DEBUG_RX_PROC | UATH_DEBUG_RECV_ALL,
		    "%s: code %d hdr len %u\n",
		    __func__, hdr->code & 0xff, hdr->len);
		/*
		 * The first response from the target after the
		 * HOST_AVAILABLE has an invalid msgid so we must
		 * treat it specially.
		 */
		if (hdr->msgid < UATH_CMD_LIST_COUNT) {
			uint32_t *rp = (uint32_t *)(hdr+1);
			u_int olen;

			if (sizeof(*hdr) > hdr->len ||
			    hdr->len >= UATH_MAX_CMDSZ) {
				device_printf(sc->sc_dev,
				    "%s: invalid WDC msg length %u; "
				    "msg ignored\n", __func__, hdr->len);
				return;
			}
			/*
			 * Calculate return/receive payload size; the
			 * first word, if present, always gives the
			 * number of bytes--unless it's 0 in which
			 * case a single 32-bit word should be present.
			 */
			dlen = hdr->len - sizeof(*hdr);
			if (dlen >= sizeof(uint32_t)) {
				olen = be32toh(rp[0]);
				dlen -= sizeof(uint32_t);
				if (olen == 0) {
					/* convention is 0 =>'s one word */
					olen = sizeof(uint32_t);
					/* XXX KASSERT(olen == dlen ) */
				}
			} else
				olen = 0;
			if (cmd->odata != NULL) {
				/* NB: cmd->olen validated in uath_cmd */
				if (olen > (u_int)cmd->olen) {
					/* XXX complain? */
					device_printf(sc->sc_dev,
					    "%s: cmd 0x%x olen %u cmd olen %u\n",
					    __func__, hdr->code, olen,
					    cmd->olen);
					olen = cmd->olen;
				}
				if (olen > dlen) {
					/* XXX complain, shouldn't happen */
					device_printf(sc->sc_dev,
					    "%s: cmd 0x%x olen %u dlen %u\n",
					    __func__, hdr->code, olen, dlen);
					olen = dlen;
				}
				/* XXX have submitter do this */
				/* copy answer into caller's supplied buffer */
				bcopy(&rp[1], cmd->odata, olen);
				cmd->olen = olen;
			}
		}
		wakeup_one(cmd);		/* wake up caller */
		break;

	case WDCMSG_TARGET_START:
		if (hdr->msgid >= UATH_CMD_LIST_COUNT) {
			/* XXX */
			return;
		}
		dlen = hdr->len - sizeof(*hdr);
		if (dlen != sizeof(uint32_t)) {
			device_printf(sc->sc_dev,
			    "%s: dlen (%u) != %zu!\n",
			    __func__, dlen, sizeof(uint32_t));
			return;
		}
		/* XXX have submitter do this */
		/* copy answer into caller's supplied buffer */
		bcopy(hdr+1, cmd->odata, sizeof(uint32_t));
		cmd->olen = sizeof(uint32_t);
		wakeup_one(cmd);		/* wake up caller */
		break;

	case WDCMSG_SEND_COMPLETE:
		/* this notification is sent when UATH_TX_NOTIFY is set */
		DPRINTF(sc, UATH_DEBUG_RX_PROC | UATH_DEBUG_RECV_ALL,
		    "%s: received Tx notification\n", __func__);
		break;

	case WDCMSG_TARGET_GET_STATS:
		DPRINTF(sc, UATH_DEBUG_RX_PROC | UATH_DEBUG_RECV_ALL,
		    "%s: received device statistics\n", __func__);
		callout_reset(&sc->stat_ch, hz, uath_stat, sc);
		break;
	}
}

static void
uath_intr_rx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uath_softc *sc = usbd_xfer_softc(xfer);
	struct uath_cmd *cmd;
	struct uath_cmd_hdr *hdr;
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	UATH_ASSERT_LOCKED(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		cmd = STAILQ_FIRST(&sc->sc_cmd_waiting);
		if (cmd == NULL)
			goto setup;
		STAILQ_REMOVE_HEAD(&sc->sc_cmd_waiting, next);
		UATH_STAT_DEC(sc, st_cmd_waiting);
		STAILQ_INSERT_TAIL(&sc->sc_cmd_inactive, cmd, next);
		UATH_STAT_INC(sc, st_cmd_inactive);

		if (actlen < sizeof(struct uath_cmd_hdr)) {
			device_printf(sc->sc_dev,
			    "%s: short xfer error (actlen %d)\n",
			    __func__, actlen);
			goto setup;
		}

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, cmd->buf, actlen);

		hdr = (struct uath_cmd_hdr *)cmd->buf;
		hdr->len = be32toh(hdr->len);
		if (hdr->len > (uint32_t)actlen) {
			device_printf(sc->sc_dev,
			    "%s: truncated xfer (len %u, actlen %d)\n",
			    __func__, hdr->len, actlen);
			goto setup;
		}

		uath_cmdeof(sc, cmd);
	case USB_ST_SETUP:
setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;
	default:
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto setup;
		}
		break;
	}
}

static void
uath_intr_tx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uath_softc *sc = usbd_xfer_softc(xfer);
	struct uath_cmd *cmd;

	UATH_ASSERT_LOCKED(sc);

	cmd = STAILQ_FIRST(&sc->sc_cmd_active);
	if (cmd != NULL && USB_GET_STATE(xfer) != USB_ST_SETUP) {
		STAILQ_REMOVE_HEAD(&sc->sc_cmd_active, next);
		UATH_STAT_DEC(sc, st_cmd_active);
		STAILQ_INSERT_TAIL((cmd->flags & UATH_CMD_FLAG_READ) ?
		    &sc->sc_cmd_waiting : &sc->sc_cmd_inactive, cmd, next);
		if (cmd->flags & UATH_CMD_FLAG_READ)
			UATH_STAT_INC(sc, st_cmd_waiting);
		else
			UATH_STAT_INC(sc, st_cmd_inactive);
	}

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
	case USB_ST_SETUP:
setup:
		cmd = STAILQ_FIRST(&sc->sc_cmd_pending);
		if (cmd == NULL) {
			DPRINTF(sc, UATH_DEBUG_XMIT, "%s: empty pending queue\n",
			    __func__);
			return;
		}
		STAILQ_REMOVE_HEAD(&sc->sc_cmd_pending, next);
		UATH_STAT_DEC(sc, st_cmd_pending);
		STAILQ_INSERT_TAIL((cmd->flags & UATH_CMD_FLAG_ASYNC) ?
		    &sc->sc_cmd_inactive : &sc->sc_cmd_active, cmd, next);
		if (cmd->flags & UATH_CMD_FLAG_ASYNC)
			UATH_STAT_INC(sc, st_cmd_inactive);
		else
			UATH_STAT_INC(sc, st_cmd_active);

		usbd_xfer_set_frame_data(xfer, 0, cmd->buf, cmd->buflen);
		usbd_transfer_submit(xfer);
		break;
	default:
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto setup;
		}
		break;
	}
}

static void
uath_update_rxstat(struct uath_softc *sc, uint32_t status)
{

	switch (status) {
	case UATH_STATUS_STOP_IN_PROGRESS:
		UATH_STAT_INC(sc, st_stopinprogress);
		break;
	case UATH_STATUS_CRC_ERR:
		UATH_STAT_INC(sc, st_crcerr);
		break;
	case UATH_STATUS_PHY_ERR:
		UATH_STAT_INC(sc, st_phyerr);
		break;
	case UATH_STATUS_DECRYPT_CRC_ERR:
		UATH_STAT_INC(sc, st_decrypt_crcerr);
		break;
	case UATH_STATUS_DECRYPT_MIC_ERR:
		UATH_STAT_INC(sc, st_decrypt_micerr);
		break;
	case UATH_STATUS_DECOMP_ERR:
		UATH_STAT_INC(sc, st_decomperr);
		break;
	case UATH_STATUS_KEY_ERR:
		UATH_STAT_INC(sc, st_keyerr);
		break;
	case UATH_STATUS_ERR:
		UATH_STAT_INC(sc, st_err);
		break;
	default:
		break;
	}
}

CTASSERT(UATH_MIN_RXBUFSZ >= sizeof(struct uath_chunk));

static struct mbuf *
uath_data_rxeof(struct usb_xfer *xfer, struct uath_data *data,
    struct uath_rx_desc **pdesc)
{
	struct uath_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct uath_chunk *chunk;
	struct uath_rx_desc *desc;
	struct mbuf *m = data->m, *mnew, *mp;
	uint16_t chunklen;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	if (actlen < (int)UATH_MIN_RXBUFSZ) {
		DPRINTF(sc, UATH_DEBUG_RECV | UATH_DEBUG_RECV_ALL,
		    "%s: wrong xfer size (len=%d)\n", __func__, actlen);
		counter_u64_add(ic->ic_ierrors, 1);
		return (NULL);
	}

	chunk = (struct uath_chunk *)data->buf;
	chunklen = be16toh(chunk->length);
	if (chunk->seqnum == 0 && chunk->flags == 0 && chunklen == 0) {
		device_printf(sc->sc_dev, "%s: strange response\n", __func__);
		counter_u64_add(ic->ic_ierrors, 1);
		UATH_RESET_INTRX(sc);
		return (NULL);
	}

	if (chunklen > actlen) {
		device_printf(sc->sc_dev,
		    "%s: invalid chunk length (len %u > actlen %d)\n",
		    __func__, chunklen, actlen);
		counter_u64_add(ic->ic_ierrors, 1);
		/* XXX cleanup? */
		UATH_RESET_INTRX(sc);
		return (NULL);
	}

	if (chunk->seqnum != sc->sc_intrx_nextnum) {
		DPRINTF(sc, UATH_DEBUG_XMIT, "invalid seqnum %d, expected %d\n",
		    chunk->seqnum, sc->sc_intrx_nextnum);
		UATH_STAT_INC(sc, st_badchunkseqnum);
		if (sc->sc_intrx_head != NULL)
			m_freem(sc->sc_intrx_head);
		UATH_RESET_INTRX(sc);
		return (NULL);
	}

	/* check multi-chunk frames  */
	if ((chunk->seqnum == 0 && !(chunk->flags & UATH_CFLAGS_FINAL)) ||
	    (chunk->seqnum != 0 && (chunk->flags & UATH_CFLAGS_FINAL)) ||
	    chunk->flags & UATH_CFLAGS_RXMSG)
		UATH_STAT_INC(sc, st_multichunk);

	if (chunk->flags & UATH_CFLAGS_FINAL) {
		if (chunklen < sizeof(struct uath_rx_desc)) {
			device_printf(sc->sc_dev,
			    "%s: invalid chunk length %d\n",
			    __func__, chunklen);
			counter_u64_add(ic->ic_ierrors, 1);
			if (sc->sc_intrx_head != NULL)
				m_freem(sc->sc_intrx_head);
			UATH_RESET_INTRX(sc);
			return (NULL);
		}
		chunklen -= sizeof(struct uath_rx_desc);
	}

	if (chunklen > 0 &&
	    (!(chunk->flags & UATH_CFLAGS_FINAL) || !(chunk->seqnum == 0))) {
		/* we should use intermediate RX buffer  */
		if (chunk->seqnum == 0)
			UATH_RESET_INTRX(sc);
		if ((sc->sc_intrx_len + sizeof(struct uath_rx_desc) +
		    chunklen) > UATH_MAX_INTRX_SIZE) {
			UATH_STAT_INC(sc, st_invalidlen);
			counter_u64_add(ic->ic_ierrors, 1);
			if (sc->sc_intrx_head != NULL)
				m_freem(sc->sc_intrx_head);
			UATH_RESET_INTRX(sc);
			return (NULL);
		}

		m->m_len = chunklen;
		m->m_data += sizeof(struct uath_chunk);

		if (sc->sc_intrx_head == NULL) {
			sc->sc_intrx_head = m;
			sc->sc_intrx_tail = m;
		} else {
			m->m_flags &= ~M_PKTHDR;
			sc->sc_intrx_tail->m_next = m;
			sc->sc_intrx_tail = m;
		}
	}
	sc->sc_intrx_len += chunklen;

	mnew = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (mnew == NULL) {
		DPRINTF(sc, UATH_DEBUG_RECV | UATH_DEBUG_RECV_ALL,
		    "%s: can't get new mbuf, drop frame\n", __func__);
		counter_u64_add(ic->ic_ierrors, 1);
		if (sc->sc_intrx_head != NULL)
			m_freem(sc->sc_intrx_head);
		UATH_RESET_INTRX(sc);
		return (NULL);
	}

	data->m = mnew;
	data->buf = mtod(mnew, uint8_t *);

	/* if the frame is not final continue the transfer  */
	if (!(chunk->flags & UATH_CFLAGS_FINAL)) {
		sc->sc_intrx_nextnum++;
		UATH_RESET_INTRX(sc);
		return (NULL);
	}

	/*
	 * if the frame is not set UATH_CFLAGS_RXMSG, then rx descriptor is
	 * located at the end, 32-bit aligned
	 */
	desc = (chunk->flags & UATH_CFLAGS_RXMSG) ?
		(struct uath_rx_desc *)(chunk + 1) :
		(struct uath_rx_desc *)(((uint8_t *)chunk) + 
		    sizeof(struct uath_chunk) + be16toh(chunk->length) -
		    sizeof(struct uath_rx_desc));
	if ((uint8_t *)chunk + actlen - sizeof(struct uath_rx_desc) <
	    (uint8_t *)desc) {
		device_printf(sc->sc_dev,
		    "%s: wrong Rx descriptor pointer "
		    "(desc %p chunk %p actlen %d)\n",
		    __func__, desc, chunk, actlen);
		counter_u64_add(ic->ic_ierrors, 1);
		if (sc->sc_intrx_head != NULL)
			m_freem(sc->sc_intrx_head);
		UATH_RESET_INTRX(sc);
		return (NULL);
	}

	*pdesc = desc;

	DPRINTF(sc, UATH_DEBUG_RECV | UATH_DEBUG_RECV_ALL,
	    "%s: frame len %u code %u status %u rate %u antenna %u "
	    "rssi %d channel %u phyerror %u connix %u decrypterror %u "
	    "keycachemiss %u\n", __func__, be32toh(desc->framelen)
	    , be32toh(desc->code), be32toh(desc->status), be32toh(desc->rate)
	    , be32toh(desc->antenna), be32toh(desc->rssi), be32toh(desc->channel)
	    , be32toh(desc->phyerror), be32toh(desc->connix)
	    , be32toh(desc->decrypterror), be32toh(desc->keycachemiss));

	if (be32toh(desc->len) > MCLBYTES) {
		DPRINTF(sc, UATH_DEBUG_RECV | UATH_DEBUG_RECV_ALL,
		    "%s: bad descriptor (len=%d)\n", __func__,
		    be32toh(desc->len));
		counter_u64_add(ic->ic_ierrors, 1);
		UATH_STAT_INC(sc, st_toobigrxpkt);
		if (sc->sc_intrx_head != NULL)
			m_freem(sc->sc_intrx_head);
		UATH_RESET_INTRX(sc);
		return (NULL);
	}

	uath_update_rxstat(sc, be32toh(desc->status));

	/* finalize mbuf */
	if (sc->sc_intrx_head == NULL) {
		uint32_t framelen;

		if (be32toh(desc->framelen) < UATH_RX_DUMMYSIZE) {
			device_printf(sc->sc_dev,
			    "%s: framelen too small (%u)\n",
			    __func__, be32toh(desc->framelen));
			counter_u64_add(ic->ic_ierrors, 1);
			if (sc->sc_intrx_head != NULL)
				m_freem(sc->sc_intrx_head);
			UATH_RESET_INTRX(sc);
			return (NULL);
		}

		framelen = be32toh(desc->framelen) - UATH_RX_DUMMYSIZE;
		if (framelen > actlen - sizeof(struct uath_chunk) ||
		    framelen < sizeof(struct ieee80211_frame_ack)) {
			device_printf(sc->sc_dev,
			    "%s: wrong frame length (%u, actlen %d)!\n",
			    __func__, framelen, actlen);
			counter_u64_add(ic->ic_ierrors, 1);
			if (sc->sc_intrx_head != NULL)
				m_freem(sc->sc_intrx_head);
			UATH_RESET_INTRX(sc);
			return (NULL);
		}

		m->m_pkthdr.len = m->m_len = framelen;
		m->m_data += sizeof(struct uath_chunk);
	} else {
		mp = sc->sc_intrx_head;
		mp->m_flags |= M_PKTHDR;
		mp->m_pkthdr.len = sc->sc_intrx_len;
		m = mp;
	}

	/* there are a lot more fields in the RX descriptor */
	if ((sc->sc_flags & UATH_FLAG_INVALID) == 0 &&
	    ieee80211_radiotap_active(ic)) {
		struct uath_rx_radiotap_header *tap = &sc->sc_rxtap;
		uint32_t tsf_hi = be32toh(desc->tstamp_high);
		uint32_t tsf_lo = be32toh(desc->tstamp_low);

		/* XXX only get low order 24bits of tsf from h/w */
		tap->wr_tsf = htole64(((uint64_t)tsf_hi << 32) | tsf_lo);
		tap->wr_flags = 0;
		if (be32toh(desc->status) == UATH_STATUS_CRC_ERR)
			tap->wr_flags |= IEEE80211_RADIOTAP_F_BADFCS;
		/* XXX map other status to BADFCS? */
		/* XXX ath h/w rate code, need to map */
		tap->wr_rate = be32toh(desc->rate);
		tap->wr_antenna = be32toh(desc->antenna);
		tap->wr_antsignal = -95 + be32toh(desc->rssi);
		tap->wr_antnoise = -95;
	}

	UATH_RESET_INTRX(sc);

	return (m);
}

static void
uath_bulk_rx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uath_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct mbuf *m = NULL;
	struct uath_data *data;
	struct uath_rx_desc *desc = NULL;
	int8_t nf;

	UATH_ASSERT_LOCKED(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		data = STAILQ_FIRST(&sc->sc_rx_active);
		if (data == NULL)
			goto setup;
		STAILQ_REMOVE_HEAD(&sc->sc_rx_active, next);
		UATH_STAT_DEC(sc, st_rx_active);
		m = uath_data_rxeof(xfer, data, &desc);
		STAILQ_INSERT_TAIL(&sc->sc_rx_inactive, data, next);
		UATH_STAT_INC(sc, st_rx_inactive);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
setup:
		data = STAILQ_FIRST(&sc->sc_rx_inactive);
		if (data == NULL)
			return;
		STAILQ_REMOVE_HEAD(&sc->sc_rx_inactive, next);
		UATH_STAT_DEC(sc, st_rx_inactive);
		STAILQ_INSERT_TAIL(&sc->sc_rx_active, data, next);
		UATH_STAT_INC(sc, st_rx_active);
		usbd_xfer_set_frame_data(xfer, 0, data->buf, MCLBYTES);
		usbd_transfer_submit(xfer);

		/*
		 * To avoid LOR we should unlock our private mutex here to call
		 * ieee80211_input() because here is at the end of a USB
		 * callback and safe to unlock.
		 */
		if (sc->sc_flags & UATH_FLAG_INVALID) {
			if (m != NULL)
				m_freem(m);
			return;
		}
		UATH_UNLOCK(sc);
		if (m != NULL && desc != NULL) {
			wh = mtod(m, struct ieee80211_frame *);
			ni = ieee80211_find_rxnode(ic,
			    (struct ieee80211_frame_min *)wh);
			nf = -95;	/* XXX */
			if (ni != NULL) {
				(void) ieee80211_input(ni, m,
				    (int)be32toh(desc->rssi), nf);
				/* node is no longer needed */
				ieee80211_free_node(ni);
			} else
				(void) ieee80211_input_all(ic, m,
				    (int)be32toh(desc->rssi), nf);
			m = NULL;
			desc = NULL;
		}
		UATH_LOCK(sc);
		uath_start(sc);
		break;
	default:
		/* needs it to the inactive queue due to a error.  */
		data = STAILQ_FIRST(&sc->sc_rx_active);
		if (data != NULL) {
			STAILQ_REMOVE_HEAD(&sc->sc_rx_active, next);
			UATH_STAT_DEC(sc, st_rx_active);
			STAILQ_INSERT_TAIL(&sc->sc_rx_inactive, data, next);
			UATH_STAT_INC(sc, st_rx_inactive);
		}
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			counter_u64_add(ic->ic_ierrors, 1);
			goto setup;
		}
		break;
	}
}

static void
uath_data_txeof(struct usb_xfer *xfer, struct uath_data *data)
{
	struct uath_softc *sc = usbd_xfer_softc(xfer);

	UATH_ASSERT_LOCKED(sc);

	if (data->m) {
		/* XXX status? */
		ieee80211_tx_complete(data->ni, data->m, 0);
		data->m = NULL;
		data->ni = NULL;
	}
	sc->sc_tx_timer = 0;
}

static void
uath_bulk_tx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uath_softc *sc = usbd_xfer_softc(xfer);
	struct uath_data *data;

	UATH_ASSERT_LOCKED(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		data = STAILQ_FIRST(&sc->sc_tx_active);
		if (data == NULL)
			goto setup;
		STAILQ_REMOVE_HEAD(&sc->sc_tx_active, next);
		UATH_STAT_DEC(sc, st_tx_active);
		uath_data_txeof(xfer, data);
		STAILQ_INSERT_TAIL(&sc->sc_tx_inactive, data, next);
		UATH_STAT_INC(sc, st_tx_inactive);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
setup:
		data = STAILQ_FIRST(&sc->sc_tx_pending);
		if (data == NULL) {
			DPRINTF(sc, UATH_DEBUG_XMIT, "%s: empty pending queue\n",
			    __func__);
			return;
		}
		STAILQ_REMOVE_HEAD(&sc->sc_tx_pending, next);
		UATH_STAT_DEC(sc, st_tx_pending);
		STAILQ_INSERT_TAIL(&sc->sc_tx_active, data, next);
		UATH_STAT_INC(sc, st_tx_active);

		usbd_xfer_set_frame_data(xfer, 0, data->buf, data->buflen);
		usbd_transfer_submit(xfer);

		uath_start(sc);
		break;
	default:
		data = STAILQ_FIRST(&sc->sc_tx_active);
		if (data == NULL)
			goto setup;
		if (data->ni != NULL) {
			if_inc_counter(data->ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			if ((sc->sc_flags & UATH_FLAG_INVALID) == 0)
				ieee80211_free_node(data->ni);
			data->ni = NULL;
		}
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto setup;
		}
		break;
	}
}

static device_method_t uath_methods[] = {
	DEVMETHOD(device_probe, uath_match),
	DEVMETHOD(device_attach, uath_attach),
	DEVMETHOD(device_detach, uath_detach),
	DEVMETHOD_END
};
static driver_t uath_driver = {
	.name = "uath",
	.methods = uath_methods,
	.size = sizeof(struct uath_softc)
};
static devclass_t uath_devclass;

DRIVER_MODULE(uath, uhub, uath_driver, uath_devclass, NULL, 0);
MODULE_DEPEND(uath, wlan, 1, 1, 1);
MODULE_DEPEND(uath, usb, 1, 1, 1);
MODULE_VERSION(uath, 1);
USB_PNP_HOST_INFO(uath_devs);

/*	$OpenBSD: if_zyd.c,v 1.52 2007/02/11 00:08:04 jsg Exp $	*/
/*	$NetBSD: if_zyd.c,v 1.7 2007/06/21 04:04:29 kiyohara Exp $	*/
/*	$FreeBSD$	*/

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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * ZyDAS ZD1211/ZD1211B USB WLAN driver.
 */

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
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
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#include <dev/usb/wlan/if_zydreg.h>
#include <dev/usb/wlan/if_zydfw.h>

#ifdef USB_DEBUG
static int zyd_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, zyd, CTLFLAG_RW, 0, "USB zyd");
SYSCTL_INT(_hw_usb_zyd, OID_AUTO, debug, CTLFLAG_RWTUN, &zyd_debug, 0,
    "zyd debug level");

enum {
	ZYD_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	ZYD_DEBUG_RECV		= 0x00000002,	/* basic recv operation */
	ZYD_DEBUG_RESET		= 0x00000004,	/* reset processing */
	ZYD_DEBUG_INIT		= 0x00000008,	/* device init */
	ZYD_DEBUG_TX_PROC	= 0x00000010,	/* tx ISR proc */
	ZYD_DEBUG_RX_PROC	= 0x00000020,	/* rx ISR proc */
	ZYD_DEBUG_STATE		= 0x00000040,	/* 802.11 state transitions */
	ZYD_DEBUG_STAT		= 0x00000080,	/* statistic */
	ZYD_DEBUG_FW		= 0x00000100,	/* firmware */
	ZYD_DEBUG_CMD		= 0x00000200,	/* fw commands */
	ZYD_DEBUG_ANY		= 0xffffffff
};
#define	DPRINTF(sc, m, fmt, ...) do {				\
	if (zyd_debug & (m))					\
		printf("%s: " fmt, __func__, ## __VA_ARGS__);	\
} while (0)
#else
#define	DPRINTF(sc, m, fmt, ...) do {				\
	(void) sc;						\
} while (0)
#endif

#define	zyd_do_request(sc,req,data) \
    usbd_do_request_flags((sc)->sc_udev, &(sc)->sc_mtx, req, data, 0, NULL, 5000)

static device_probe_t zyd_match;
static device_attach_t zyd_attach;
static device_detach_t zyd_detach;

static usb_callback_t zyd_intr_read_callback;
static usb_callback_t zyd_intr_write_callback;
static usb_callback_t zyd_bulk_read_callback;
static usb_callback_t zyd_bulk_write_callback;

static struct ieee80211vap *zyd_vap_create(struct ieee80211com *,
		    const char [IFNAMSIZ], int, enum ieee80211_opmode, int,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    const uint8_t [IEEE80211_ADDR_LEN]);
static void	zyd_vap_delete(struct ieee80211vap *);
static void	zyd_tx_free(struct zyd_tx_data *, int);
static void	zyd_setup_tx_list(struct zyd_softc *);
static void	zyd_unsetup_tx_list(struct zyd_softc *);
static int	zyd_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static int	zyd_cmd(struct zyd_softc *, uint16_t, const void *, int,
		    void *, int, int);
static int	zyd_read16(struct zyd_softc *, uint16_t, uint16_t *);
static int	zyd_read32(struct zyd_softc *, uint16_t, uint32_t *);
static int	zyd_write16(struct zyd_softc *, uint16_t, uint16_t);
static int	zyd_write32(struct zyd_softc *, uint16_t, uint32_t);
static int	zyd_rfwrite(struct zyd_softc *, uint32_t);
static int	zyd_lock_phy(struct zyd_softc *);
static int	zyd_unlock_phy(struct zyd_softc *);
static int	zyd_rf_attach(struct zyd_softc *, uint8_t);
static const char *zyd_rf_name(uint8_t);
static int	zyd_hw_init(struct zyd_softc *);
static int	zyd_read_pod(struct zyd_softc *);
static int	zyd_read_eeprom(struct zyd_softc *);
static int	zyd_get_macaddr(struct zyd_softc *);
static int	zyd_set_macaddr(struct zyd_softc *, const uint8_t *);
static int	zyd_set_bssid(struct zyd_softc *, const uint8_t *);
static int	zyd_switch_radio(struct zyd_softc *, int);
static int	zyd_set_led(struct zyd_softc *, int, int);
static void	zyd_set_multi(struct zyd_softc *);
static void	zyd_update_mcast(struct ieee80211com *);
static int	zyd_set_rxfilter(struct zyd_softc *);
static void	zyd_set_chan(struct zyd_softc *, struct ieee80211_channel *);
static int	zyd_set_beacon_interval(struct zyd_softc *, int);
static void	zyd_rx_data(struct usb_xfer *, int, uint16_t);
static int	zyd_tx_start(struct zyd_softc *, struct mbuf *,
		    struct ieee80211_node *);
static int	zyd_transmit(struct ieee80211com *, struct mbuf *);
static void	zyd_start(struct zyd_softc *);
static int	zyd_raw_xmit(struct ieee80211_node *, struct mbuf *,
		    const struct ieee80211_bpf_params *);
static void	zyd_parent(struct ieee80211com *);
static void	zyd_init_locked(struct zyd_softc *);
static void	zyd_stop(struct zyd_softc *);
static int	zyd_loadfirmware(struct zyd_softc *);
static void	zyd_scan_start(struct ieee80211com *);
static void	zyd_scan_end(struct ieee80211com *);
static void	zyd_getradiocaps(struct ieee80211com *, int, int *,
		    struct ieee80211_channel[]);
static void	zyd_set_channel(struct ieee80211com *);
static int	zyd_rfmd_init(struct zyd_rf *);
static int	zyd_rfmd_switch_radio(struct zyd_rf *, int);
static int	zyd_rfmd_set_channel(struct zyd_rf *, uint8_t);
static int	zyd_al2230_init(struct zyd_rf *);
static int	zyd_al2230_switch_radio(struct zyd_rf *, int);
static int	zyd_al2230_set_channel(struct zyd_rf *, uint8_t);
static int	zyd_al2230_set_channel_b(struct zyd_rf *, uint8_t);
static int	zyd_al2230_init_b(struct zyd_rf *);
static int	zyd_al7230B_init(struct zyd_rf *);
static int	zyd_al7230B_switch_radio(struct zyd_rf *, int);
static int	zyd_al7230B_set_channel(struct zyd_rf *, uint8_t);
static int	zyd_al2210_init(struct zyd_rf *);
static int	zyd_al2210_switch_radio(struct zyd_rf *, int);
static int	zyd_al2210_set_channel(struct zyd_rf *, uint8_t);
static int	zyd_gct_init(struct zyd_rf *);
static int	zyd_gct_switch_radio(struct zyd_rf *, int);
static int	zyd_gct_set_channel(struct zyd_rf *, uint8_t);
static int	zyd_gct_mode(struct zyd_rf *);
static int	zyd_gct_set_channel_synth(struct zyd_rf *, int, int);
static int	zyd_gct_write(struct zyd_rf *, uint16_t);
static int	zyd_gct_txgain(struct zyd_rf *, uint8_t);
static int	zyd_maxim2_init(struct zyd_rf *);
static int	zyd_maxim2_switch_radio(struct zyd_rf *, int);
static int	zyd_maxim2_set_channel(struct zyd_rf *, uint8_t);

static const struct zyd_phy_pair zyd_def_phy[] = ZYD_DEF_PHY;
static const struct zyd_phy_pair zyd_def_phyB[] = ZYD_DEF_PHYB;

/* various supported device vendors/products */
#define ZYD_ZD1211	0
#define ZYD_ZD1211B	1

#define	ZYD_ZD1211_DEV(v,p)	\
	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, ZYD_ZD1211) }
#define	ZYD_ZD1211B_DEV(v,p)	\
	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, ZYD_ZD1211B) }
static const STRUCT_USB_HOST_ID zyd_devs[] = {
	/* ZYD_ZD1211 */
	ZYD_ZD1211_DEV(3COM2, 3CRUSB10075),
	ZYD_ZD1211_DEV(ABOCOM, WL54),
	ZYD_ZD1211_DEV(ASUS, WL159G),
	ZYD_ZD1211_DEV(CYBERTAN, TG54USB),
	ZYD_ZD1211_DEV(DRAYTEK, VIGOR550),
	ZYD_ZD1211_DEV(PLANEX2, GWUS54GD),
	ZYD_ZD1211_DEV(PLANEX2, GWUS54GZL),
	ZYD_ZD1211_DEV(PLANEX3, GWUS54GZ),
	ZYD_ZD1211_DEV(PLANEX3, GWUS54MINI),
	ZYD_ZD1211_DEV(SAGEM, XG760A),
	ZYD_ZD1211_DEV(SENAO, NUB8301),
	ZYD_ZD1211_DEV(SITECOMEU, WL113),
	ZYD_ZD1211_DEV(SWEEX, ZD1211),
	ZYD_ZD1211_DEV(TEKRAM, QUICKWLAN),
	ZYD_ZD1211_DEV(TEKRAM, ZD1211_1),
	ZYD_ZD1211_DEV(TEKRAM, ZD1211_2),
	ZYD_ZD1211_DEV(TWINMOS, G240),
	ZYD_ZD1211_DEV(UMEDIA, ALL0298V2),
	ZYD_ZD1211_DEV(UMEDIA, TEW429UB_A),
	ZYD_ZD1211_DEV(UMEDIA, TEW429UB),
	ZYD_ZD1211_DEV(WISTRONNEWEB, UR055G),
	ZYD_ZD1211_DEV(ZCOM, ZD1211),
	ZYD_ZD1211_DEV(ZYDAS, ZD1211),
	ZYD_ZD1211_DEV(ZYXEL, AG225H),
	ZYD_ZD1211_DEV(ZYXEL, ZYAIRG220),
	ZYD_ZD1211_DEV(ZYXEL, G200V2),
	/* ZYD_ZD1211B */
	ZYD_ZD1211B_DEV(ACCTON, SMCWUSBG_NF),
	ZYD_ZD1211B_DEV(ACCTON, SMCWUSBG),
	ZYD_ZD1211B_DEV(ACCTON, ZD1211B),
	ZYD_ZD1211B_DEV(ASUS, A9T_WIFI),
	ZYD_ZD1211B_DEV(BELKIN, F5D7050_V4000),
	ZYD_ZD1211B_DEV(BELKIN, ZD1211B),
	ZYD_ZD1211B_DEV(CISCOLINKSYS, WUSBF54G),
	ZYD_ZD1211B_DEV(FIBERLINE, WL430U),
	ZYD_ZD1211B_DEV(MELCO, KG54L),
	ZYD_ZD1211B_DEV(PHILIPS, SNU5600),
	ZYD_ZD1211B_DEV(PLANEX2, GW_US54GXS),
	ZYD_ZD1211B_DEV(SAGEM, XG76NA),
	ZYD_ZD1211B_DEV(SITECOMEU, ZD1211B),
	ZYD_ZD1211B_DEV(UMEDIA, TEW429UBC1),
	ZYD_ZD1211B_DEV(USR, USR5423),
	ZYD_ZD1211B_DEV(VTECH, ZD1211B),
	ZYD_ZD1211B_DEV(ZCOM, ZD1211B),
	ZYD_ZD1211B_DEV(ZYDAS, ZD1211B),
	ZYD_ZD1211B_DEV(ZYXEL, M202),
	ZYD_ZD1211B_DEV(ZYXEL, G202),
	ZYD_ZD1211B_DEV(ZYXEL, G220V2)
};

static const struct usb_config zyd_config[ZYD_N_TRANSFER] = {
	[ZYD_BULK_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = ZYD_MAX_TXBUFSZ,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = zyd_bulk_write_callback,
		.ep_index = 0,
		.timeout = 10000,	/* 10 seconds */
	},
	[ZYD_BULK_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = ZYX_MAX_RXBUFSZ,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = zyd_bulk_read_callback,
		.ep_index = 0,
	},
	[ZYD_INTR_WR] = {
		.type = UE_BULK_INTR,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = sizeof(struct zyd_cmd),
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = zyd_intr_write_callback,
		.timeout = 1000,	/* 1 second */
		.ep_index = 1,
	},
	[ZYD_INTR_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = sizeof(struct zyd_cmd),
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = zyd_intr_read_callback,
	},
};
#define zyd_read16_m(sc, val, data)	do {				\
	error = zyd_read16(sc, val, data);				\
	if (error != 0)							\
		goto fail;						\
} while (0)
#define zyd_write16_m(sc, val, data)	do {				\
	error = zyd_write16(sc, val, data);				\
	if (error != 0)							\
		goto fail;						\
} while (0)
#define zyd_read32_m(sc, val, data)	do {				\
	error = zyd_read32(sc, val, data);				\
	if (error != 0)							\
		goto fail;						\
} while (0)
#define zyd_write32_m(sc, val, data)	do {				\
	error = zyd_write32(sc, val, data);				\
	if (error != 0)							\
		goto fail;						\
} while (0)

static int
zyd_match(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != ZYD_CONFIG_INDEX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != ZYD_IFACE_INDEX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(zyd_devs, sizeof(zyd_devs), uaa));
}

static int
zyd_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct zyd_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t iface_index;
	int error;

	if (uaa->info.bcdDevice < 0x4330) {
		device_printf(dev, "device version mismatch: 0x%X "
		    "(only >= 43.30 supported)\n",
		    uaa->info.bcdDevice);
		return (EINVAL);
	}

	device_set_usb_desc(dev);
	sc->sc_dev = dev;
	sc->sc_udev = uaa->device;
	sc->sc_macrev = USB_GET_DRIVER_INFO(uaa);

	mtx_init(&sc->sc_mtx, device_get_nameunit(sc->sc_dev),
	    MTX_NETWORK_LOCK, MTX_DEF);
	STAILQ_INIT(&sc->sc_rqh);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	iface_index = ZYD_IFACE_INDEX;
	error = usbd_transfer_setup(uaa->device,
	    &iface_index, sc->sc_xfer, zyd_config,
	    ZYD_N_TRANSFER, sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "could not allocate USB transfers, "
		    "err=%s\n", usbd_errstr(error));
		goto detach;
	}

	ZYD_LOCK(sc);
	if ((error = zyd_get_macaddr(sc)) != 0) {
		device_printf(sc->sc_dev, "could not read EEPROM\n");
		ZYD_UNLOCK(sc);
		goto detach;
	}
	ZYD_UNLOCK(sc);

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(dev);
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;

	/* set device capabilities */
	ic->ic_caps =
		  IEEE80211_C_STA		/* station mode */
		| IEEE80211_C_MONITOR		/* monitor mode */
		| IEEE80211_C_SHPREAMBLE	/* short preamble supported */
	        | IEEE80211_C_SHSLOT		/* short slot time supported */
		| IEEE80211_C_BGSCAN		/* capable of bg scanning */
	        | IEEE80211_C_WPA		/* 802.11i */
		;

	zyd_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	ieee80211_ifattach(ic);
	ic->ic_raw_xmit = zyd_raw_xmit;
	ic->ic_scan_start = zyd_scan_start;
	ic->ic_scan_end = zyd_scan_end;
	ic->ic_getradiocaps = zyd_getradiocaps;
	ic->ic_set_channel = zyd_set_channel;
	ic->ic_vap_create = zyd_vap_create;
	ic->ic_vap_delete = zyd_vap_delete;
	ic->ic_update_mcast = zyd_update_mcast;
	ic->ic_update_promisc = zyd_update_mcast;
	ic->ic_parent = zyd_parent;
	ic->ic_transmit = zyd_transmit;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_txtap.wt_ihdr, sizeof(sc->sc_txtap),
		ZYD_TX_RADIOTAP_PRESENT,
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
		ZYD_RX_RADIOTAP_PRESENT);

	if (bootverbose)
		ieee80211_announce(ic);

	return (0);

detach:
	zyd_detach(dev);
	return (ENXIO);			/* failure */
}

static void
zyd_drain_mbufq(struct zyd_softc *sc)
{
	struct mbuf *m;
	struct ieee80211_node *ni;

	ZYD_LOCK_ASSERT(sc, MA_OWNED);
	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		m->m_pkthdr.rcvif = NULL;
		ieee80211_free_node(ni);
		m_freem(m);
	}
}


static int
zyd_detach(device_t dev)
{
	struct zyd_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	unsigned int x;

	/*
	 * Prevent further allocations from RX/TX data
	 * lists and ioctls:
	 */
	ZYD_LOCK(sc);
	sc->sc_flags |= ZYD_FLAG_DETACHED;
	zyd_drain_mbufq(sc);
	STAILQ_INIT(&sc->tx_q);
	STAILQ_INIT(&sc->tx_free);
	ZYD_UNLOCK(sc);

	/* drain USB transfers */
	for (x = 0; x != ZYD_N_TRANSFER; x++)
		usbd_transfer_drain(sc->sc_xfer[x]);

	/* free TX list, if any */
	ZYD_LOCK(sc);
	zyd_unsetup_tx_list(sc);
	ZYD_UNLOCK(sc);

	/* free USB transfers and some data buffers */
	usbd_transfer_unsetup(sc->sc_xfer, ZYD_N_TRANSFER);

	if (ic->ic_softc == sc)
		ieee80211_ifdetach(ic);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static struct ieee80211vap *
zyd_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct zyd_vap *zvp;
	struct ieee80211vap *vap;

	if (!TAILQ_EMPTY(&ic->ic_vaps))		/* only one at a time */
		return (NULL);
	zvp = malloc(sizeof(struct zyd_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &zvp->vap;

	/* enable s/w bmiss handling for sta mode */
	if (ieee80211_vap_setup(ic, vap, name, unit, opmode,
	    flags | IEEE80211_CLONE_NOBEACONS, bssid) != 0) {
		/* out of memory */
		free(zvp, M_80211_VAP);
		return (NULL);
	}

	/* override state transition machine */
	zvp->newstate = vap->iv_newstate;
	vap->iv_newstate = zyd_newstate;

	ieee80211_ratectl_init(vap);
	ieee80211_ratectl_setinterval(vap, 1000 /* 1 sec */);

	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change,
	    ieee80211_media_status, mac);
	ic->ic_opmode = opmode;
	return (vap);
}

static void
zyd_vap_delete(struct ieee80211vap *vap)
{
	struct zyd_vap *zvp = ZYD_VAP(vap);

	ieee80211_ratectl_deinit(vap);
	ieee80211_vap_detach(vap);
	free(zvp, M_80211_VAP);
}

static void
zyd_tx_free(struct zyd_tx_data *data, int txerr)
{
	struct zyd_softc *sc = data->sc;

	if (data->m != NULL) {
		ieee80211_tx_complete(data->ni, data->m, txerr);
		data->m = NULL;
		data->ni = NULL;
	}
	STAILQ_INSERT_TAIL(&sc->tx_free, data, next);
	sc->tx_nfree++;
}

static void
zyd_setup_tx_list(struct zyd_softc *sc)
{
	struct zyd_tx_data *data;
	int i;

	sc->tx_nfree = 0;
	STAILQ_INIT(&sc->tx_q);
	STAILQ_INIT(&sc->tx_free);

	for (i = 0; i < ZYD_TX_LIST_CNT; i++) {
		data = &sc->tx_data[i];

		data->sc = sc;
		STAILQ_INSERT_TAIL(&sc->tx_free, data, next);
		sc->tx_nfree++;
	}
}

static void
zyd_unsetup_tx_list(struct zyd_softc *sc)
{
	struct zyd_tx_data *data;
	int i;

	/* make sure any subsequent use of the queues will fail */
	sc->tx_nfree = 0;
	STAILQ_INIT(&sc->tx_q);
	STAILQ_INIT(&sc->tx_free);

	/* free up all node references and mbufs */
	for (i = 0; i < ZYD_TX_LIST_CNT; i++) {
		data = &sc->tx_data[i];

		if (data->m != NULL) {
			m_freem(data->m);
			data->m = NULL;
		}
		if (data->ni != NULL) {
			ieee80211_free_node(data->ni);
			data->ni = NULL;
		}
	}
}

static int
zyd_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct zyd_vap *zvp = ZYD_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct zyd_softc *sc = ic->ic_softc;
	int error;

	DPRINTF(sc, ZYD_DEBUG_STATE, "%s: %s -> %s\n", __func__,
	    ieee80211_state_name[vap->iv_state],
	    ieee80211_state_name[nstate]);

	IEEE80211_UNLOCK(ic);
	ZYD_LOCK(sc);
	switch (nstate) {
	case IEEE80211_S_AUTH:
		zyd_set_chan(sc, ic->ic_curchan);
		break;
	case IEEE80211_S_RUN:
		if (vap->iv_opmode == IEEE80211_M_MONITOR)
			break;

		/* turn link LED on */
		error = zyd_set_led(sc, ZYD_LED1, 1);
		if (error != 0)
			break;

		/* make data LED blink upon Tx */
		zyd_write32_m(sc, sc->sc_fwbase + ZYD_FW_LINK_STATUS, 1);

		IEEE80211_ADDR_COPY(sc->sc_bssid, vap->iv_bss->ni_bssid);
		zyd_set_bssid(sc, sc->sc_bssid);
		break;
	default:
		break;
	}
fail:
	ZYD_UNLOCK(sc);
	IEEE80211_LOCK(ic);
	return (zvp->newstate(vap, nstate, arg));
}

/*
 * Callback handler for interrupt transfer
 */
static void
zyd_intr_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct zyd_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct ieee80211_node *ni;
	struct zyd_cmd *cmd = &sc->sc_ibuf;
	struct usb_page_cache *pc;
	int datalen;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, cmd, sizeof(*cmd));

		switch (le16toh(cmd->code)) {
		case ZYD_NOTIF_RETRYSTATUS:
		{
			struct zyd_notif_retry *retry =
			    (struct zyd_notif_retry *)cmd->data;
			uint16_t count = le16toh(retry->count);

			DPRINTF(sc, ZYD_DEBUG_TX_PROC,
			    "retry intr: rate=0x%x addr=%s count=%d (0x%x)\n",
			    le16toh(retry->rate), ether_sprintf(retry->macaddr),
			    count & 0xff, count);

			/*
			 * Find the node to which the packet was sent and
			 * update its retry statistics.  In BSS mode, this node
			 * is the AP we're associated to so no lookup is
			 * actually needed.
			 */
			ni = ieee80211_find_txnode(vap, retry->macaddr);
			if (ni != NULL) {
				struct ieee80211_ratectl_tx_status *txs =
				    &sc->sc_txs;
				int retrycnt = count & 0xff;

				txs->flags =
				    IEEE80211_RATECTL_STATUS_LONG_RETRY;
				txs->long_retries = retrycnt;
				if (count & 0x100) {
					txs->status =
					    IEEE80211_RATECTL_TX_FAIL_LONG;
				} else {
					txs->status =
					    IEEE80211_RATECTL_TX_SUCCESS;
				}


				ieee80211_ratectl_tx_complete(ni, txs);
				ieee80211_free_node(ni);
			}
			if (count & 0x100)
				/* too many retries */
				if_inc_counter(vap->iv_ifp, IFCOUNTER_OERRORS,
				    1);
			break;
		}
		case ZYD_NOTIF_IORD:
		{
			struct zyd_rq *rqp;

			if (le16toh(*(uint16_t *)cmd->data) == ZYD_CR_INTERRUPT)
				break;	/* HMAC interrupt */

			datalen = actlen - sizeof(cmd->code);
			datalen -= 2;	/* XXX: padding? */

			STAILQ_FOREACH(rqp, &sc->sc_rqh, rq) {
				int i;
				int count;

				if (rqp->olen != datalen)
					continue;
				count = rqp->olen / sizeof(struct zyd_pair);
				for (i = 0; i < count; i++) {
					if (*(((const uint16_t *)rqp->idata) + i) !=
					    (((struct zyd_pair *)cmd->data) + i)->reg)
						break;
				}
				if (i != count)
					continue;
				/* copy answer into caller-supplied buffer */
				memcpy(rqp->odata, cmd->data, rqp->olen);
				DPRINTF(sc, ZYD_DEBUG_CMD,
				    "command %p complete, data = %*D \n",
				    rqp, rqp->olen, (char *)rqp->odata, ":");
				wakeup(rqp);	/* wakeup caller */
				break;
			}
			if (rqp == NULL) {
				device_printf(sc->sc_dev,
				    "unexpected IORD notification %*D\n",
				    datalen, cmd->data, ":");
			}
			break;
		}
		default:
			device_printf(sc->sc_dev, "unknown notification %x\n",
			    le16toh(cmd->code));
		}

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		DPRINTF(sc, ZYD_DEBUG_CMD, "error = %s\n",
		    usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
zyd_intr_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct zyd_softc *sc = usbd_xfer_softc(xfer);
	struct zyd_rq *rqp, *cmd;
	struct usb_page_cache *pc;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		cmd = usbd_xfer_get_priv(xfer);
		DPRINTF(sc, ZYD_DEBUG_CMD, "command %p transferred\n", cmd);
		STAILQ_FOREACH(rqp, &sc->sc_rqh, rq) {
			/* Ensure the cached rq pointer is still valid */
			if (rqp == cmd &&
			    (rqp->flags & ZYD_CMD_FLAG_READ) == 0)
				wakeup(rqp);	/* wakeup caller */
		}

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		STAILQ_FOREACH(rqp, &sc->sc_rqh, rq) {
			if (rqp->flags & ZYD_CMD_FLAG_SENT)
				continue;

			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_in(pc, 0, rqp->cmd, rqp->ilen);

			usbd_xfer_set_frame_len(xfer, 0, rqp->ilen);
			usbd_xfer_set_priv(xfer, rqp);
			rqp->flags |= ZYD_CMD_FLAG_SENT;
			usbd_transfer_submit(xfer);
			break;
		}
		break;

	default:			/* Error */
		DPRINTF(sc, ZYD_DEBUG_ANY, "error = %s\n",
		    usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static int
zyd_cmd(struct zyd_softc *sc, uint16_t code, const void *idata, int ilen,
    void *odata, int olen, int flags)
{
	struct zyd_cmd cmd;
	struct zyd_rq rq;
	int error;

	if (ilen > (int)sizeof(cmd.data))
		return (EINVAL);

	cmd.code = htole16(code);
	memcpy(cmd.data, idata, ilen);
	DPRINTF(sc, ZYD_DEBUG_CMD, "sending cmd %p = %*D\n",
	    &rq, ilen, idata, ":");

	rq.cmd = &cmd;
	rq.idata = idata;
	rq.odata = odata;
	rq.ilen = sizeof(uint16_t) + ilen;
	rq.olen = olen;
	rq.flags = flags;
	STAILQ_INSERT_TAIL(&sc->sc_rqh, &rq, rq);
	usbd_transfer_start(sc->sc_xfer[ZYD_INTR_RD]);
	usbd_transfer_start(sc->sc_xfer[ZYD_INTR_WR]);

	/* wait at most one second for command reply */
	error = mtx_sleep(&rq, &sc->sc_mtx, 0 , "zydcmd", hz);
	if (error)
		device_printf(sc->sc_dev, "command timeout\n");
	STAILQ_REMOVE(&sc->sc_rqh, &rq, zyd_rq, rq);
	DPRINTF(sc, ZYD_DEBUG_CMD, "finsihed cmd %p, error = %d \n",
	    &rq, error);

	return (error);
}

static int
zyd_read16(struct zyd_softc *sc, uint16_t reg, uint16_t *val)
{
	struct zyd_pair tmp;
	int error;

	reg = htole16(reg);
	error = zyd_cmd(sc, ZYD_CMD_IORD, &reg, sizeof(reg), &tmp, sizeof(tmp),
	    ZYD_CMD_FLAG_READ);
	if (error == 0)
		*val = le16toh(tmp.val);
	return (error);
}

static int
zyd_read32(struct zyd_softc *sc, uint16_t reg, uint32_t *val)
{
	struct zyd_pair tmp[2];
	uint16_t regs[2];
	int error;

	regs[0] = htole16(ZYD_REG32_HI(reg));
	regs[1] = htole16(ZYD_REG32_LO(reg));
	error = zyd_cmd(sc, ZYD_CMD_IORD, regs, sizeof(regs), tmp, sizeof(tmp),
	    ZYD_CMD_FLAG_READ);
	if (error == 0)
		*val = le16toh(tmp[0].val) << 16 | le16toh(tmp[1].val);
	return (error);
}

static int
zyd_write16(struct zyd_softc *sc, uint16_t reg, uint16_t val)
{
	struct zyd_pair pair;

	pair.reg = htole16(reg);
	pair.val = htole16(val);

	return zyd_cmd(sc, ZYD_CMD_IOWR, &pair, sizeof(pair), NULL, 0, 0);
}

static int
zyd_write32(struct zyd_softc *sc, uint16_t reg, uint32_t val)
{
	struct zyd_pair pair[2];

	pair[0].reg = htole16(ZYD_REG32_HI(reg));
	pair[0].val = htole16(val >> 16);
	pair[1].reg = htole16(ZYD_REG32_LO(reg));
	pair[1].val = htole16(val & 0xffff);

	return zyd_cmd(sc, ZYD_CMD_IOWR, pair, sizeof(pair), NULL, 0, 0);
}

static int
zyd_rfwrite(struct zyd_softc *sc, uint32_t val)
{
	struct zyd_rf *rf = &sc->sc_rf;
	struct zyd_rfwrite_cmd req;
	uint16_t cr203;
	int error, i;

	zyd_read16_m(sc, ZYD_CR203, &cr203);
	cr203 &= ~(ZYD_RF_IF_LE | ZYD_RF_CLK | ZYD_RF_DATA);

	req.code  = htole16(2);
	req.width = htole16(rf->width);
	for (i = 0; i < rf->width; i++) {
		req.bit[i] = htole16(cr203);
		if (val & (1 << (rf->width - 1 - i)))
			req.bit[i] |= htole16(ZYD_RF_DATA);
	}
	error = zyd_cmd(sc, ZYD_CMD_RFCFG, &req, 4 + 2 * rf->width, NULL, 0, 0);
fail:
	return (error);
}

static int
zyd_rfwrite_cr(struct zyd_softc *sc, uint32_t val)
{
	int error;

	zyd_write16_m(sc, ZYD_CR244, (val >> 16) & 0xff);
	zyd_write16_m(sc, ZYD_CR243, (val >>  8) & 0xff);
	zyd_write16_m(sc, ZYD_CR242, (val >>  0) & 0xff);
fail:
	return (error);
}

static int
zyd_lock_phy(struct zyd_softc *sc)
{
	int error;
	uint32_t tmp;

	zyd_read32_m(sc, ZYD_MAC_MISC, &tmp);
	tmp &= ~ZYD_UNLOCK_PHY_REGS;
	zyd_write32_m(sc, ZYD_MAC_MISC, tmp);
fail:
	return (error);
}

static int
zyd_unlock_phy(struct zyd_softc *sc)
{
	int error;
	uint32_t tmp;

	zyd_read32_m(sc, ZYD_MAC_MISC, &tmp);
	tmp |= ZYD_UNLOCK_PHY_REGS;
	zyd_write32_m(sc, ZYD_MAC_MISC, tmp);
fail:
	return (error);
}

/*
 * RFMD RF methods.
 */
static int
zyd_rfmd_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_RFMD_PHY;
	static const uint32_t rfini[] = ZYD_RFMD_RF;
	int i, error;

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++) {
		zyd_write16_m(sc, phyini[i].reg, phyini[i].val);
	}

	/* init RFMD radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return (error);
	}
fail:
	return (error);
}

static int
zyd_rfmd_switch_radio(struct zyd_rf *rf, int on)
{
	int error;
	struct zyd_softc *sc = rf->rf_sc;

	zyd_write16_m(sc, ZYD_CR10, on ? 0x89 : 0x15);
	zyd_write16_m(sc, ZYD_CR11, on ? 0x00 : 0x81);
fail:
	return (error);
}

static int
zyd_rfmd_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	int error;
	struct zyd_softc *sc = rf->rf_sc;
	static const struct {
		uint32_t	r1, r2;
	} rfprog[] = ZYD_RFMD_CHANTABLE;

	error = zyd_rfwrite(sc, rfprog[chan - 1].r1);
	if (error != 0)
		goto fail;
	error = zyd_rfwrite(sc, rfprog[chan - 1].r2);
	if (error != 0)
		goto fail;

fail:
	return (error);
}

/*
 * AL2230 RF methods.
 */
static int
zyd_al2230_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_AL2230_PHY;
	static const struct zyd_phy_pair phy2230s[] = ZYD_AL2230S_PHY_INIT;
	static const struct zyd_phy_pair phypll[] = {
		{ ZYD_CR251, 0x2f }, { ZYD_CR251, 0x3f },
		{ ZYD_CR138, 0x28 }, { ZYD_CR203, 0x06 }
	};
	static const uint32_t rfini1[] = ZYD_AL2230_RF_PART1;
	static const uint32_t rfini2[] = ZYD_AL2230_RF_PART2;
	static const uint32_t rfini3[] = ZYD_AL2230_RF_PART3;
	int i, error;

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++)
		zyd_write16_m(sc, phyini[i].reg, phyini[i].val);

	if (sc->sc_rfrev == ZYD_RF_AL2230S || sc->sc_al2230s != 0) {
		for (i = 0; i < nitems(phy2230s); i++)
			zyd_write16_m(sc, phy2230s[i].reg, phy2230s[i].val);
	}

	/* init AL2230 radio */
	for (i = 0; i < nitems(rfini1); i++) {
		error = zyd_rfwrite(sc, rfini1[i]);
		if (error != 0)
			goto fail;
	}

	if (sc->sc_rfrev == ZYD_RF_AL2230S || sc->sc_al2230s != 0)
		error = zyd_rfwrite(sc, 0x000824);
	else
		error = zyd_rfwrite(sc, 0x0005a4);
	if (error != 0)
		goto fail;

	for (i = 0; i < nitems(rfini2); i++) {
		error = zyd_rfwrite(sc, rfini2[i]);
		if (error != 0)
			goto fail;
	}

	for (i = 0; i < nitems(phypll); i++)
		zyd_write16_m(sc, phypll[i].reg, phypll[i].val);

	for (i = 0; i < nitems(rfini3); i++) {
		error = zyd_rfwrite(sc, rfini3[i]);
		if (error != 0)
			goto fail;
	}
fail:
	return (error);
}

static int
zyd_al2230_fini(struct zyd_rf *rf)
{
	int error, i;
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phy[] = ZYD_AL2230_PHY_FINI_PART1;

	for (i = 0; i < nitems(phy); i++)
		zyd_write16_m(sc, phy[i].reg, phy[i].val);

	if (sc->sc_newphy != 0)
		zyd_write16_m(sc, ZYD_CR9, 0xe1);

	zyd_write16_m(sc, ZYD_CR203, 0x6);
fail:
	return (error);
}

static int
zyd_al2230_init_b(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phy1[] = ZYD_AL2230_PHY_PART1;
	static const struct zyd_phy_pair phy2[] = ZYD_AL2230_PHY_PART2;
	static const struct zyd_phy_pair phy3[] = ZYD_AL2230_PHY_PART3;
	static const struct zyd_phy_pair phy2230s[] = ZYD_AL2230S_PHY_INIT;
	static const struct zyd_phy_pair phyini[] = ZYD_AL2230_PHY_B;
	static const uint32_t rfini_part1[] = ZYD_AL2230_RF_B_PART1;
	static const uint32_t rfini_part2[] = ZYD_AL2230_RF_B_PART2;
	static const uint32_t rfini_part3[] = ZYD_AL2230_RF_B_PART3;
	static const uint32_t zyd_al2230_chtable[][3] = ZYD_AL2230_CHANTABLE;
	int i, error;

	for (i = 0; i < nitems(phy1); i++)
		zyd_write16_m(sc, phy1[i].reg, phy1[i].val);

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++)
		zyd_write16_m(sc, phyini[i].reg, phyini[i].val);

	if (sc->sc_rfrev == ZYD_RF_AL2230S || sc->sc_al2230s != 0) {
		for (i = 0; i < nitems(phy2230s); i++)
			zyd_write16_m(sc, phy2230s[i].reg, phy2230s[i].val);
	}

	for (i = 0; i < 3; i++) {
		error = zyd_rfwrite_cr(sc, zyd_al2230_chtable[0][i]);
		if (error != 0)
			return (error);
	}

	for (i = 0; i < nitems(rfini_part1); i++) {
		error = zyd_rfwrite_cr(sc, rfini_part1[i]);
		if (error != 0)
			return (error);
	}

	if (sc->sc_rfrev == ZYD_RF_AL2230S || sc->sc_al2230s != 0)
		error = zyd_rfwrite(sc, 0x241000);
	else
		error = zyd_rfwrite(sc, 0x25a000);
	if (error != 0)
		goto fail;

	for (i = 0; i < nitems(rfini_part2); i++) {
		error = zyd_rfwrite_cr(sc, rfini_part2[i]);
		if (error != 0)
			return (error);
	}

	for (i = 0; i < nitems(phy2); i++)
		zyd_write16_m(sc, phy2[i].reg, phy2[i].val);

	for (i = 0; i < nitems(rfini_part3); i++) {
		error = zyd_rfwrite_cr(sc, rfini_part3[i]);
		if (error != 0)
			return (error);
	}

	for (i = 0; i < nitems(phy3); i++)
		zyd_write16_m(sc, phy3[i].reg, phy3[i].val);

	error = zyd_al2230_fini(rf);
fail:
	return (error);
}

static int
zyd_al2230_switch_radio(struct zyd_rf *rf, int on)
{
	struct zyd_softc *sc = rf->rf_sc;
	int error, on251 = (sc->sc_macrev == ZYD_ZD1211) ? 0x3f : 0x7f;

	zyd_write16_m(sc, ZYD_CR11,  on ? 0x00 : 0x04);
	zyd_write16_m(sc, ZYD_CR251, on ? on251 : 0x2f);
fail:
	return (error);
}

static int
zyd_al2230_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	int error, i;
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phy1[] = {
		{ ZYD_CR138, 0x28 }, { ZYD_CR203, 0x06 },
	};
	static const struct {
		uint32_t	r1, r2, r3;
	} rfprog[] = ZYD_AL2230_CHANTABLE;

	error = zyd_rfwrite(sc, rfprog[chan - 1].r1);
	if (error != 0)
		goto fail;
	error = zyd_rfwrite(sc, rfprog[chan - 1].r2);
	if (error != 0)
		goto fail;
	error = zyd_rfwrite(sc, rfprog[chan - 1].r3);
	if (error != 0)
		goto fail;

	for (i = 0; i < nitems(phy1); i++)
		zyd_write16_m(sc, phy1[i].reg, phy1[i].val);
fail:
	return (error);
}

static int
zyd_al2230_set_channel_b(struct zyd_rf *rf, uint8_t chan)
{
	int error, i;
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phy1[] = ZYD_AL2230_PHY_PART1;
	static const struct {
		uint32_t	r1, r2, r3;
	} rfprog[] = ZYD_AL2230_CHANTABLE_B;

	for (i = 0; i < nitems(phy1); i++)
		zyd_write16_m(sc, phy1[i].reg, phy1[i].val);

	error = zyd_rfwrite_cr(sc, rfprog[chan - 1].r1);
	if (error != 0)
		goto fail;
	error = zyd_rfwrite_cr(sc, rfprog[chan - 1].r2);
	if (error != 0)
		goto fail;
	error = zyd_rfwrite_cr(sc, rfprog[chan - 1].r3);
	if (error != 0)
		goto fail;
	error = zyd_al2230_fini(rf);
fail:
	return (error);
}

#define	ZYD_AL2230_PHY_BANDEDGE6					\
{									\
	{ ZYD_CR128, 0x14 }, { ZYD_CR129, 0x12 }, { ZYD_CR130, 0x10 },	\
	{ ZYD_CR47,  0x1e }						\
}

static int
zyd_al2230_bandedge6(struct zyd_rf *rf, struct ieee80211_channel *c)
{
	int error = 0, i;
	struct zyd_softc *sc = rf->rf_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct zyd_phy_pair r[] = ZYD_AL2230_PHY_BANDEDGE6;
	int chan = ieee80211_chan2ieee(ic, c);

	if (chan == 1 || chan == 11)
		r[0].val = 0x12;
	
	for (i = 0; i < nitems(r); i++)
		zyd_write16_m(sc, r[i].reg, r[i].val);
fail:
	return (error);
}

/*
 * AL7230B RF methods.
 */
static int
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
	for (i = 0; i < nitems(phyini_1); i++)
		zyd_write16_m(sc, phyini_1[i].reg, phyini_1[i].val);

	/* init AL7230B radio, part one */
	for (i = 0; i < nitems(rfini_1); i++) {
		if ((error = zyd_rfwrite(sc, rfini_1[i])) != 0)
			return (error);
	}
	/* init RF-dependent PHY registers, part two */
	for (i = 0; i < nitems(phyini_2); i++)
		zyd_write16_m(sc, phyini_2[i].reg, phyini_2[i].val);

	/* init AL7230B radio, part two */
	for (i = 0; i < nitems(rfini_2); i++) {
		if ((error = zyd_rfwrite(sc, rfini_2[i])) != 0)
			return (error);
	}
	/* init RF-dependent PHY registers, part three */
	for (i = 0; i < nitems(phyini_3); i++)
		zyd_write16_m(sc, phyini_3[i].reg, phyini_3[i].val);
fail:
	return (error);
}

static int
zyd_al7230B_switch_radio(struct zyd_rf *rf, int on)
{
	int error;
	struct zyd_softc *sc = rf->rf_sc;

	zyd_write16_m(sc, ZYD_CR11,  on ? 0x00 : 0x04);
	zyd_write16_m(sc, ZYD_CR251, on ? 0x3f : 0x2f);
fail:
	return (error);
}

static int
zyd_al7230B_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct {
		uint32_t	r1, r2;
	} rfprog[] = ZYD_AL7230B_CHANTABLE;
	static const uint32_t rfsc[] = ZYD_AL7230B_RF_SETCHANNEL;
	int i, error;

	zyd_write16_m(sc, ZYD_CR240, 0x57);
	zyd_write16_m(sc, ZYD_CR251, 0x2f);

	for (i = 0; i < nitems(rfsc); i++) {
		if ((error = zyd_rfwrite(sc, rfsc[i])) != 0)
			return (error);
	}

	zyd_write16_m(sc, ZYD_CR128, 0x14);
	zyd_write16_m(sc, ZYD_CR129, 0x12);
	zyd_write16_m(sc, ZYD_CR130, 0x10);
	zyd_write16_m(sc, ZYD_CR38,  0x38);
	zyd_write16_m(sc, ZYD_CR136, 0xdf);

	error = zyd_rfwrite(sc, rfprog[chan - 1].r1);
	if (error != 0)
		goto fail;
	error = zyd_rfwrite(sc, rfprog[chan - 1].r2);
	if (error != 0)
		goto fail;
	error = zyd_rfwrite(sc, 0x3c9000);
	if (error != 0)
		goto fail;

	zyd_write16_m(sc, ZYD_CR251, 0x3f);
	zyd_write16_m(sc, ZYD_CR203, 0x06);
	zyd_write16_m(sc, ZYD_CR240, 0x08);
fail:
	return (error);
}

/*
 * AL2210 RF methods.
 */
static int
zyd_al2210_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_AL2210_PHY;
	static const uint32_t rfini[] = ZYD_AL2210_RF;
	uint32_t tmp;
	int i, error;

	zyd_write32_m(sc, ZYD_CR18, 2);

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++)
		zyd_write16_m(sc, phyini[i].reg, phyini[i].val);

	/* init AL2210 radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return (error);
	}
	zyd_write16_m(sc, ZYD_CR47, 0x1e);
	zyd_read32_m(sc, ZYD_CR_RADIO_PD, &tmp);
	zyd_write32_m(sc, ZYD_CR_RADIO_PD, tmp & ~1);
	zyd_write32_m(sc, ZYD_CR_RADIO_PD, tmp | 1);
	zyd_write32_m(sc, ZYD_CR_RFCFG, 0x05);
	zyd_write32_m(sc, ZYD_CR_RFCFG, 0x00);
	zyd_write16_m(sc, ZYD_CR47, 0x1e);
	zyd_write32_m(sc, ZYD_CR18, 3);
fail:
	return (error);
}

static int
zyd_al2210_switch_radio(struct zyd_rf *rf, int on)
{
	/* vendor driver does nothing for this RF chip */

	return (0);
}

static int
zyd_al2210_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	int error;
	struct zyd_softc *sc = rf->rf_sc;
	static const uint32_t rfprog[] = ZYD_AL2210_CHANTABLE;
	uint32_t tmp;

	zyd_write32_m(sc, ZYD_CR18, 2);
	zyd_write16_m(sc, ZYD_CR47, 0x1e);
	zyd_read32_m(sc, ZYD_CR_RADIO_PD, &tmp);
	zyd_write32_m(sc, ZYD_CR_RADIO_PD, tmp & ~1);
	zyd_write32_m(sc, ZYD_CR_RADIO_PD, tmp | 1);
	zyd_write32_m(sc, ZYD_CR_RFCFG, 0x05);
	zyd_write32_m(sc, ZYD_CR_RFCFG, 0x00);
	zyd_write16_m(sc, ZYD_CR47, 0x1e);

	/* actually set the channel */
	error = zyd_rfwrite(sc, rfprog[chan - 1]);
	if (error != 0)
		goto fail;

	zyd_write32_m(sc, ZYD_CR18, 3);
fail:
	return (error);
}

/*
 * GCT RF methods.
 */
static int
zyd_gct_init(struct zyd_rf *rf)
{
#define	ZYD_GCT_INTR_REG	0x85c1
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_GCT_PHY;
	static const uint32_t rfini[] = ZYD_GCT_RF;
	static const uint16_t vco[11][7] = ZYD_GCT_VCO;
	int i, idx = -1, error;
	uint16_t data;

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++)
		zyd_write16_m(sc, phyini[i].reg, phyini[i].val);

	/* init cgt radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return (error);
	}

	error = zyd_gct_mode(rf);
	if (error != 0)
		return (error);

	for (i = 0; i < (int)(nitems(vco) - 1); i++) {
		error = zyd_gct_set_channel_synth(rf, 1, 0);
		if (error != 0)
			goto fail;
		error = zyd_gct_write(rf, vco[i][0]);
		if (error != 0)
			goto fail;
		zyd_write16_m(sc, ZYD_GCT_INTR_REG, 0xf);
		zyd_read16_m(sc, ZYD_GCT_INTR_REG, &data);
		if ((data & 0xf) == 0) {
			idx = i;
			break;
		}
	}
	if (idx == -1) {
		error = zyd_gct_set_channel_synth(rf, 1, 1);
		if (error != 0)
			goto fail;
		error = zyd_gct_write(rf, 0x6662);
		if (error != 0)
			goto fail;
	}

	rf->idx = idx;
	zyd_write16_m(sc, ZYD_CR203, 0x6);
fail:
	return (error);
#undef ZYD_GCT_INTR_REG
}

static int
zyd_gct_mode(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const uint32_t mode[] = {
		0x25f98, 0x25f9a, 0x25f94, 0x27fd4
	};
	int i, error;

	for (i = 0; i < nitems(mode); i++) {
		if ((error = zyd_rfwrite(sc, mode[i])) != 0)
			break;
	}
	return (error);
}

static int
zyd_gct_set_channel_synth(struct zyd_rf *rf, int chan, int acal)
{
	int error, idx = chan - 1;
	struct zyd_softc *sc = rf->rf_sc;
	static uint32_t acal_synth[] = ZYD_GCT_CHANNEL_ACAL;
	static uint32_t std_synth[] = ZYD_GCT_CHANNEL_STD;
	static uint32_t div_synth[] = ZYD_GCT_CHANNEL_DIV;

	error = zyd_rfwrite(sc,
	    (acal == 1) ? acal_synth[idx] : std_synth[idx]);
	if (error != 0)
		return (error);
	return zyd_rfwrite(sc, div_synth[idx]);
}

static int
zyd_gct_write(struct zyd_rf *rf, uint16_t value)
{
	struct zyd_softc *sc = rf->rf_sc;

	return zyd_rfwrite(sc, 0x300000 | 0x40000 | value);
}

static int
zyd_gct_switch_radio(struct zyd_rf *rf, int on)
{
	int error;
	struct zyd_softc *sc = rf->rf_sc;

	error = zyd_rfwrite(sc, on ? 0x25f94 : 0x25f90);
	if (error != 0)
		return (error);

	zyd_write16_m(sc, ZYD_CR11, on ? 0x00 : 0x04);
	zyd_write16_m(sc, ZYD_CR251,
	    on ? ((sc->sc_macrev == ZYD_ZD1211B) ? 0x7f : 0x3f) : 0x2f);
fail:
	return (error);
}

static int
zyd_gct_set_channel(struct zyd_rf *rf, uint8_t chan)
{
	int error, i;
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair cmd[] = {
		{ ZYD_CR80, 0x30 }, { ZYD_CR81, 0x30 }, { ZYD_CR79, 0x58 },
		{ ZYD_CR12, 0xf0 }, { ZYD_CR77, 0x1b }, { ZYD_CR78, 0x58 },
	};
	static const uint16_t vco[11][7] = ZYD_GCT_VCO;

	error = zyd_gct_set_channel_synth(rf, chan, 0);
	if (error != 0)
		goto fail;
	error = zyd_gct_write(rf, (rf->idx == -1) ? 0x6662 :
	    vco[rf->idx][((chan - 1) / 2)]);
	if (error != 0)
		goto fail;
	error = zyd_gct_mode(rf);
	if (error != 0)
		return (error);
	for (i = 0; i < nitems(cmd); i++)
		zyd_write16_m(sc, cmd[i].reg, cmd[i].val);
	error = zyd_gct_txgain(rf, chan);
	if (error != 0)
		return (error);
	zyd_write16_m(sc, ZYD_CR203, 0x6);
fail:
	return (error);
}

static int
zyd_gct_txgain(struct zyd_rf *rf, uint8_t chan)
{
	struct zyd_softc *sc = rf->rf_sc;
	static uint32_t txgain[] = ZYD_GCT_TXGAIN;
	uint8_t idx = sc->sc_pwrint[chan - 1];

	if (idx >= nitems(txgain)) {
		device_printf(sc->sc_dev, "could not set TX gain (%d %#x)\n",
		    chan, idx);
		return 0;
	}

	return zyd_rfwrite(sc, 0x700000 | txgain[idx]);
}

/*
 * Maxim2 RF methods.
 */
static int
zyd_maxim2_init(struct zyd_rf *rf)
{
	struct zyd_softc *sc = rf->rf_sc;
	static const struct zyd_phy_pair phyini[] = ZYD_MAXIM2_PHY;
	static const uint32_t rfini[] = ZYD_MAXIM2_RF;
	uint16_t tmp;
	int i, error;

	/* init RF-dependent PHY registers */
	for (i = 0; i < nitems(phyini); i++)
		zyd_write16_m(sc, phyini[i].reg, phyini[i].val);

	zyd_read16_m(sc, ZYD_CR203, &tmp);
	zyd_write16_m(sc, ZYD_CR203, tmp & ~(1 << 4));

	/* init maxim2 radio */
	for (i = 0; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return (error);
	}
	zyd_read16_m(sc, ZYD_CR203, &tmp);
	zyd_write16_m(sc, ZYD_CR203, tmp | (1 << 4));
fail:
	return (error);
}

static int
zyd_maxim2_switch_radio(struct zyd_rf *rf, int on)
{

	/* vendor driver does nothing for this RF chip */
	return (0);
}

static int
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
	for (i = 0; i < nitems(phyini); i++)
		zyd_write16_m(sc, phyini[i].reg, phyini[i].val);

	zyd_read16_m(sc, ZYD_CR203, &tmp);
	zyd_write16_m(sc, ZYD_CR203, tmp & ~(1 << 4));

	/* first two values taken from the chantables */
	error = zyd_rfwrite(sc, rfprog[chan - 1].r1);
	if (error != 0)
		goto fail;
	error = zyd_rfwrite(sc, rfprog[chan - 1].r2);
	if (error != 0)
		goto fail;

	/* init maxim2 radio - skipping the two first values */
	for (i = 2; i < nitems(rfini); i++) {
		if ((error = zyd_rfwrite(sc, rfini[i])) != 0)
			return (error);
	}
	zyd_read16_m(sc, ZYD_CR203, &tmp);
	zyd_write16_m(sc, ZYD_CR203, tmp | (1 << 4));
fail:
	return (error);
}

static int
zyd_rf_attach(struct zyd_softc *sc, uint8_t type)
{
	struct zyd_rf *rf = &sc->sc_rf;

	rf->rf_sc = sc;
	rf->update_pwr = 1;

	switch (type) {
	case ZYD_RF_RFMD:
		rf->init         = zyd_rfmd_init;
		rf->switch_radio = zyd_rfmd_switch_radio;
		rf->set_channel  = zyd_rfmd_set_channel;
		rf->width        = 24;	/* 24-bit RF values */
		break;
	case ZYD_RF_AL2230:
	case ZYD_RF_AL2230S:
		if (sc->sc_macrev == ZYD_ZD1211B) {
			rf->init = zyd_al2230_init_b;
			rf->set_channel = zyd_al2230_set_channel_b;
		} else {
			rf->init = zyd_al2230_init;
			rf->set_channel = zyd_al2230_set_channel;
		}
		rf->switch_radio = zyd_al2230_switch_radio;
		rf->bandedge6	 = zyd_al2230_bandedge6;
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
	case ZYD_RF_MAXIM_NEW:
	case ZYD_RF_GCT:
		rf->init         = zyd_gct_init;
		rf->switch_radio = zyd_gct_switch_radio;
		rf->set_channel  = zyd_gct_set_channel;
		rf->width        = 24;	/* 24-bit RF values */
		rf->update_pwr   = 0;
		break;
	case ZYD_RF_MAXIM_NEW2:
		rf->init         = zyd_maxim2_init;
		rf->switch_radio = zyd_maxim2_switch_radio;
		rf->set_channel  = zyd_maxim2_set_channel;
		rf->width        = 18;	/* 18-bit RF values */
		break;
	default:
		device_printf(sc->sc_dev,
		    "sorry, radio \"%s\" is not supported yet\n",
		    zyd_rf_name(type));
		return (EINVAL);
	}
	return (0);
}

static const char *
zyd_rf_name(uint8_t type)
{
	static const char * const zyd_rfs[] = {
		"unknown", "unknown", "UW2451",   "UCHIP",     "AL2230",
		"AL7230B", "THETA",   "AL2210",   "MAXIM_NEW", "GCT",
		"AL2230S",  "RALINK",  "INTERSIL", "RFMD",      "MAXIM_NEW2",
		"PHILIPS"
	};

	return zyd_rfs[(type > 15) ? 0 : type];
}

static int
zyd_hw_init(struct zyd_softc *sc)
{
	int error;
	const struct zyd_phy_pair *phyp;
	struct zyd_rf *rf = &sc->sc_rf;
	uint16_t val;

	/* specify that the plug and play is finished */
	zyd_write32_m(sc, ZYD_MAC_AFTER_PNP, 1);
	zyd_read16_m(sc, ZYD_FIRMWARE_BASE_ADDR, &sc->sc_fwbase);
	DPRINTF(sc, ZYD_DEBUG_FW, "firmware base address=0x%04x\n",
	    sc->sc_fwbase);

	/* retrieve firmware revision number */
	zyd_read16_m(sc, sc->sc_fwbase + ZYD_FW_FIRMWARE_REV, &sc->sc_fwrev);
	zyd_write32_m(sc, ZYD_CR_GPI_EN, 0);
	zyd_write32_m(sc, ZYD_MAC_CONT_WIN_LIMIT, 0x7f043f);
	/* set mandatory rates - XXX assumes 802.11b/g */
	zyd_write32_m(sc, ZYD_MAC_MAN_RATE, 0x150f);

	/* disable interrupts */
	zyd_write32_m(sc, ZYD_CR_INTERRUPT, 0);

	if ((error = zyd_read_pod(sc)) != 0) {
		device_printf(sc->sc_dev, "could not read EEPROM\n");
		goto fail;
	}

	/* PHY init (resetting) */
	error = zyd_lock_phy(sc);
	if (error != 0)
		goto fail;
	phyp = (sc->sc_macrev == ZYD_ZD1211B) ? zyd_def_phyB : zyd_def_phy;
	for (; phyp->reg != 0; phyp++)
		zyd_write16_m(sc, phyp->reg, phyp->val);
	if (sc->sc_macrev == ZYD_ZD1211 && sc->sc_fix_cr157 != 0) {
		zyd_read16_m(sc, ZYD_EEPROM_PHY_REG, &val);
		zyd_write32_m(sc, ZYD_CR157, val >> 8);
	}
	error = zyd_unlock_phy(sc);
	if (error != 0)
		goto fail;

	/* HMAC init */
	zyd_write32_m(sc, ZYD_MAC_ACK_EXT, 0x00000020);
	zyd_write32_m(sc, ZYD_CR_ADDA_MBIAS_WT, 0x30000808);
	zyd_write32_m(sc, ZYD_MAC_SNIFFER, 0x00000000);
	zyd_write32_m(sc, ZYD_MAC_RXFILTER, 0x00000000);
	zyd_write32_m(sc, ZYD_MAC_GHTBL, 0x00000000);
	zyd_write32_m(sc, ZYD_MAC_GHTBH, 0x80000000);
	zyd_write32_m(sc, ZYD_MAC_MISC, 0x000000a4);
	zyd_write32_m(sc, ZYD_CR_ADDA_PWR_DWN, 0x0000007f);
	zyd_write32_m(sc, ZYD_MAC_BCNCFG, 0x00f00401);
	zyd_write32_m(sc, ZYD_MAC_PHY_DELAY2, 0x00000000);
	zyd_write32_m(sc, ZYD_MAC_ACK_EXT, 0x00000080);
	zyd_write32_m(sc, ZYD_CR_ADDA_PWR_DWN, 0x00000000);
	zyd_write32_m(sc, ZYD_MAC_SIFS_ACK_TIME, 0x00000100);
	zyd_write32_m(sc, ZYD_CR_RX_PE_DELAY, 0x00000070);
	zyd_write32_m(sc, ZYD_CR_PS_CTRL, 0x10000000);
	zyd_write32_m(sc, ZYD_MAC_RTSCTSRATE, 0x02030203);
	zyd_write32_m(sc, ZYD_MAC_AFTER_PNP, 1);
	zyd_write32_m(sc, ZYD_MAC_BACKOFF_PROTECT, 0x00000114);
	zyd_write32_m(sc, ZYD_MAC_DIFS_EIFS_SIFS, 0x0a47c032);
	zyd_write32_m(sc, ZYD_MAC_CAM_MODE, 0x3);

	if (sc->sc_macrev == ZYD_ZD1211) {
		zyd_write32_m(sc, ZYD_MAC_RETRY, 0x00000002);
		zyd_write32_m(sc, ZYD_MAC_RX_THRESHOLD, 0x000c0640);
	} else {
		zyd_write32_m(sc, ZYD_MACB_MAX_RETRY, 0x02020202);
		zyd_write32_m(sc, ZYD_MACB_TXPWR_CTL4, 0x007f003f);
		zyd_write32_m(sc, ZYD_MACB_TXPWR_CTL3, 0x007f003f);
		zyd_write32_m(sc, ZYD_MACB_TXPWR_CTL2, 0x003f001f);
		zyd_write32_m(sc, ZYD_MACB_TXPWR_CTL1, 0x001f000f);
		zyd_write32_m(sc, ZYD_MACB_AIFS_CTL1, 0x00280028);
		zyd_write32_m(sc, ZYD_MACB_AIFS_CTL2, 0x008C003C);
		zyd_write32_m(sc, ZYD_MACB_TXOP, 0x01800824);
		zyd_write32_m(sc, ZYD_MAC_RX_THRESHOLD, 0x000c0eff);
	}

	/* init beacon interval to 100ms */
	if ((error = zyd_set_beacon_interval(sc, 100)) != 0)
		goto fail;

	if ((error = zyd_rf_attach(sc, sc->sc_rfrev)) != 0) {
		device_printf(sc->sc_dev, "could not attach RF, rev 0x%x\n",
		    sc->sc_rfrev);
		goto fail;
	}

	/* RF chip init */
	error = zyd_lock_phy(sc);
	if (error != 0)
		goto fail;
	error = (*rf->init)(rf);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "radio initialization failed, error %d\n", error);
		goto fail;
	}
	error = zyd_unlock_phy(sc);
	if (error != 0)
		goto fail;

	if ((error = zyd_read_eeprom(sc)) != 0) {
		device_printf(sc->sc_dev, "could not read EEPROM\n");
		goto fail;
	}

fail:	return (error);
}

static int
zyd_read_pod(struct zyd_softc *sc)
{
	int error;
	uint32_t tmp;

	zyd_read32_m(sc, ZYD_EEPROM_POD, &tmp);
	sc->sc_rfrev     = tmp & 0x0f;
	sc->sc_ledtype   = (tmp >>  4) & 0x01;
	sc->sc_al2230s   = (tmp >>  7) & 0x01;
	sc->sc_cckgain   = (tmp >>  8) & 0x01;
	sc->sc_fix_cr157 = (tmp >> 13) & 0x01;
	sc->sc_parev     = (tmp >> 16) & 0x0f;
	sc->sc_bandedge6 = (tmp >> 21) & 0x01;
	sc->sc_newphy    = (tmp >> 31) & 0x01;
	sc->sc_txled     = ((tmp & (1 << 24)) && (tmp & (1 << 29))) ? 0 : 1;
fail:
	return (error);
}

static int
zyd_read_eeprom(struct zyd_softc *sc)
{
	uint16_t val;
	int error, i;

	/* read Tx power calibration tables */
	for (i = 0; i < 7; i++) {
		zyd_read16_m(sc, ZYD_EEPROM_PWR_CAL + i, &val);
		sc->sc_pwrcal[i * 2] = val >> 8;
		sc->sc_pwrcal[i * 2 + 1] = val & 0xff;
		zyd_read16_m(sc, ZYD_EEPROM_PWR_INT + i, &val);
		sc->sc_pwrint[i * 2] = val >> 8;
		sc->sc_pwrint[i * 2 + 1] = val & 0xff;
		zyd_read16_m(sc, ZYD_EEPROM_36M_CAL + i, &val);
		sc->sc_ofdm36_cal[i * 2] = val >> 8;
		sc->sc_ofdm36_cal[i * 2 + 1] = val & 0xff;
		zyd_read16_m(sc, ZYD_EEPROM_48M_CAL + i, &val);
		sc->sc_ofdm48_cal[i * 2] = val >> 8;
		sc->sc_ofdm48_cal[i * 2 + 1] = val & 0xff;
		zyd_read16_m(sc, ZYD_EEPROM_54M_CAL + i, &val);
		sc->sc_ofdm54_cal[i * 2] = val >> 8;
		sc->sc_ofdm54_cal[i * 2 + 1] = val & 0xff;
	}
fail:
	return (error);
}

static int
zyd_get_macaddr(struct zyd_softc *sc)
{
	struct usb_device_request req;
	usb_error_t error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = ZYD_READFWDATAREQ;
	USETW(req.wValue, ZYD_EEPROM_MAC_ADDR_P1);
	USETW(req.wIndex, 0);
	USETW(req.wLength, IEEE80211_ADDR_LEN);

	error = zyd_do_request(sc, &req, sc->sc_ic.ic_macaddr);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not read EEPROM: %s\n",
		    usbd_errstr(error));
	}

	return (error);
}

static int
zyd_set_macaddr(struct zyd_softc *sc, const uint8_t *addr)
{
	int error;
	uint32_t tmp;

	tmp = addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0];
	zyd_write32_m(sc, ZYD_MAC_MACADRL, tmp);
	tmp = addr[5] << 8 | addr[4];
	zyd_write32_m(sc, ZYD_MAC_MACADRH, tmp);
fail:
	return (error);
}

static int
zyd_set_bssid(struct zyd_softc *sc, const uint8_t *addr)
{
	int error;
	uint32_t tmp;

	tmp = addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0];
	zyd_write32_m(sc, ZYD_MAC_BSSADRL, tmp);
	tmp = addr[5] << 8 | addr[4];
	zyd_write32_m(sc, ZYD_MAC_BSSADRH, tmp);
fail:
	return (error);
}

static int
zyd_switch_radio(struct zyd_softc *sc, int on)
{
	struct zyd_rf *rf = &sc->sc_rf;
	int error;

	error = zyd_lock_phy(sc);
	if (error != 0)
		goto fail;
	error = (*rf->switch_radio)(rf, on);
	if (error != 0)
		goto fail;
	error = zyd_unlock_phy(sc);
fail:
	return (error);
}

static int
zyd_set_led(struct zyd_softc *sc, int which, int on)
{
	int error;
	uint32_t tmp;

	zyd_read32_m(sc, ZYD_MAC_TX_PE_CONTROL, &tmp);
	tmp &= ~which;
	if (on)
		tmp |= which;
	zyd_write32_m(sc, ZYD_MAC_TX_PE_CONTROL, tmp);
fail:
	return (error);
}

static void
zyd_set_multi(struct zyd_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t low, high;
	int error;

	if ((sc->sc_flags & ZYD_FLAG_RUNNING) == 0)
		return;

	low = 0x00000000;
	high = 0x80000000;

	if (ic->ic_opmode == IEEE80211_M_MONITOR || ic->ic_allmulti > 0 ||
	    ic->ic_promisc > 0) {
		low = 0xffffffff;
		high = 0xffffffff;
	} else {
		struct ieee80211vap *vap;
		struct ifnet *ifp;
		struct ifmultiaddr *ifma;
		uint8_t v;

		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
			ifp = vap->iv_ifp;
			if_maddr_rlock(ifp);
			CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
				if (ifma->ifma_addr->sa_family != AF_LINK)
					continue;
				v = ((uint8_t *)LLADDR((struct sockaddr_dl *)
				    ifma->ifma_addr))[5] >> 2;
				if (v < 32)
					low |= 1 << v;
				else
					high |= 1 << (v - 32);
			}
			if_maddr_runlock(ifp);
		}
	}

	/* reprogram multicast global hash table */
	zyd_write32_m(sc, ZYD_MAC_GHTBL, low);
	zyd_write32_m(sc, ZYD_MAC_GHTBH, high);
fail:
	if (error != 0)
		device_printf(sc->sc_dev,
		    "could not set multicast hash table\n");
}

static void
zyd_update_mcast(struct ieee80211com *ic)
{
	struct zyd_softc *sc = ic->ic_softc;

	ZYD_LOCK(sc);
	zyd_set_multi(sc);
	ZYD_UNLOCK(sc);
}

static int
zyd_set_rxfilter(struct zyd_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t rxfilter;

	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		rxfilter = ZYD_FILTER_BSS;
		break;
	case IEEE80211_M_IBSS:
	case IEEE80211_M_HOSTAP:
		rxfilter = ZYD_FILTER_HOSTAP;
		break;
	case IEEE80211_M_MONITOR:
		rxfilter = ZYD_FILTER_MONITOR;
		break;
	default:
		/* should not get there */
		return (EINVAL);
	}
	return zyd_write32(sc, ZYD_MAC_RXFILTER, rxfilter);
}

static void
zyd_set_chan(struct zyd_softc *sc, struct ieee80211_channel *c)
{
	int error;
	struct ieee80211com *ic = &sc->sc_ic;
	struct zyd_rf *rf = &sc->sc_rf;
	uint32_t tmp;
	int chan;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY) {
		/* XXX should NEVER happen */
		device_printf(sc->sc_dev,
		    "%s: invalid channel %x\n", __func__, chan);
		return;
	}

	error = zyd_lock_phy(sc);
	if (error != 0)
		goto fail;

	error = (*rf->set_channel)(rf, chan);
	if (error != 0)
		goto fail;

	if (rf->update_pwr) {
		/* update Tx power */
		zyd_write16_m(sc, ZYD_CR31, sc->sc_pwrint[chan - 1]);

		if (sc->sc_macrev == ZYD_ZD1211B) {
			zyd_write16_m(sc, ZYD_CR67,
			    sc->sc_ofdm36_cal[chan - 1]);
			zyd_write16_m(sc, ZYD_CR66,
			    sc->sc_ofdm48_cal[chan - 1]);
			zyd_write16_m(sc, ZYD_CR65,
			    sc->sc_ofdm54_cal[chan - 1]);
			zyd_write16_m(sc, ZYD_CR68, sc->sc_pwrcal[chan - 1]);
			zyd_write16_m(sc, ZYD_CR69, 0x28);
			zyd_write16_m(sc, ZYD_CR69, 0x2a);
		}
	}
	if (sc->sc_cckgain) {
		/* set CCK baseband gain from EEPROM */
		if (zyd_read32(sc, ZYD_EEPROM_PHY_REG, &tmp) == 0)
			zyd_write16_m(sc, ZYD_CR47, tmp & 0xff);
	}
	if (sc->sc_bandedge6 && rf->bandedge6 != NULL) {
		error = (*rf->bandedge6)(rf, c);
		if (error != 0)
			goto fail;
	}
	zyd_write32_m(sc, ZYD_CR_CONFIG_PHILIPS, 0);

	error = zyd_unlock_phy(sc);
	if (error != 0)
		goto fail;

	sc->sc_rxtap.wr_chan_freq = sc->sc_txtap.wt_chan_freq =
	    htole16(c->ic_freq);
	sc->sc_rxtap.wr_chan_flags = sc->sc_txtap.wt_chan_flags =
	    htole16(c->ic_flags);
fail:
	return;
}

static int
zyd_set_beacon_interval(struct zyd_softc *sc, int bintval)
{
	int error;
	uint32_t val;

	zyd_read32_m(sc, ZYD_CR_ATIM_WND_PERIOD, &val);
	sc->sc_atim_wnd = val;
	zyd_read32_m(sc, ZYD_CR_PRE_TBTT, &val);
	sc->sc_pre_tbtt = val;
	sc->sc_bcn_int = bintval;

	if (sc->sc_bcn_int <= 5)
		sc->sc_bcn_int = 5;
	if (sc->sc_pre_tbtt < 4 || sc->sc_pre_tbtt >= sc->sc_bcn_int)
		sc->sc_pre_tbtt = sc->sc_bcn_int - 1;
	if (sc->sc_atim_wnd >= sc->sc_pre_tbtt)
		sc->sc_atim_wnd = sc->sc_pre_tbtt - 1;

	zyd_write32_m(sc, ZYD_CR_ATIM_WND_PERIOD, sc->sc_atim_wnd);
	zyd_write32_m(sc, ZYD_CR_PRE_TBTT, sc->sc_pre_tbtt);
	zyd_write32_m(sc, ZYD_CR_BCN_INTERVAL, sc->sc_bcn_int);
fail:
	return (error);
}

static void
zyd_rx_data(struct usb_xfer *xfer, int offset, uint16_t len)
{
	struct zyd_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct zyd_plcphdr plcp;
	struct zyd_rx_stat stat;
	struct usb_page_cache *pc;
	struct mbuf *m;
	int rlen, rssi;

	if (len < ZYD_MIN_FRAGSZ) {
		DPRINTF(sc, ZYD_DEBUG_RECV, "%s: frame too short (length=%d)\n",
		    device_get_nameunit(sc->sc_dev), len);
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}
	pc = usbd_xfer_get_frame(xfer, 0);
	usbd_copy_out(pc, offset, &plcp, sizeof(plcp));
	usbd_copy_out(pc, offset + len - sizeof(stat), &stat, sizeof(stat));

	if (stat.flags & ZYD_RX_ERROR) {
		DPRINTF(sc, ZYD_DEBUG_RECV,
		    "%s: RX status indicated error (%x)\n",
		    device_get_nameunit(sc->sc_dev), stat.flags);
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}

	/* compute actual frame length */
	rlen = len - sizeof(struct zyd_plcphdr) -
	    sizeof(struct zyd_rx_stat) - IEEE80211_CRC_LEN;

	/* allocate a mbuf to store the frame */
	if (rlen > (int)MCLBYTES) {
		DPRINTF(sc, ZYD_DEBUG_RECV, "%s: frame too long (length=%d)\n",
		    device_get_nameunit(sc->sc_dev), rlen);
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	} else if (rlen > (int)MHLEN)
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	else
		m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL) {
		DPRINTF(sc, ZYD_DEBUG_RECV, "%s: could not allocate rx mbuf\n",
		    device_get_nameunit(sc->sc_dev));
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}
	m->m_pkthdr.len = m->m_len = rlen;
	usbd_copy_out(pc, offset + sizeof(plcp), mtod(m, uint8_t *), rlen);

	if (ieee80211_radiotap_active(ic)) {
		struct zyd_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		if (stat.flags & (ZYD_RX_BADCRC16 | ZYD_RX_BADCRC32))
			tap->wr_flags |= IEEE80211_RADIOTAP_F_BADFCS;
		/* XXX toss, no way to express errors */
		if (stat.flags & ZYD_RX_DECRYPTERR)
			tap->wr_flags |= IEEE80211_RADIOTAP_F_BADFCS;
		tap->wr_rate = ieee80211_plcp2rate(plcp.signal,
		    (stat.flags & ZYD_RX_OFDM) ?
			IEEE80211_T_OFDM : IEEE80211_T_CCK);
		tap->wr_antsignal = stat.rssi + -95;
		tap->wr_antnoise = -95;	/* XXX */
	}
	rssi = (stat.rssi > 63) ? 127 : 2 * stat.rssi;

	sc->sc_rx_data[sc->sc_rx_count].rssi = rssi;
	sc->sc_rx_data[sc->sc_rx_count].m = m;
	sc->sc_rx_count++;
}

static void
zyd_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct zyd_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct zyd_rx_desc desc;
	struct mbuf *m;
	struct usb_page_cache *pc;
	uint32_t offset;
	uint8_t rssi;
	int8_t nf;
	int i;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	sc->sc_rx_count = 0;
	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, actlen - sizeof(desc), &desc, sizeof(desc));

		offset = 0;
		if (UGETW(desc.tag) == ZYD_TAG_MULTIFRAME) {
			DPRINTF(sc, ZYD_DEBUG_RECV,
			    "%s: received multi-frame transfer\n", __func__);

			for (i = 0; i < ZYD_MAX_RXFRAMECNT; i++) {
				uint16_t len16 = UGETW(desc.len[i]);

				if (len16 == 0 || len16 > actlen)
					break;

				zyd_rx_data(xfer, offset, len16);

				/* next frame is aligned on a 32-bit boundary */
				len16 = (len16 + 3) & ~3;
				offset += len16;
				if (len16 > actlen)
					break;
				actlen -= len16;
			}
		} else {
			DPRINTF(sc, ZYD_DEBUG_RECV,
			    "%s: received single-frame transfer\n", __func__);

			zyd_rx_data(xfer, 0, actlen);
		}
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);

		/*
		 * At the end of a USB callback it is always safe to unlock
		 * the private mutex of a device! That is why we do the
		 * "ieee80211_input" here, and not some lines up!
		 */
		ZYD_UNLOCK(sc);
		for (i = 0; i < sc->sc_rx_count; i++) {
			rssi = sc->sc_rx_data[i].rssi;
			m = sc->sc_rx_data[i].m;
			sc->sc_rx_data[i].m = NULL;

			nf = -95;	/* XXX */

			ni = ieee80211_find_rxnode(ic,
			    mtod(m, struct ieee80211_frame_min *));
			if (ni != NULL) {
				(void)ieee80211_input(ni, m, rssi, nf);
				ieee80211_free_node(ni);
			} else
				(void)ieee80211_input_all(ic, m, rssi, nf);
		}
		ZYD_LOCK(sc);
		zyd_start(sc);
		break;

	default:			/* Error */
		DPRINTF(sc, ZYD_DEBUG_ANY, "frame error: %s\n", usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static uint8_t
zyd_plcp_signal(struct zyd_softc *sc, int rate)
{
	switch (rate) {
	/* OFDM rates (cf IEEE Std 802.11a-1999, pp. 14 Table 80) */
	case 12:
		return (0xb);
	case 18:
		return (0xf);
	case 24:
		return (0xa);
	case 36:
		return (0xe);
	case 48:
		return (0x9);
	case 72:
		return (0xd);
	case 96:
		return (0x8);
	case 108:
		return (0xc);
	/* CCK rates (NB: not IEEE std, device-specific) */
	case 2:
		return (0x0);
	case 4:
		return (0x1);
	case 11:
		return (0x2);
	case 22:
		return (0x3);
	}

	device_printf(sc->sc_dev, "unsupported rate %d\n", rate);
	return (0x0);
}

static void
zyd_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct zyd_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211vap *vap;
	struct zyd_tx_data *data;
	struct mbuf *m;
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTF(sc, ZYD_DEBUG_ANY, "transfer complete, %u bytes\n",
		    actlen);

		/* free resources */
		data = usbd_xfer_get_priv(xfer);
		zyd_tx_free(data, 0);
		usbd_xfer_set_priv(xfer, NULL);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		data = STAILQ_FIRST(&sc->tx_q);
		if (data) {
			STAILQ_REMOVE_HEAD(&sc->tx_q, next);
			m = data->m;

			if (m->m_pkthdr.len > (int)ZYD_MAX_TXBUFSZ) {
				DPRINTF(sc, ZYD_DEBUG_ANY, "data overflow, %u bytes\n",
				    m->m_pkthdr.len);
				m->m_pkthdr.len = ZYD_MAX_TXBUFSZ;
			}
			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_in(pc, 0, &data->desc, ZYD_TX_DESC_SIZE);
			usbd_m_copy_in(pc, ZYD_TX_DESC_SIZE, m, 0,
			    m->m_pkthdr.len);

			vap = data->ni->ni_vap;
			if (ieee80211_radiotap_active_vap(vap)) {
				struct zyd_tx_radiotap_header *tap = &sc->sc_txtap;

				tap->wt_flags = 0;
				tap->wt_rate = data->rate;

				ieee80211_radiotap_tx(vap, m);
			}

			usbd_xfer_set_frame_len(xfer, 0, ZYD_TX_DESC_SIZE + m->m_pkthdr.len);
			usbd_xfer_set_priv(xfer, data);
			usbd_transfer_submit(xfer);
		}
		zyd_start(sc);
		break;

	default:			/* Error */
		DPRINTF(sc, ZYD_DEBUG_ANY, "transfer error, %s\n",
		    usbd_errstr(error));

		counter_u64_add(sc->sc_ic.ic_oerrors, 1);
		data = usbd_xfer_get_priv(xfer);
		usbd_xfer_set_priv(xfer, NULL);
		if (data != NULL)
			zyd_tx_free(data, error);

		if (error != USB_ERR_CANCELLED) {
			if (error == USB_ERR_TIMEOUT)
				device_printf(sc->sc_dev, "device timeout\n");

			/*
			 * Try to clear stall first, also if other
			 * errors occur, hence clearing stall
			 * introduces a 50 ms delay:
			 */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static int
zyd_tx_start(struct zyd_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct zyd_tx_desc *desc;
	struct zyd_tx_data *data;
	struct ieee80211_frame *wh;
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct ieee80211_key *k;
	int rate, totlen, type, ismcast;
	static const uint8_t ratediv[] = ZYD_TX_RATEDIV;
	uint8_t phy;
	uint16_t pktlen;
	uint32_t bits;

	wh = mtod(m0, struct ieee80211_frame *);
	data = STAILQ_FIRST(&sc->tx_free);
	STAILQ_REMOVE_HEAD(&sc->tx_free, next);
	sc->tx_nfree--;

	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	if (type == IEEE80211_FC0_TYPE_MGT ||
	    type == IEEE80211_FC0_TYPE_CTL ||
	    (m0->m_flags & M_EAPOL) != 0) {
		rate = tp->mgmtrate;
	} else {
		/* for data frames */
		if (ismcast)
			rate = tp->mcastrate;
		else if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE)
			rate = tp->ucastrate;
		else {
			(void) ieee80211_ratectl_rate(ni, NULL, 0);
			rate = ni->ni_txrate;
		}
	}

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			return (ENOBUFS);
		}
		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	data->ni = ni;
	data->m = m0;
	data->rate = rate;

	/* fill Tx descriptor */
	desc = &data->desc;
	phy = zyd_plcp_signal(sc, rate);
	desc->phy = phy;
	if (ZYD_RATE_IS_OFDM(rate)) {
		desc->phy |= ZYD_TX_PHY_OFDM;
		if (IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan))
			desc->phy |= ZYD_TX_PHY_5GHZ;
	} else if (rate != 2 && (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
		desc->phy |= ZYD_TX_PHY_SHPREAMBLE;

	totlen = m0->m_pkthdr.len + IEEE80211_CRC_LEN;
	desc->len = htole16(totlen);

	desc->flags = ZYD_TX_FLAG_BACKOFF;
	if (!ismcast) {
		/* multicast frames are not sent at OFDM rates in 802.11b/g */
		if (totlen > vap->iv_rtsthreshold) {
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

	/* actual transmit length (XXX why +10?) */
	pktlen = ZYD_TX_DESC_SIZE + 10;
	if (sc->sc_macrev == ZYD_ZD1211)
		pktlen += totlen;
	desc->pktlen = htole16(pktlen);

	bits = (rate == 11) ? (totlen * 16) + 10 :
	    ((rate == 22) ? (totlen * 8) + 10 : (totlen * 8));
	desc->plcp_length = htole16(bits / ratediv[phy]);
	desc->plcp_service = 0;
	if (rate == 22 && (bits % 11) > 0 && (bits % 11) <= 3)
		desc->plcp_service |= ZYD_PLCP_LENGEXT;
	desc->nextlen = 0;

	if (ieee80211_radiotap_active_vap(vap)) {
		struct zyd_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;

		ieee80211_radiotap_tx(vap, m0);
	}

	DPRINTF(sc, ZYD_DEBUG_XMIT,
	    "%s: sending data frame len=%zu rate=%u\n",
	    device_get_nameunit(sc->sc_dev), (size_t)m0->m_pkthdr.len,
		rate);

	STAILQ_INSERT_TAIL(&sc->tx_q, data, next);
	usbd_transfer_start(sc->sc_xfer[ZYD_BULK_WR]);

	return (0);
}

static int
zyd_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct zyd_softc *sc = ic->ic_softc;
	int error;

	ZYD_LOCK(sc);
	if ((sc->sc_flags & ZYD_FLAG_RUNNING) == 0) {
		ZYD_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		ZYD_UNLOCK(sc);
		return (error);
	}
	zyd_start(sc);
	ZYD_UNLOCK(sc);

	return (0);
}

static void
zyd_start(struct zyd_softc *sc)
{
	struct ieee80211_node *ni;
	struct mbuf *m;

	ZYD_LOCK_ASSERT(sc, MA_OWNED);

	while (sc->tx_nfree > 0 && (m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		if (zyd_tx_start(sc, m, ni) != 0) {
			m_freem(m);
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			ieee80211_free_node(ni);
			break;
		}
	}
}

static int
zyd_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
	const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct zyd_softc *sc = ic->ic_softc;

	ZYD_LOCK(sc);
	/* prevent management frames from being sent if we're not ready */
	if (!(sc->sc_flags & ZYD_FLAG_RUNNING)) {
		ZYD_UNLOCK(sc);
		m_freem(m);
		return (ENETDOWN);
	}
	if (sc->tx_nfree == 0) {
		ZYD_UNLOCK(sc);
		m_freem(m);
		return (ENOBUFS);		/* XXX */
	}

	/*
	 * Legacy path; interpret frame contents to decide
	 * precisely how to send the frame.
	 * XXX raw path
	 */
	if (zyd_tx_start(sc, m, ni) != 0) {
		ZYD_UNLOCK(sc);
		m_freem(m);
		return (EIO);
	}
	ZYD_UNLOCK(sc);
	return (0);
}

static void
zyd_parent(struct ieee80211com *ic)
{
	struct zyd_softc *sc = ic->ic_softc;
	int startall = 0;

	ZYD_LOCK(sc);
	if (sc->sc_flags & ZYD_FLAG_DETACHED) {
		ZYD_UNLOCK(sc);
		return;
	}
	if (ic->ic_nrunning > 0) {
		if ((sc->sc_flags & ZYD_FLAG_RUNNING) == 0) {
			zyd_init_locked(sc);
			startall = 1;
		} else
			zyd_set_multi(sc);
	} else if (sc->sc_flags & ZYD_FLAG_RUNNING)
		zyd_stop(sc);
	ZYD_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

static void
zyd_init_locked(struct zyd_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct usb_config_descriptor *cd;
	int error;
	uint32_t val;

	ZYD_LOCK_ASSERT(sc, MA_OWNED);

	if (!(sc->sc_flags & ZYD_FLAG_INITONCE)) {
		error = zyd_loadfirmware(sc);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not load firmware (error=%d)\n", error);
			goto fail;
		}

		/* reset device */
		cd = usbd_get_config_descriptor(sc->sc_udev);
		error = usbd_req_set_config(sc->sc_udev, &sc->sc_mtx,
		    cd->bConfigurationValue);
		if (error)
			device_printf(sc->sc_dev, "reset failed, continuing\n");

		error = zyd_hw_init(sc);
		if (error) {
			device_printf(sc->sc_dev,
			    "hardware initialization failed\n");
			goto fail;
		}

		device_printf(sc->sc_dev,
		    "HMAC ZD1211%s, FW %02x.%02x, RF %s S%x, PA%x LED %x "
		    "BE%x NP%x Gain%x F%x\n",
		    (sc->sc_macrev == ZYD_ZD1211) ? "": "B",
		    sc->sc_fwrev >> 8, sc->sc_fwrev & 0xff,
		    zyd_rf_name(sc->sc_rfrev), sc->sc_al2230s, sc->sc_parev,
		    sc->sc_ledtype, sc->sc_bandedge6, sc->sc_newphy,
		    sc->sc_cckgain, sc->sc_fix_cr157);

		/* read regulatory domain (currently unused) */
		zyd_read32_m(sc, ZYD_EEPROM_SUBID, &val);
		sc->sc_regdomain = val >> 16;
		DPRINTF(sc, ZYD_DEBUG_INIT, "regulatory domain %x\n",
		    sc->sc_regdomain);

		/* we'll do software WEP decryption for now */
		DPRINTF(sc, ZYD_DEBUG_INIT, "%s: setting encryption type\n",
		    __func__);
		zyd_write32_m(sc, ZYD_MAC_ENCRYPTION_TYPE, ZYD_ENC_SNIFFER);

		sc->sc_flags |= ZYD_FLAG_INITONCE;
	}

	if (sc->sc_flags & ZYD_FLAG_RUNNING)
		zyd_stop(sc);

	DPRINTF(sc, ZYD_DEBUG_INIT, "setting MAC address to %6D\n",
	    vap ? vap->iv_myaddr : ic->ic_macaddr, ":");
	error = zyd_set_macaddr(sc, vap ? vap->iv_myaddr : ic->ic_macaddr);
	if (error != 0)
		return;

	/* set basic rates */
	if (ic->ic_curmode == IEEE80211_MODE_11B)
		zyd_write32_m(sc, ZYD_MAC_BAS_RATE, 0x0003);
	else if (ic->ic_curmode == IEEE80211_MODE_11A)
		zyd_write32_m(sc, ZYD_MAC_BAS_RATE, 0x1500);
	else	/* assumes 802.11b/g */
		zyd_write32_m(sc, ZYD_MAC_BAS_RATE, 0xff0f);

	/* promiscuous mode */
	zyd_write32_m(sc, ZYD_MAC_SNIFFER, 0);
	/* multicast setup */
	zyd_set_multi(sc);
	/* set RX filter  */
	error = zyd_set_rxfilter(sc);
	if (error != 0)
		goto fail;

	/* switch radio transmitter ON */
	error = zyd_switch_radio(sc, 1);
	if (error != 0)
		goto fail;
	/* set default BSS channel */
	zyd_set_chan(sc, ic->ic_curchan);

	/*
	 * Allocate Tx and Rx xfer queues.
	 */
	zyd_setup_tx_list(sc);

	/* enable interrupts */
	zyd_write32_m(sc, ZYD_CR_INTERRUPT, ZYD_HWINT_MASK);

	sc->sc_flags |= ZYD_FLAG_RUNNING;
	usbd_xfer_set_stall(sc->sc_xfer[ZYD_BULK_WR]);
	usbd_transfer_start(sc->sc_xfer[ZYD_BULK_RD]);
	usbd_transfer_start(sc->sc_xfer[ZYD_INTR_RD]);

	return;

fail:	zyd_stop(sc);
	return;
}

static void
zyd_stop(struct zyd_softc *sc)
{
	int error;

	ZYD_LOCK_ASSERT(sc, MA_OWNED);

	sc->sc_flags &= ~ZYD_FLAG_RUNNING;
	zyd_drain_mbufq(sc);

	/*
	 * Drain all the transfers, if not already drained:
	 */
	ZYD_UNLOCK(sc);
	usbd_transfer_drain(sc->sc_xfer[ZYD_BULK_WR]);
	usbd_transfer_drain(sc->sc_xfer[ZYD_BULK_RD]);
	ZYD_LOCK(sc);

	zyd_unsetup_tx_list(sc);

	/* Stop now if the device was never set up */
	if (!(sc->sc_flags & ZYD_FLAG_INITONCE))
		return;

	/* switch radio transmitter OFF */
	error = zyd_switch_radio(sc, 0);
	if (error != 0)
		goto fail;
	/* disable Rx */
	zyd_write32_m(sc, ZYD_MAC_RXFILTER, 0);
	/* disable interrupts */
	zyd_write32_m(sc, ZYD_CR_INTERRUPT, 0);

fail:
	return;
}

static int
zyd_loadfirmware(struct zyd_softc *sc)
{
	struct usb_device_request req;
	size_t size;
	u_char *fw;
	uint8_t stat;
	uint16_t addr;

	if (sc->sc_flags & ZYD_FLAG_FWLOADED)
		return (0);

	if (sc->sc_macrev == ZYD_ZD1211) {
		fw = (u_char *)zd1211_firmware;
		size = sizeof(zd1211_firmware);
	} else {
		fw = (u_char *)zd1211b_firmware;
		size = sizeof(zd1211b_firmware);
	}

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = ZYD_DOWNLOADREQ;
	USETW(req.wIndex, 0);

	addr = ZYD_FIRMWARE_START_ADDR;
	while (size > 0) {
		/*
		 * When the transfer size is 4096 bytes, it is not
		 * likely to be able to transfer it.
		 * The cause is port or machine or chip?
		 */
		const int mlen = min(size, 64);

		DPRINTF(sc, ZYD_DEBUG_FW,
		    "loading firmware block: len=%d, addr=0x%x\n", mlen, addr);

		USETW(req.wValue, addr);
		USETW(req.wLength, mlen);
		if (zyd_do_request(sc, &req, fw) != 0)
			return (EIO);

		addr += mlen / 2;
		fw   += mlen;
		size -= mlen;
	}

	/* check whether the upload succeeded */
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = ZYD_DOWNLOADSTS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, sizeof(stat));
	if (zyd_do_request(sc, &req, &stat) != 0)
		return (EIO);

	sc->sc_flags |= ZYD_FLAG_FWLOADED;

	return (stat & 0x80) ? (EIO) : (0);
}

static void
zyd_scan_start(struct ieee80211com *ic)
{
	struct zyd_softc *sc = ic->ic_softc;

	ZYD_LOCK(sc);
	/* want broadcast address while scanning */
	zyd_set_bssid(sc, ieee80211broadcastaddr);
	ZYD_UNLOCK(sc);
}

static void
zyd_scan_end(struct ieee80211com *ic)
{
	struct zyd_softc *sc = ic->ic_softc;

	ZYD_LOCK(sc);
	/* restore previous bssid */
	zyd_set_bssid(sc, sc->sc_bssid);
	ZYD_UNLOCK(sc);
}

static void
zyd_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	uint8_t bands[IEEE80211_MODE_BYTES];

	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	ieee80211_add_channels_default_2ghz(chans, maxchans, nchans, bands, 0);
}

static void
zyd_set_channel(struct ieee80211com *ic)
{
	struct zyd_softc *sc = ic->ic_softc;

	ZYD_LOCK(sc);
	zyd_set_chan(sc, ic->ic_curchan);
	ZYD_UNLOCK(sc);
}

static device_method_t zyd_methods[] = {
        /* Device interface */
        DEVMETHOD(device_probe, zyd_match),
        DEVMETHOD(device_attach, zyd_attach),
        DEVMETHOD(device_detach, zyd_detach),
	DEVMETHOD_END
};

static driver_t zyd_driver = {
	.name = "zyd",
	.methods = zyd_methods,
	.size = sizeof(struct zyd_softc)
};

static devclass_t zyd_devclass;

DRIVER_MODULE(zyd, uhub, zyd_driver, zyd_devclass, NULL, 0);
MODULE_DEPEND(zyd, usb, 1, 1, 1);
MODULE_DEPEND(zyd, wlan, 1, 1, 1);
MODULE_VERSION(zyd, 1);
USB_PNP_HOST_INFO(zyd_devs);

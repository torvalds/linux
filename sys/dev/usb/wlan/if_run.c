/*-
 * Copyright (c) 2008,2010 Damien Bergamini <damien.bergamini@free.fr>
 * ported to FreeBSD by Akinori Furukoshi <moonlightakkiy@yahoo.ca>
 * USB Consulting, Hans Petter Selasky <hselasky@freebsd.org>
 * Copyright (c) 2013-2014 Kevin Lo
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
 * Ralink Technology RT2700U/RT2800U/RT3000U/RT3900E chipset driver.
 * http://www.ralinktech.com/
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
#include <sys/linker.h>
#include <sys/firmware.h>
#include <sys/kdb.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
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
#include <net80211/ieee80211_ratectl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR	run_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_msctest.h>

#include <dev/usb/wlan/if_runreg.h>
#include <dev/usb/wlan/if_runvar.h>

#ifdef	USB_DEBUG
#define	RUN_DEBUG
#endif

#ifdef	RUN_DEBUG
int run_debug = 0;
static SYSCTL_NODE(_hw_usb, OID_AUTO, run, CTLFLAG_RW, 0, "USB run");
SYSCTL_INT(_hw_usb_run, OID_AUTO, debug, CTLFLAG_RWTUN, &run_debug, 0,
    "run debug level");

enum {
	RUN_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	RUN_DEBUG_XMIT_DESC	= 0x00000002,	/* xmit descriptors */
	RUN_DEBUG_RECV		= 0x00000004,	/* basic recv operation */
	RUN_DEBUG_RECV_DESC	= 0x00000008,	/* recv descriptors */
	RUN_DEBUG_STATE		= 0x00000010,	/* 802.11 state transitions */
	RUN_DEBUG_RATE		= 0x00000020,	/* rate adaptation */
	RUN_DEBUG_USB		= 0x00000040,	/* usb requests */
	RUN_DEBUG_FIRMWARE	= 0x00000080,	/* firmware(9) loading debug */
	RUN_DEBUG_BEACON	= 0x00000100,	/* beacon handling */
	RUN_DEBUG_INTR		= 0x00000200,	/* ISR */
	RUN_DEBUG_TEMP		= 0x00000400,	/* temperature calibration */
	RUN_DEBUG_ROM		= 0x00000800,	/* various ROM info */
	RUN_DEBUG_KEY		= 0x00001000,	/* crypto keys management */
	RUN_DEBUG_TXPWR		= 0x00002000,	/* dump Tx power values */
	RUN_DEBUG_RSSI		= 0x00004000,	/* dump RSSI lookups */
	RUN_DEBUG_RESET		= 0x00008000,	/* initialization progress */
	RUN_DEBUG_CALIB		= 0x00010000,	/* calibration progress */
	RUN_DEBUG_CMD		= 0x00020000,	/* command queue */
	RUN_DEBUG_ANY		= 0xffffffff
};

#define RUN_DPRINTF(_sc, _m, ...) do {			\
	if (run_debug & (_m))				\
		device_printf((_sc)->sc_dev, __VA_ARGS__);	\
} while(0)
#else
#define RUN_DPRINTF(_sc, _m, ...)	do { (void) _sc; } while (0)
#endif

#define	IEEE80211_HAS_ADDR4(wh)	IEEE80211_IS_DSTODS(wh)

/*
 * Because of LOR in run_key_delete(), use atomic instead.
 * '& RUN_CMDQ_MASQ' is to loop cmdq[].
 */
#define	RUN_CMDQ_GET(c)	(atomic_fetchadd_32((c), 1) & RUN_CMDQ_MASQ)

static const STRUCT_USB_HOST_ID run_devs[] = {
#define	RUN_DEV(v,p)	{ USB_VP(USB_VENDOR_##v, USB_PRODUCT_##v##_##p) }
#define	RUN_DEV_EJECT(v,p)	\
	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, RUN_EJECT) }
#define	RUN_EJECT	1
    RUN_DEV(ABOCOM,		RT2770),
    RUN_DEV(ABOCOM,		RT2870),
    RUN_DEV(ABOCOM,		RT3070),
    RUN_DEV(ABOCOM,		RT3071),
    RUN_DEV(ABOCOM,		RT3072),
    RUN_DEV(ABOCOM2,		RT2870_1),
    RUN_DEV(ACCTON,		RT2770),
    RUN_DEV(ACCTON,		RT2870_1),
    RUN_DEV(ACCTON,		RT2870_2),
    RUN_DEV(ACCTON,		RT2870_3),
    RUN_DEV(ACCTON,		RT2870_4),
    RUN_DEV(ACCTON,		RT2870_5),
    RUN_DEV(ACCTON,		RT3070),
    RUN_DEV(ACCTON,		RT3070_1),
    RUN_DEV(ACCTON,		RT3070_2),
    RUN_DEV(ACCTON,		RT3070_3),
    RUN_DEV(ACCTON,		RT3070_4),
    RUN_DEV(ACCTON,		RT3070_5),
    RUN_DEV(AIRTIES,		RT3070),
    RUN_DEV(ALLWIN,		RT2070),
    RUN_DEV(ALLWIN,		RT2770),
    RUN_DEV(ALLWIN,		RT2870),
    RUN_DEV(ALLWIN,		RT3070),
    RUN_DEV(ALLWIN,		RT3071),
    RUN_DEV(ALLWIN,		RT3072),
    RUN_DEV(ALLWIN,		RT3572),
    RUN_DEV(AMIGO,		RT2870_1),
    RUN_DEV(AMIGO,		RT2870_2),
    RUN_DEV(AMIT,		CGWLUSB2GNR),
    RUN_DEV(AMIT,		RT2870_1),
    RUN_DEV(AMIT2,		RT2870),
    RUN_DEV(ASUS,		RT2870_1),
    RUN_DEV(ASUS,		RT2870_2),
    RUN_DEV(ASUS,		RT2870_3),
    RUN_DEV(ASUS,		RT2870_4),
    RUN_DEV(ASUS,		RT2870_5),
    RUN_DEV(ASUS,		USBN13),
    RUN_DEV(ASUS,		RT3070_1),
    RUN_DEV(ASUS,		USBN66),
    RUN_DEV(ASUS,		USB_N53),
    RUN_DEV(ASUS2,		USBN11),
    RUN_DEV(AZUREWAVE,		RT2870_1),
    RUN_DEV(AZUREWAVE,		RT2870_2),
    RUN_DEV(AZUREWAVE,		RT3070_1),
    RUN_DEV(AZUREWAVE,		RT3070_2),
    RUN_DEV(AZUREWAVE,		RT3070_3),
    RUN_DEV(BELKIN,		F9L1103),
    RUN_DEV(BELKIN,		F5D8053V3),
    RUN_DEV(BELKIN,		F5D8055),
    RUN_DEV(BELKIN,		F5D8055V2),
    RUN_DEV(BELKIN,		F6D4050V1),
    RUN_DEV(BELKIN,		F6D4050V2),
    RUN_DEV(BELKIN,		RT2870_1),
    RUN_DEV(BELKIN,		RT2870_2),
    RUN_DEV(CISCOLINKSYS,	AE1000),
    RUN_DEV(CISCOLINKSYS2,	RT3070),
    RUN_DEV(CISCOLINKSYS3,	RT3070),
    RUN_DEV(CONCEPTRONIC2,	RT2870_1),
    RUN_DEV(CONCEPTRONIC2,	RT2870_2),
    RUN_DEV(CONCEPTRONIC2,	RT2870_3),
    RUN_DEV(CONCEPTRONIC2,	RT2870_4),
    RUN_DEV(CONCEPTRONIC2,	RT2870_5),
    RUN_DEV(CONCEPTRONIC2,	RT2870_6),
    RUN_DEV(CONCEPTRONIC2,	RT2870_7),
    RUN_DEV(CONCEPTRONIC2,	RT2870_8),
    RUN_DEV(CONCEPTRONIC2,	RT3070_1),
    RUN_DEV(CONCEPTRONIC2,	RT3070_2),
    RUN_DEV(CONCEPTRONIC2,	VIGORN61),
    RUN_DEV(COREGA,		CGWLUSB300GNM),
    RUN_DEV(COREGA,		RT2870_1),
    RUN_DEV(COREGA,		RT2870_2),
    RUN_DEV(COREGA,		RT2870_3),
    RUN_DEV(COREGA,		RT3070),
    RUN_DEV(CYBERTAN,		RT2870),
    RUN_DEV(DLINK,		RT2870),
    RUN_DEV(DLINK,		RT3072),
    RUN_DEV(DLINK,		DWA125A3),
    RUN_DEV(DLINK,		DWA127),
    RUN_DEV(DLINK,		DWA140B3),
    RUN_DEV(DLINK,		DWA160B2),
    RUN_DEV(DLINK,		DWA140D1),
    RUN_DEV(DLINK,		DWA162),
    RUN_DEV(DLINK2,		DWA130),
    RUN_DEV(DLINK2,		RT2870_1),
    RUN_DEV(DLINK2,		RT2870_2),
    RUN_DEV(DLINK2,		RT3070_1),
    RUN_DEV(DLINK2,		RT3070_2),
    RUN_DEV(DLINK2,		RT3070_3),
    RUN_DEV(DLINK2,		RT3070_4),
    RUN_DEV(DLINK2,		RT3070_5),
    RUN_DEV(DLINK2,		RT3072),
    RUN_DEV(DLINK2,		RT3072_1),
    RUN_DEV(EDIMAX,		EW7717),
    RUN_DEV(EDIMAX,		EW7718),
    RUN_DEV(EDIMAX,		EW7733UND),
    RUN_DEV(EDIMAX,		RT2870_1),
    RUN_DEV(ENCORE,		RT3070_1),
    RUN_DEV(ENCORE,		RT3070_2),
    RUN_DEV(ENCORE,		RT3070_3),
    RUN_DEV(GIGABYTE,		GNWB31N),
    RUN_DEV(GIGABYTE,		GNWB32L),
    RUN_DEV(GIGABYTE,		RT2870_1),
    RUN_DEV(GIGASET,		RT3070_1),
    RUN_DEV(GIGASET,		RT3070_2),
    RUN_DEV(GUILLEMOT,		HWNU300),
    RUN_DEV(HAWKING,		HWUN2),
    RUN_DEV(HAWKING,		RT2870_1),
    RUN_DEV(HAWKING,		RT2870_2),
    RUN_DEV(HAWKING,		RT3070),
    RUN_DEV(IODATA,		RT3072_1),
    RUN_DEV(IODATA,		RT3072_2),
    RUN_DEV(IODATA,		RT3072_3),
    RUN_DEV(IODATA,		RT3072_4),
    RUN_DEV(LINKSYS4,		RT3070),
    RUN_DEV(LINKSYS4,		WUSB100),
    RUN_DEV(LINKSYS4,		WUSB54GCV3),
    RUN_DEV(LINKSYS4,		WUSB600N),
    RUN_DEV(LINKSYS4,		WUSB600NV2),
    RUN_DEV(LOGITEC,		RT2870_1),
    RUN_DEV(LOGITEC,		RT2870_2),
    RUN_DEV(LOGITEC,		RT2870_3),
    RUN_DEV(LOGITEC,		LANW300NU2),
    RUN_DEV(LOGITEC,		LANW150NU2),
    RUN_DEV(LOGITEC,		LANW300NU2S),
    RUN_DEV(MELCO,		WLIUCG300HP),
    RUN_DEV(MELCO,		RT2870_2),
    RUN_DEV(MELCO,		WLIUCAG300N),
    RUN_DEV(MELCO,		WLIUCG300N),
    RUN_DEV(MELCO,		WLIUCG301N),
    RUN_DEV(MELCO,		WLIUCGN),
    RUN_DEV(MELCO,		WLIUCGNM),
    RUN_DEV(MELCO,		WLIUCG300HPV1),
    RUN_DEV(MELCO,		WLIUCGNM2),
    RUN_DEV(MOTOROLA4,		RT2770),
    RUN_DEV(MOTOROLA4,		RT3070),
    RUN_DEV(MSI,		RT3070_1),
    RUN_DEV(MSI,		RT3070_2),
    RUN_DEV(MSI,		RT3070_3),
    RUN_DEV(MSI,		RT3070_4),
    RUN_DEV(MSI,		RT3070_5),
    RUN_DEV(MSI,		RT3070_6),
    RUN_DEV(MSI,		RT3070_7),
    RUN_DEV(MSI,		RT3070_8),
    RUN_DEV(MSI,		RT3070_9),
    RUN_DEV(MSI,		RT3070_10),
    RUN_DEV(MSI,		RT3070_11),
    RUN_DEV(NETGEAR,		WNDA4100),
    RUN_DEV(OVISLINK,		RT3072),
    RUN_DEV(PARA,		RT3070),
    RUN_DEV(PEGATRON,		RT2870),
    RUN_DEV(PEGATRON,		RT3070),
    RUN_DEV(PEGATRON,		RT3070_2),
    RUN_DEV(PEGATRON,		RT3070_3),
    RUN_DEV(PHILIPS,		RT2870),
    RUN_DEV(PLANEX2,		GWUS300MINIS),
    RUN_DEV(PLANEX2,		GWUSMICRON),
    RUN_DEV(PLANEX2,		RT2870),
    RUN_DEV(PLANEX2,		RT3070),
    RUN_DEV(QCOM,		RT2870),
    RUN_DEV(QUANTA,		RT3070),
    RUN_DEV(RALINK,		RT2070),
    RUN_DEV(RALINK,		RT2770),
    RUN_DEV(RALINK,		RT2870),
    RUN_DEV(RALINK,		RT3070),
    RUN_DEV(RALINK,		RT3071),
    RUN_DEV(RALINK,		RT3072),
    RUN_DEV(RALINK,		RT3370),
    RUN_DEV(RALINK,		RT3572),
    RUN_DEV(RALINK,		RT3573),
    RUN_DEV(RALINK,		RT5370),
    RUN_DEV(RALINK,		RT5372),
    RUN_DEV(RALINK,		RT5572),
    RUN_DEV(RALINK,		RT8070),
    RUN_DEV(SAMSUNG,		WIS09ABGN),
    RUN_DEV(SAMSUNG2,		RT2870_1),
    RUN_DEV(SENAO,		RT2870_1),
    RUN_DEV(SENAO,		RT2870_2),
    RUN_DEV(SENAO,		RT2870_3),
    RUN_DEV(SENAO,		RT2870_4),
    RUN_DEV(SENAO,		RT3070),
    RUN_DEV(SENAO,		RT3071),
    RUN_DEV(SENAO,		RT3072_1),
    RUN_DEV(SENAO,		RT3072_2),
    RUN_DEV(SENAO,		RT3072_3),
    RUN_DEV(SENAO,		RT3072_4),
    RUN_DEV(SENAO,		RT3072_5),
    RUN_DEV(SITECOMEU,		RT2770),
    RUN_DEV(SITECOMEU,		RT2870_1),
    RUN_DEV(SITECOMEU,		RT2870_2),
    RUN_DEV(SITECOMEU,		RT2870_3),
    RUN_DEV(SITECOMEU,		RT2870_4),
    RUN_DEV(SITECOMEU,		RT3070),
    RUN_DEV(SITECOMEU,		RT3070_2),
    RUN_DEV(SITECOMEU,		RT3070_3),
    RUN_DEV(SITECOMEU,		RT3070_4),
    RUN_DEV(SITECOMEU,		RT3071),
    RUN_DEV(SITECOMEU,		RT3072_1),
    RUN_DEV(SITECOMEU,		RT3072_2),
    RUN_DEV(SITECOMEU,		RT3072_3),
    RUN_DEV(SITECOMEU,		RT3072_4),
    RUN_DEV(SITECOMEU,		RT3072_5),
    RUN_DEV(SITECOMEU,		RT3072_6),
    RUN_DEV(SITECOMEU,		WL608),
    RUN_DEV(SPARKLAN,		RT2870_1),
    RUN_DEV(SPARKLAN,		RT3070),
    RUN_DEV(SWEEX2,		LW153),
    RUN_DEV(SWEEX2,		LW303),
    RUN_DEV(SWEEX2,		LW313),
    RUN_DEV(TOSHIBA,		RT3070),
    RUN_DEV(UMEDIA,		RT2870_1),
    RUN_DEV(ZCOM,		RT2870_1),
    RUN_DEV(ZCOM,		RT2870_2),
    RUN_DEV(ZINWELL,		RT2870_1),
    RUN_DEV(ZINWELL,		RT2870_2),
    RUN_DEV(ZINWELL,		RT3070),
    RUN_DEV(ZINWELL,		RT3072_1),
    RUN_DEV(ZINWELL,		RT3072_2),
    RUN_DEV(ZYXEL,		RT2870_1),
    RUN_DEV(ZYXEL,		RT2870_2),
    RUN_DEV(ZYXEL,		RT3070),
    RUN_DEV_EJECT(ZYXEL,	NWD2705),
    RUN_DEV_EJECT(RALINK,	RT_STOR),
#undef RUN_DEV_EJECT
#undef RUN_DEV
};

static device_probe_t	run_match;
static device_attach_t	run_attach;
static device_detach_t	run_detach;

static usb_callback_t	run_bulk_rx_callback;
static usb_callback_t	run_bulk_tx_callback0;
static usb_callback_t	run_bulk_tx_callback1;
static usb_callback_t	run_bulk_tx_callback2;
static usb_callback_t	run_bulk_tx_callback3;
static usb_callback_t	run_bulk_tx_callback4;
static usb_callback_t	run_bulk_tx_callback5;

static void	run_autoinst(void *, struct usb_device *,
		    struct usb_attach_arg *);
static int	run_driver_loaded(struct module *, int, void *);
static void	run_bulk_tx_callbackN(struct usb_xfer *xfer,
		    usb_error_t error, u_int index);
static struct ieee80211vap *run_vap_create(struct ieee80211com *,
		    const char [IFNAMSIZ], int, enum ieee80211_opmode, int,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    const uint8_t [IEEE80211_ADDR_LEN]);
static void	run_vap_delete(struct ieee80211vap *);
static void	run_cmdq_cb(void *, int);
static void	run_setup_tx_list(struct run_softc *,
		    struct run_endpoint_queue *);
static void	run_unsetup_tx_list(struct run_softc *,
		    struct run_endpoint_queue *);
static int	run_load_microcode(struct run_softc *);
static int	run_reset(struct run_softc *);
static usb_error_t run_do_request(struct run_softc *,
		    struct usb_device_request *, void *);
static int	run_read(struct run_softc *, uint16_t, uint32_t *);
static int	run_read_region_1(struct run_softc *, uint16_t, uint8_t *, int);
static int	run_write_2(struct run_softc *, uint16_t, uint16_t);
static int	run_write(struct run_softc *, uint16_t, uint32_t);
static int	run_write_region_1(struct run_softc *, uint16_t,
		    const uint8_t *, int);
static int	run_set_region_4(struct run_softc *, uint16_t, uint32_t, int);
static int	run_efuse_read(struct run_softc *, uint16_t, uint16_t *, int);
static int	run_efuse_read_2(struct run_softc *, uint16_t, uint16_t *);
static int	run_eeprom_read_2(struct run_softc *, uint16_t, uint16_t *);
static int	run_rt2870_rf_write(struct run_softc *, uint32_t);
static int	run_rt3070_rf_read(struct run_softc *, uint8_t, uint8_t *);
static int	run_rt3070_rf_write(struct run_softc *, uint8_t, uint8_t);
static int	run_bbp_read(struct run_softc *, uint8_t, uint8_t *);
static int	run_bbp_write(struct run_softc *, uint8_t, uint8_t);
static int	run_mcu_cmd(struct run_softc *, uint8_t, uint16_t);
static const char *run_get_rf(uint16_t);
static void	run_rt3593_get_txpower(struct run_softc *);
static void	run_get_txpower(struct run_softc *);
static int	run_read_eeprom(struct run_softc *);
static struct ieee80211_node *run_node_alloc(struct ieee80211vap *,
			    const uint8_t mac[IEEE80211_ADDR_LEN]);
static int	run_media_change(struct ifnet *);
static int	run_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static int	run_wme_update(struct ieee80211com *);
static void	run_key_set_cb(void *);
static int	run_key_set(struct ieee80211vap *, struct ieee80211_key *);
static void	run_key_delete_cb(void *);
static int	run_key_delete(struct ieee80211vap *, struct ieee80211_key *);
static void	run_ratectl_to(void *);
static void	run_ratectl_cb(void *, int);
static void	run_drain_fifo(void *);
static void	run_iter_func(void *, struct ieee80211_node *);
static void	run_newassoc_cb(void *);
static void	run_newassoc(struct ieee80211_node *, int);
static void	run_recv_mgmt(struct ieee80211_node *, struct mbuf *, int,
		    const struct ieee80211_rx_stats *, int, int);
static void	run_rx_frame(struct run_softc *, struct mbuf *, uint32_t);
static void	run_tx_free(struct run_endpoint_queue *pq,
		    struct run_tx_data *, int);
static void	run_set_tx_desc(struct run_softc *, struct run_tx_data *);
static int	run_tx(struct run_softc *, struct mbuf *,
		    struct ieee80211_node *);
static int	run_tx_mgt(struct run_softc *, struct mbuf *,
		    struct ieee80211_node *);
static int	run_sendprot(struct run_softc *, const struct mbuf *,
		    struct ieee80211_node *, int, int);
static int	run_tx_param(struct run_softc *, struct mbuf *,
		    struct ieee80211_node *,
		    const struct ieee80211_bpf_params *);
static int	run_raw_xmit(struct ieee80211_node *, struct mbuf *,
		    const struct ieee80211_bpf_params *);
static int	run_transmit(struct ieee80211com *, struct mbuf *);
static void	run_start(struct run_softc *);
static void	run_parent(struct ieee80211com *);
static void	run_iq_calib(struct run_softc *, u_int);
static void	run_set_agc(struct run_softc *, uint8_t);
static void	run_select_chan_group(struct run_softc *, int);
static void	run_set_rx_antenna(struct run_softc *, int);
static void	run_rt2870_set_chan(struct run_softc *, u_int);
static void	run_rt3070_set_chan(struct run_softc *, u_int);
static void	run_rt3572_set_chan(struct run_softc *, u_int);
static void	run_rt3593_set_chan(struct run_softc *, u_int);
static void	run_rt5390_set_chan(struct run_softc *, u_int);
static void	run_rt5592_set_chan(struct run_softc *, u_int);
static int	run_set_chan(struct run_softc *, struct ieee80211_channel *);
static void	run_set_channel(struct ieee80211com *);
static void	run_getradiocaps(struct ieee80211com *, int, int *,
		    struct ieee80211_channel[]);
static void	run_scan_start(struct ieee80211com *);
static void	run_scan_end(struct ieee80211com *);
static void	run_update_beacon(struct ieee80211vap *, int);
static void	run_update_beacon_cb(void *);
static void	run_updateprot(struct ieee80211com *);
static void	run_updateprot_cb(void *);
static void	run_usb_timeout_cb(void *);
static void	run_reset_livelock(struct run_softc *);
static void	run_enable_tsf_sync(struct run_softc *);
static void	run_enable_tsf(struct run_softc *);
static void	run_disable_tsf(struct run_softc *);
static void	run_get_tsf(struct run_softc *, uint64_t *);
static void	run_enable_mrr(struct run_softc *);
static void	run_set_txpreamble(struct run_softc *);
static void	run_set_basicrates(struct run_softc *);
static void	run_set_leds(struct run_softc *, uint16_t);
static void	run_set_bssid(struct run_softc *, const uint8_t *);
static void	run_set_macaddr(struct run_softc *, const uint8_t *);
static void	run_updateslot(struct ieee80211com *);
static void	run_updateslot_cb(void *);
static void	run_update_mcast(struct ieee80211com *);
static int8_t	run_rssi2dbm(struct run_softc *, uint8_t, uint8_t);
static void	run_update_promisc_locked(struct run_softc *);
static void	run_update_promisc(struct ieee80211com *);
static void	run_rt5390_bbp_init(struct run_softc *);
static int	run_bbp_init(struct run_softc *);
static int	run_rt3070_rf_init(struct run_softc *);
static void	run_rt3593_rf_init(struct run_softc *);
static void	run_rt5390_rf_init(struct run_softc *);
static int	run_rt3070_filter_calib(struct run_softc *, uint8_t, uint8_t,
		    uint8_t *);
static void	run_rt3070_rf_setup(struct run_softc *);
static void	run_rt3593_rf_setup(struct run_softc *);
static void	run_rt5390_rf_setup(struct run_softc *);
static int	run_txrx_enable(struct run_softc *);
static void	run_adjust_freq_offset(struct run_softc *);
static void	run_init_locked(struct run_softc *);
static void	run_stop(void *);
static void	run_delay(struct run_softc *, u_int);

static eventhandler_tag run_etag;

static const struct rt2860_rate {
	uint8_t		rate;
	uint8_t		mcs;
	enum		ieee80211_phytype phy;
	uint8_t		ctl_ridx;
	uint16_t	sp_ack_dur;
	uint16_t	lp_ack_dur;
} rt2860_rates[] = {
	{   2, 0, IEEE80211_T_DS,   0, 314, 314 },
	{   4, 1, IEEE80211_T_DS,   1, 258, 162 },
	{  11, 2, IEEE80211_T_DS,   2, 223, 127 },
	{  22, 3, IEEE80211_T_DS,   3, 213, 117 },
	{  12, 0, IEEE80211_T_OFDM, 4,  60,  60 },
	{  18, 1, IEEE80211_T_OFDM, 4,  52,  52 },
	{  24, 2, IEEE80211_T_OFDM, 6,  48,  48 },
	{  36, 3, IEEE80211_T_OFDM, 6,  44,  44 },
	{  48, 4, IEEE80211_T_OFDM, 8,  44,  44 },
	{  72, 5, IEEE80211_T_OFDM, 8,  40,  40 },
	{  96, 6, IEEE80211_T_OFDM, 8,  40,  40 },
	{ 108, 7, IEEE80211_T_OFDM, 8,  40,  40 }
};

static const struct {
	uint16_t	reg;
	uint32_t	val;
} rt2870_def_mac[] = {
	RT2870_DEF_MAC
};

static const struct {
	uint8_t	reg;
	uint8_t	val;
} rt2860_def_bbp[] = {
	RT2860_DEF_BBP
},rt5390_def_bbp[] = {
	RT5390_DEF_BBP
},rt5592_def_bbp[] = {
	RT5592_DEF_BBP
};

/* 
 * Default values for BBP register R196 for RT5592.
 */
static const uint8_t rt5592_bbp_r196[] = {
	0xe0, 0x1f, 0x38, 0x32, 0x08, 0x28, 0x19, 0x0a, 0xff, 0x00,
	0x16, 0x10, 0x10, 0x0b, 0x36, 0x2c, 0x26, 0x24, 0x42, 0x36,
	0x30, 0x2d, 0x4c, 0x46, 0x3d, 0x40, 0x3e, 0x42, 0x3d, 0x40,
	0x3c, 0x34, 0x2c, 0x2f, 0x3c, 0x35, 0x2e, 0x2a, 0x49, 0x41,
	0x36, 0x31, 0x30, 0x30, 0x0e, 0x0d, 0x28, 0x21, 0x1c, 0x16,
	0x50, 0x4a, 0x43, 0x40, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x7d, 0x14, 0x32, 0x2c, 0x36, 0x4c, 0x43, 0x2c,
	0x2e, 0x36, 0x30, 0x6e
};

static const struct rfprog {
	uint8_t		chan;
	uint32_t	r1, r2, r3, r4;
} rt2860_rf2850[] = {
	RT2860_RF2850
};

struct {
	uint8_t	n, r, k;
} rt3070_freqs[] = {
	RT3070_RF3052
};

static const struct rt5592_freqs {
	uint16_t	n;
	uint8_t		k, m, r;
} rt5592_freqs_20mhz[] = {
	RT5592_RF5592_20MHZ
},rt5592_freqs_40mhz[] = {
	RT5592_RF5592_40MHZ
};

static const struct {
	uint8_t	reg;
	uint8_t	val;
} rt3070_def_rf[] = {
	RT3070_DEF_RF
},rt3572_def_rf[] = {
	RT3572_DEF_RF
},rt3593_def_rf[] = {
	RT3593_DEF_RF
},rt5390_def_rf[] = {
	RT5390_DEF_RF
},rt5392_def_rf[] = {
	RT5392_DEF_RF
},rt5592_def_rf[] = {
	RT5592_DEF_RF
},rt5592_2ghz_def_rf[] = {
	RT5592_2GHZ_DEF_RF
},rt5592_5ghz_def_rf[] = {
	RT5592_5GHZ_DEF_RF
};

static const struct {
	u_int	firstchan;
	u_int	lastchan;
	uint8_t	reg;
	uint8_t	val;
} rt5592_chan_5ghz[] = {
	RT5592_CHAN_5GHZ
};

static const struct usb_config run_config[RUN_N_XFER] = {
    [RUN_BULK_TX_BE] = {
	.type = UE_BULK,
	.endpoint = UE_ADDR_ANY,
	.ep_index = 0,
	.direction = UE_DIR_OUT,
	.bufsize = RUN_MAX_TXSZ,
	.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
	.callback = run_bulk_tx_callback0,
	.timeout = 5000,	/* ms */
    },
    [RUN_BULK_TX_BK] = {
	.type = UE_BULK,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_OUT,
	.ep_index = 1,
	.bufsize = RUN_MAX_TXSZ,
	.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
	.callback = run_bulk_tx_callback1,
	.timeout = 5000,	/* ms */
    },
    [RUN_BULK_TX_VI] = {
	.type = UE_BULK,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_OUT,
	.ep_index = 2,
	.bufsize = RUN_MAX_TXSZ,
	.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
	.callback = run_bulk_tx_callback2,
	.timeout = 5000,	/* ms */
    },
    [RUN_BULK_TX_VO] = {
	.type = UE_BULK,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_OUT,
	.ep_index = 3,
	.bufsize = RUN_MAX_TXSZ,
	.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
	.callback = run_bulk_tx_callback3,
	.timeout = 5000,	/* ms */
    },
    [RUN_BULK_TX_HCCA] = {
	.type = UE_BULK,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_OUT,
	.ep_index = 4,
	.bufsize = RUN_MAX_TXSZ,
	.flags = {.pipe_bof = 1,.force_short_xfer = 1,.no_pipe_ok = 1,},
	.callback = run_bulk_tx_callback4,
	.timeout = 5000,	/* ms */
    },
    [RUN_BULK_TX_PRIO] = {
	.type = UE_BULK,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_OUT,
	.ep_index = 5,
	.bufsize = RUN_MAX_TXSZ,
	.flags = {.pipe_bof = 1,.force_short_xfer = 1,.no_pipe_ok = 1,},
	.callback = run_bulk_tx_callback5,
	.timeout = 5000,	/* ms */
    },
    [RUN_BULK_RX] = {
	.type = UE_BULK,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = RUN_MAX_RXSZ,
	.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
	.callback = run_bulk_rx_callback,
    }
};

static void
run_autoinst(void *arg, struct usb_device *udev,
    struct usb_attach_arg *uaa)
{
	struct usb_interface *iface;
	struct usb_interface_descriptor *id;

	if (uaa->dev_state != UAA_DEV_READY)
		return;

	iface = usbd_get_iface(udev, 0);
	if (iface == NULL)
		return;
	id = iface->idesc;
	if (id == NULL || id->bInterfaceClass != UICLASS_MASS)
		return;
	if (usbd_lookup_id_by_uaa(run_devs, sizeof(run_devs), uaa))
		return;

	if (usb_msc_eject(udev, 0, MSC_EJECT_STOPUNIT) == 0)
		uaa->dev_state = UAA_DEV_EJECTING;
}

static int
run_driver_loaded(struct module *mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		run_etag = EVENTHANDLER_REGISTER(usb_dev_configured,
		    run_autoinst, NULL, EVENTHANDLER_PRI_ANY);
		break;
	case MOD_UNLOAD:
		EVENTHANDLER_DEREGISTER(usb_dev_configured, run_etag);
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static int
run_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != 0)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != RT2860_IFACE_INDEX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(run_devs, sizeof(run_devs), uaa));
}

static int
run_attach(device_t self)
{
	struct run_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t ver;
	uint8_t iface_index;
	int ntries, error;

	device_set_usb_desc(self);
	sc->sc_udev = uaa->device;
	sc->sc_dev = self;
	if (USB_GET_DRIVER_INFO(uaa) != RUN_EJECT)
		sc->sc_flags |= RUN_FLAG_FWLOAD_NEEDED;

	mtx_init(&sc->sc_mtx, device_get_nameunit(sc->sc_dev),
	    MTX_NETWORK_LOCK, MTX_DEF);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	iface_index = RT2860_IFACE_INDEX;

	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, run_config, RUN_N_XFER, sc, &sc->sc_mtx);
	if (error) {
		device_printf(self, "could not allocate USB transfers, "
		    "err=%s\n", usbd_errstr(error));
		goto detach;
	}

	RUN_LOCK(sc);

	/* wait for the chip to settle */
	for (ntries = 0; ntries < 100; ntries++) {
		if (run_read(sc, RT2860_ASIC_VER_ID, &ver) != 0) {
			RUN_UNLOCK(sc);
			goto detach;
		}
		if (ver != 0 && ver != 0xffffffff)
			break;
		run_delay(sc, 10);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev,
		    "timeout waiting for NIC to initialize\n");
		RUN_UNLOCK(sc);
		goto detach;
	}
	sc->mac_ver = ver >> 16;
	sc->mac_rev = ver & 0xffff;

	/* retrieve RF rev. no and various other things from EEPROM */
	run_read_eeprom(sc);

	device_printf(sc->sc_dev,
	    "MAC/BBP RT%04X (rev 0x%04X), RF %s (MIMO %dT%dR), address %s\n",
	    sc->mac_ver, sc->mac_rev, run_get_rf(sc->rf_rev),
	    sc->ntxchains, sc->nrxchains, ether_sprintf(ic->ic_macaddr));

	RUN_UNLOCK(sc);

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(self);
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_STA |		/* station mode supported */
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_IBSS |
	    IEEE80211_C_HOSTAP |
	    IEEE80211_C_WDS |		/* 4-address traffic works */
	    IEEE80211_C_MBSS |
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WME |		/* WME */
	    IEEE80211_C_WPA;		/* WPA1|WPA2(RSN) */

	ic->ic_cryptocaps =
	    IEEE80211_CRYPTO_WEP |
	    IEEE80211_CRYPTO_AES_CCM |
	    IEEE80211_CRYPTO_TKIPMIC |
	    IEEE80211_CRYPTO_TKIP;

	ic->ic_flags |= IEEE80211_F_DATAPAD;
	ic->ic_flags_ext |= IEEE80211_FEXT_SWBMISS;

	run_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	ieee80211_ifattach(ic);

	ic->ic_scan_start = run_scan_start;
	ic->ic_scan_end = run_scan_end;
	ic->ic_set_channel = run_set_channel;
	ic->ic_getradiocaps = run_getradiocaps;
	ic->ic_node_alloc = run_node_alloc;
	ic->ic_newassoc = run_newassoc;
	ic->ic_updateslot = run_updateslot;
	ic->ic_update_mcast = run_update_mcast;
	ic->ic_wme.wme_update = run_wme_update;
	ic->ic_raw_xmit = run_raw_xmit;
	ic->ic_update_promisc = run_update_promisc;
	ic->ic_vap_create = run_vap_create;
	ic->ic_vap_delete = run_vap_delete;
	ic->ic_transmit = run_transmit;
	ic->ic_parent = run_parent;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_txtap.wt_ihdr, sizeof(sc->sc_txtap),
		RUN_TX_RADIOTAP_PRESENT,
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
		RUN_RX_RADIOTAP_PRESENT);

	TASK_INIT(&sc->cmdq_task, 0, run_cmdq_cb, sc);
	TASK_INIT(&sc->ratectl_task, 0, run_ratectl_cb, sc);
	usb_callout_init_mtx(&sc->ratectl_ch, &sc->sc_mtx, 0);

	if (bootverbose)
		ieee80211_announce(ic);

	return (0);

detach:
	run_detach(self);
	return (ENXIO);
}

static void
run_drain_mbufq(struct run_softc *sc)
{
	struct mbuf *m;
	struct ieee80211_node *ni;

	RUN_LOCK_ASSERT(sc, MA_OWNED);
	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		m->m_pkthdr.rcvif = NULL;
		ieee80211_free_node(ni);
		m_freem(m);
	}
}

static int
run_detach(device_t self)
{
	struct run_softc *sc = device_get_softc(self);
	struct ieee80211com *ic = &sc->sc_ic;
	int i;

	RUN_LOCK(sc);
	sc->sc_detached = 1;
	RUN_UNLOCK(sc);

	/* stop all USB transfers */
	usbd_transfer_unsetup(sc->sc_xfer, RUN_N_XFER);

	RUN_LOCK(sc);
	sc->ratectl_run = RUN_RATECTL_OFF;
	sc->cmdq_run = sc->cmdq_key_set = RUN_CMDQ_ABORT;

	/* free TX list, if any */
	for (i = 0; i != RUN_EP_QUEUES; i++)
		run_unsetup_tx_list(sc, &sc->sc_epq[i]);

	/* Free TX queue */
	run_drain_mbufq(sc);
	RUN_UNLOCK(sc);

	if (sc->sc_ic.ic_softc == sc) {
		/* drain tasks */
		usb_callout_drain(&sc->ratectl_ch);
		ieee80211_draintask(ic, &sc->cmdq_task);
		ieee80211_draintask(ic, &sc->ratectl_task);
		ieee80211_ifdetach(ic);
	}

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static struct ieee80211vap *
run_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct run_softc *sc = ic->ic_softc;
	struct run_vap *rvp;
	struct ieee80211vap *vap;
	int i;

	if (sc->rvp_cnt >= RUN_VAP_MAX) {
		device_printf(sc->sc_dev, "number of VAPs maxed out\n");
		return (NULL);
	}

	switch (opmode) {
	case IEEE80211_M_STA:
		/* enable s/w bmiss handling for sta mode */
		flags |= IEEE80211_CLONE_NOBEACONS; 
		/* fall though */
	case IEEE80211_M_IBSS:
	case IEEE80211_M_MONITOR:
	case IEEE80211_M_HOSTAP:
	case IEEE80211_M_MBSS:
		/* other than WDS vaps, only one at a time */
		if (!TAILQ_EMPTY(&ic->ic_vaps))
			return (NULL);
		break;
	case IEEE80211_M_WDS:
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next){
			if(vap->iv_opmode != IEEE80211_M_HOSTAP)
				continue;
			/* WDS vap's always share the local mac address. */
			flags &= ~IEEE80211_CLONE_BSSID;
			break;
		}
		if (vap == NULL) {
			device_printf(sc->sc_dev,
			    "wds only supported in ap mode\n");
			return (NULL);
		}
		break;
	default:
		device_printf(sc->sc_dev, "unknown opmode %d\n", opmode);
		return (NULL);
	}

	rvp = malloc(sizeof(struct run_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &rvp->vap;

	if (ieee80211_vap_setup(ic, vap, name, unit, opmode, flags,
	    bssid) != 0) {
		/* out of memory */
		free(rvp, M_80211_VAP);
		return (NULL);
	}

	vap->iv_update_beacon = run_update_beacon;
	vap->iv_max_aid = RT2870_WCID_MAX;
	/*
	 * To delete the right key from h/w, we need wcid.
	 * Luckily, there is unused space in ieee80211_key{}, wk_pad,
	 * and matching wcid will be written into there. So, cast
	 * some spells to remove 'const' from ieee80211_key{}
	 */
	vap->iv_key_delete = (void *)run_key_delete;
	vap->iv_key_set = (void *)run_key_set;

	/* override state transition machine */
	rvp->newstate = vap->iv_newstate;
	vap->iv_newstate = run_newstate;
	if (opmode == IEEE80211_M_IBSS) {
		rvp->recv_mgmt = vap->iv_recv_mgmt;
		vap->iv_recv_mgmt = run_recv_mgmt;
	}

	ieee80211_ratectl_init(vap);
	ieee80211_ratectl_setinterval(vap, 1000 /* 1 sec */);

	/* complete setup */
	ieee80211_vap_attach(vap, run_media_change, ieee80211_media_status,
	    mac);

	/* make sure id is always unique */
	for (i = 0; i < RUN_VAP_MAX; i++) {
		if((sc->rvp_bmap & 1 << i) == 0){
			sc->rvp_bmap |= 1 << i;
			rvp->rvp_id = i;
			break;
		}
	}
	if (sc->rvp_cnt++ == 0)
		ic->ic_opmode = opmode;

	if (opmode == IEEE80211_M_HOSTAP)
		sc->cmdq_run = RUN_CMDQ_GO;

	RUN_DPRINTF(sc, RUN_DEBUG_STATE, "rvp_id=%d bmap=%x rvp_cnt=%d\n",
	    rvp->rvp_id, sc->rvp_bmap, sc->rvp_cnt);

	return (vap);
}

static void
run_vap_delete(struct ieee80211vap *vap)
{
	struct run_vap *rvp = RUN_VAP(vap);
	struct ieee80211com *ic;
	struct run_softc *sc;
	uint8_t rvp_id;

	if (vap == NULL)
		return;

	ic = vap->iv_ic;
	sc = ic->ic_softc;

	RUN_LOCK(sc);

	m_freem(rvp->beacon_mbuf);
	rvp->beacon_mbuf = NULL;

	rvp_id = rvp->rvp_id;
	sc->ratectl_run &= ~(1 << rvp_id);
	sc->rvp_bmap &= ~(1 << rvp_id);
	run_set_region_4(sc, RT2860_SKEY(rvp_id, 0), 0, 128);
	run_set_region_4(sc, RT2860_BCN_BASE(rvp_id), 0, 512);
	--sc->rvp_cnt;

	RUN_DPRINTF(sc, RUN_DEBUG_STATE,
	    "vap=%p rvp_id=%d bmap=%x rvp_cnt=%d\n",
	    vap, rvp_id, sc->rvp_bmap, sc->rvp_cnt);

	RUN_UNLOCK(sc);

	ieee80211_ratectl_deinit(vap);
	ieee80211_vap_detach(vap);
	free(rvp, M_80211_VAP);
}

/*
 * There are numbers of functions need to be called in context thread.
 * Rather than creating taskqueue event for each of those functions,
 * here is all-for-one taskqueue callback function. This function
 * guarantees deferred functions are executed in the same order they
 * were enqueued.
 * '& RUN_CMDQ_MASQ' is to loop cmdq[].
 */
static void
run_cmdq_cb(void *arg, int pending)
{
	struct run_softc *sc = arg;
	uint8_t i;

	/* call cmdq[].func locked */
	RUN_LOCK(sc);
	for (i = sc->cmdq_exec; sc->cmdq[i].func && pending;
	    i = sc->cmdq_exec, pending--) {
		RUN_DPRINTF(sc, RUN_DEBUG_CMD, "cmdq_exec=%d pending=%d\n",
		    i, pending);
		if (sc->cmdq_run == RUN_CMDQ_GO) {
			/*
			 * If arg0 is NULL, callback func needs more
			 * than one arg. So, pass ptr to cmdq struct.
			 */
			if (sc->cmdq[i].arg0)
				sc->cmdq[i].func(sc->cmdq[i].arg0);
			else
				sc->cmdq[i].func(&sc->cmdq[i]);
		}
		sc->cmdq[i].arg0 = NULL;
		sc->cmdq[i].func = NULL;
		sc->cmdq_exec++;
		sc->cmdq_exec &= RUN_CMDQ_MASQ;
	}
	RUN_UNLOCK(sc);
}

static void
run_setup_tx_list(struct run_softc *sc, struct run_endpoint_queue *pq)
{
	struct run_tx_data *data;

	memset(pq, 0, sizeof(*pq));

	STAILQ_INIT(&pq->tx_qh);
	STAILQ_INIT(&pq->tx_fh);

	for (data = &pq->tx_data[0];
	    data < &pq->tx_data[RUN_TX_RING_COUNT]; data++) {
		data->sc = sc;
		STAILQ_INSERT_TAIL(&pq->tx_fh, data, next);
	}
	pq->tx_nfree = RUN_TX_RING_COUNT;
}

static void
run_unsetup_tx_list(struct run_softc *sc, struct run_endpoint_queue *pq)
{
	struct run_tx_data *data;

	/* make sure any subsequent use of the queues will fail */
	pq->tx_nfree = 0;
	STAILQ_INIT(&pq->tx_fh);
	STAILQ_INIT(&pq->tx_qh);

	/* free up all node references and mbufs */
	for (data = &pq->tx_data[0];
	    data < &pq->tx_data[RUN_TX_RING_COUNT]; data++) {
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
run_load_microcode(struct run_softc *sc)
{
	usb_device_request_t req;
	const struct firmware *fw;
	const u_char *base;
	uint32_t tmp;
	int ntries, error;
	const uint64_t *temp;
	uint64_t bytes;

	RUN_UNLOCK(sc);
	fw = firmware_get("runfw");
	RUN_LOCK(sc);
	if (fw == NULL) {
		device_printf(sc->sc_dev,
		    "failed loadfirmware of file %s\n", "runfw");
		return ENOENT;
	}

	if (fw->datasize != 8192) {
		device_printf(sc->sc_dev,
		    "invalid firmware size (should be 8KB)\n");
		error = EINVAL;
		goto fail;
	}

	/*
	 * RT3071/RT3072 use a different firmware
	 * run-rt2870 (8KB) contains both,
	 * first half (4KB) is for rt2870,
	 * last half is for rt3071.
	 */
	base = fw->data;
	if ((sc->mac_ver) != 0x2860 &&
	    (sc->mac_ver) != 0x2872 &&
	    (sc->mac_ver) != 0x3070) { 
		base += 4096;
	}

	/* cheap sanity check */
	temp = fw->data;
	bytes = *temp;
	if (bytes != be64toh(0xffffff0210280210ULL)) {
		device_printf(sc->sc_dev, "firmware checksum failed\n");
		error = EINVAL;
		goto fail;
	}

	/* write microcode image */
	if (sc->sc_flags & RUN_FLAG_FWLOAD_NEEDED) {
		run_write_region_1(sc, RT2870_FW_BASE, base, 4096);
		run_write(sc, RT2860_H2M_MAILBOX_CID, 0xffffffff);
		run_write(sc, RT2860_H2M_MAILBOX_STATUS, 0xffffffff);
	}

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2870_RESET;
	USETW(req.wValue, 8);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	if ((error = usbd_do_request(sc->sc_udev, &sc->sc_mtx, &req, NULL))
	    != 0) {
		device_printf(sc->sc_dev, "firmware reset failed\n");
		goto fail;
	}

	run_delay(sc, 10);

	run_write(sc, RT2860_H2M_BBPAGENT, 0);
	run_write(sc, RT2860_H2M_MAILBOX, 0);
	run_write(sc, RT2860_H2M_INTSRC, 0);
	if ((error = run_mcu_cmd(sc, RT2860_MCU_CMD_RFRESET, 0)) != 0)
		goto fail;

	/* wait until microcontroller is ready */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((error = run_read(sc, RT2860_SYS_CTRL, &tmp)) != 0)
			goto fail;
		if (tmp & RT2860_MCU_READY)
			break;
		run_delay(sc, 10);
	}
	if (ntries == 1000) {
		device_printf(sc->sc_dev,
		    "timeout waiting for MCU to initialize\n");
		error = ETIMEDOUT;
		goto fail;
	}
	device_printf(sc->sc_dev, "firmware %s ver. %u.%u loaded\n",
	    (base == fw->data) ? "RT2870" : "RT3071",
	    *(base + 4092), *(base + 4093));

fail:
	firmware_put(fw, FIRMWARE_UNLOAD);
	return (error);
}

static int
run_reset(struct run_softc *sc)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2870_RESET;
	USETW(req.wValue, 1);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return (usbd_do_request(sc->sc_udev, &sc->sc_mtx, &req, NULL));
}

static usb_error_t
run_do_request(struct run_softc *sc,
    struct usb_device_request *req, void *data)
{
	usb_error_t err;
	int ntries = 10;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	while (ntries--) {
		err = usbd_do_request_flags(sc->sc_udev, &sc->sc_mtx,
		    req, data, 0, NULL, 250 /* ms */);
		if (err == 0)
			break;
		RUN_DPRINTF(sc, RUN_DEBUG_USB,
		    "Control request failed, %s (retrying)\n",
		    usbd_errstr(err));
		run_delay(sc, 10);
	}
	return (err);
}

static int
run_read(struct run_softc *sc, uint16_t reg, uint32_t *val)
{
	uint32_t tmp;
	int error;

	error = run_read_region_1(sc, reg, (uint8_t *)&tmp, sizeof tmp);
	if (error == 0)
		*val = le32toh(tmp);
	else
		*val = 0xffffffff;
	return (error);
}

static int
run_read_region_1(struct run_softc *sc, uint16_t reg, uint8_t *buf, int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2870_READ_REGION_1;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);

	return (run_do_request(sc, &req, buf));
}

static int
run_write_2(struct run_softc *sc, uint16_t reg, uint16_t val)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2870_WRITE_2;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);

	return (run_do_request(sc, &req, NULL));
}

static int
run_write(struct run_softc *sc, uint16_t reg, uint32_t val)
{
	int error;

	if ((error = run_write_2(sc, reg, val & 0xffff)) == 0)
		error = run_write_2(sc, reg + 2, val >> 16);
	return (error);
}

static int
run_write_region_1(struct run_softc *sc, uint16_t reg, const uint8_t *buf,
    int len)
{
#if 1
	int i, error = 0;
	/*
	 * NB: the WRITE_REGION_1 command is not stable on RT2860.
	 * We thus issue multiple WRITE_2 commands instead.
	 */
	KASSERT((len & 1) == 0, ("run_write_region_1: Data too long.\n"));
	for (i = 0; i < len && error == 0; i += 2)
		error = run_write_2(sc, reg + i, buf[i] | buf[i + 1] << 8);
	return (error);
#else
	usb_device_request_t req;
	int error = 0;

	/*
	 * NOTE: It appears the WRITE_REGION_1 command cannot be
	 * passed a huge amount of data, which will crash the
	 * firmware. Limit amount of data passed to 64-bytes at a
	 * time.
	 */
	while (len > 0) {
		int delta = 64;
		if (delta > len)
			delta = len;

		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
		req.bRequest = RT2870_WRITE_REGION_1;
		USETW(req.wValue, 0);
		USETW(req.wIndex, reg);
		USETW(req.wLength, delta);
		error = run_do_request(sc, &req, __DECONST(uint8_t *, buf));
		if (error != 0)
			break;
		reg += delta;
		buf += delta;
		len -= delta;
	}
	return (error);
#endif
}

static int
run_set_region_4(struct run_softc *sc, uint16_t reg, uint32_t val, int len)
{
	int i, error = 0;

	KASSERT((len & 3) == 0, ("run_set_region_4: Invalid data length.\n"));
	for (i = 0; i < len && error == 0; i += 4)
		error = run_write(sc, reg + i, val);
	return (error);
}

static int
run_efuse_read(struct run_softc *sc, uint16_t addr, uint16_t *val, int count)
{
	uint32_t tmp;
	uint16_t reg;
	int error, ntries;

	if ((error = run_read(sc, RT3070_EFUSE_CTRL, &tmp)) != 0)
		return (error);

	if (count == 2)
		addr *= 2;
	/*-
	 * Read one 16-byte block into registers EFUSE_DATA[0-3]:
	 * DATA0: F E D C
	 * DATA1: B A 9 8
	 * DATA2: 7 6 5 4
	 * DATA3: 3 2 1 0
	 */
	tmp &= ~(RT3070_EFSROM_MODE_MASK | RT3070_EFSROM_AIN_MASK);
	tmp |= (addr & ~0xf) << RT3070_EFSROM_AIN_SHIFT | RT3070_EFSROM_KICK;
	run_write(sc, RT3070_EFUSE_CTRL, tmp);
	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = run_read(sc, RT3070_EFUSE_CTRL, &tmp)) != 0)
			return (error);
		if (!(tmp & RT3070_EFSROM_KICK))
			break;
		run_delay(sc, 2);
	}
	if (ntries == 100)
		return (ETIMEDOUT);

	if ((tmp & RT3070_EFUSE_AOUT_MASK) == RT3070_EFUSE_AOUT_MASK) {
		*val = 0xffff;	/* address not found */
		return (0);
	}
	/* determine to which 32-bit register our 16-bit word belongs */
	reg = RT3070_EFUSE_DATA3 - (addr & 0xc);
	if ((error = run_read(sc, reg, &tmp)) != 0)
		return (error);

	tmp >>= (8 * (addr & 0x3));
	*val = (addr & 1) ? tmp >> 16 : tmp & 0xffff;

	return (0);
}

/* Read 16-bit from eFUSE ROM for RT3xxx. */
static int
run_efuse_read_2(struct run_softc *sc, uint16_t addr, uint16_t *val)
{
	return (run_efuse_read(sc, addr, val, 2));
}

static int
run_eeprom_read_2(struct run_softc *sc, uint16_t addr, uint16_t *val)
{
	usb_device_request_t req;
	uint16_t tmp;
	int error;

	addr *= 2;
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2870_EEPROM_READ;
	USETW(req.wValue, 0);
	USETW(req.wIndex, addr);
	USETW(req.wLength, sizeof(tmp));

	error = usbd_do_request(sc->sc_udev, &sc->sc_mtx, &req, &tmp);
	if (error == 0)
		*val = le16toh(tmp);
	else
		*val = 0xffff;
	return (error);
}

static __inline int
run_srom_read(struct run_softc *sc, uint16_t addr, uint16_t *val)
{
	/* either eFUSE ROM or EEPROM */
	return sc->sc_srom_read(sc, addr, val);
}

static int
run_rt2870_rf_write(struct run_softc *sc, uint32_t val)
{
	uint32_t tmp;
	int error, ntries;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = run_read(sc, RT2860_RF_CSR_CFG0, &tmp)) != 0)
			return (error);
		if (!(tmp & RT2860_RF_REG_CTRL))
			break;
	}
	if (ntries == 10)
		return (ETIMEDOUT);

	return (run_write(sc, RT2860_RF_CSR_CFG0, val));
}

static int
run_rt3070_rf_read(struct run_softc *sc, uint8_t reg, uint8_t *val)
{
	uint32_t tmp;
	int error, ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = run_read(sc, RT3070_RF_CSR_CFG, &tmp)) != 0)
			return (error);
		if (!(tmp & RT3070_RF_KICK))
			break;
	}
	if (ntries == 100)
		return (ETIMEDOUT);

	tmp = RT3070_RF_KICK | reg << 8;
	if ((error = run_write(sc, RT3070_RF_CSR_CFG, tmp)) != 0)
		return (error);

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = run_read(sc, RT3070_RF_CSR_CFG, &tmp)) != 0)
			return (error);
		if (!(tmp & RT3070_RF_KICK))
			break;
	}
	if (ntries == 100)
		return (ETIMEDOUT);

	*val = tmp & 0xff;
	return (0);
}

static int
run_rt3070_rf_write(struct run_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int error, ntries;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = run_read(sc, RT3070_RF_CSR_CFG, &tmp)) != 0)
			return (error);
		if (!(tmp & RT3070_RF_KICK))
			break;
	}
	if (ntries == 10)
		return (ETIMEDOUT);

	tmp = RT3070_RF_WRITE | RT3070_RF_KICK | reg << 8 | val;
	return (run_write(sc, RT3070_RF_CSR_CFG, tmp));
}

static int
run_bbp_read(struct run_softc *sc, uint8_t reg, uint8_t *val)
{
	uint32_t tmp;
	int ntries, error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = run_read(sc, RT2860_BBP_CSR_CFG, &tmp)) != 0)
			return (error);
		if (!(tmp & RT2860_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return (ETIMEDOUT);

	tmp = RT2860_BBP_CSR_READ | RT2860_BBP_CSR_KICK | reg << 8;
	if ((error = run_write(sc, RT2860_BBP_CSR_CFG, tmp)) != 0)
		return (error);

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = run_read(sc, RT2860_BBP_CSR_CFG, &tmp)) != 0)
			return (error);
		if (!(tmp & RT2860_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return (ETIMEDOUT);

	*val = tmp & 0xff;
	return (0);
}

static int
run_bbp_write(struct run_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int ntries, error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = run_read(sc, RT2860_BBP_CSR_CFG, &tmp)) != 0)
			return (error);
		if (!(tmp & RT2860_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return (ETIMEDOUT);

	tmp = RT2860_BBP_CSR_KICK | reg << 8 | val;
	return (run_write(sc, RT2860_BBP_CSR_CFG, tmp));
}

/*
 * Send a command to the 8051 microcontroller unit.
 */
static int
run_mcu_cmd(struct run_softc *sc, uint8_t cmd, uint16_t arg)
{
	uint32_t tmp;
	int error, ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = run_read(sc, RT2860_H2M_MAILBOX, &tmp)) != 0)
			return error;
		if (!(tmp & RT2860_H2M_BUSY))
			break;
	}
	if (ntries == 100)
		return ETIMEDOUT;

	tmp = RT2860_H2M_BUSY | RT2860_TOKEN_NO_INTR << 16 | arg;
	if ((error = run_write(sc, RT2860_H2M_MAILBOX, tmp)) == 0)
		error = run_write(sc, RT2860_HOST_CMD, cmd);
	return (error);
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
	return (b32);
}

static const char *
run_get_rf(uint16_t rev)
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
	case RT3593_RF_3053:	return "RT3053";
	case RT5592_RF_5592:	return "RT5592";
	case RT5390_RF_5370:	return "RT5370";
	case RT5390_RF_5372:	return "RT5372";
	}
	return ("unknown");
}

static void
run_rt3593_get_txpower(struct run_softc *sc)
{
	uint16_t addr, val;
	int i;

	/* Read power settings for 2GHz channels. */
	for (i = 0; i < 14; i += 2) {
		addr = (sc->ntxchains == 3) ? RT3593_EEPROM_PWR2GHZ_BASE1 :
		    RT2860_EEPROM_PWR2GHZ_BASE1;
		run_srom_read(sc, addr + i / 2, &val);
		sc->txpow1[i + 0] = (int8_t)(val & 0xff);
		sc->txpow1[i + 1] = (int8_t)(val >> 8);

		addr = (sc->ntxchains == 3) ? RT3593_EEPROM_PWR2GHZ_BASE2 :
		    RT2860_EEPROM_PWR2GHZ_BASE2;
		run_srom_read(sc, addr + i / 2, &val);
		sc->txpow2[i + 0] = (int8_t)(val & 0xff);
		sc->txpow2[i + 1] = (int8_t)(val >> 8);

		if (sc->ntxchains == 3) {
			run_srom_read(sc, RT3593_EEPROM_PWR2GHZ_BASE3 + i / 2,
			    &val);
			sc->txpow3[i + 0] = (int8_t)(val & 0xff);
			sc->txpow3[i + 1] = (int8_t)(val >> 8);
		}
	}
	/* Fix broken Tx power entries. */
	for (i = 0; i < 14; i++) {
		if (sc->txpow1[i] > 31)
			sc->txpow1[i] = 5;
		if (sc->txpow2[i] > 31)
			sc->txpow2[i] = 5;
		if (sc->ntxchains == 3) {
			if (sc->txpow3[i] > 31)
				sc->txpow3[i] = 5;
		}
	}
	/* Read power settings for 5GHz channels. */
	for (i = 0; i < 40; i += 2) {
		run_srom_read(sc, RT3593_EEPROM_PWR5GHZ_BASE1 + i / 2, &val);
		sc->txpow1[i + 14] = (int8_t)(val & 0xff);
		sc->txpow1[i + 15] = (int8_t)(val >> 8);

		run_srom_read(sc, RT3593_EEPROM_PWR5GHZ_BASE2 + i / 2, &val);
		sc->txpow2[i + 14] = (int8_t)(val & 0xff);
		sc->txpow2[i + 15] = (int8_t)(val >> 8);

		if (sc->ntxchains == 3) {
			run_srom_read(sc, RT3593_EEPROM_PWR5GHZ_BASE3 + i / 2,
			    &val);
			sc->txpow3[i + 14] = (int8_t)(val & 0xff);
			sc->txpow3[i + 15] = (int8_t)(val >> 8);
		}
	}
}

static void
run_get_txpower(struct run_softc *sc)
{
	uint16_t val;
	int i;

	/* Read power settings for 2GHz channels. */
	for (i = 0; i < 14; i += 2) {
		run_srom_read(sc, RT2860_EEPROM_PWR2GHZ_BASE1 + i / 2, &val);
		sc->txpow1[i + 0] = (int8_t)(val & 0xff);
		sc->txpow1[i + 1] = (int8_t)(val >> 8);

		if (sc->mac_ver != 0x5390) {
			run_srom_read(sc,
			    RT2860_EEPROM_PWR2GHZ_BASE2 + i / 2, &val);
			sc->txpow2[i + 0] = (int8_t)(val & 0xff);
			sc->txpow2[i + 1] = (int8_t)(val >> 8);
		}
	}
	/* Fix broken Tx power entries. */
	for (i = 0; i < 14; i++) {
		if (sc->mac_ver >= 0x5390) {
			if (sc->txpow1[i] < 0 || sc->txpow1[i] > 39)
				sc->txpow1[i] = 5;
		} else {
			if (sc->txpow1[i] < 0 || sc->txpow1[i] > 31)
				sc->txpow1[i] = 5;
		}
		if (sc->mac_ver > 0x5390) {
			if (sc->txpow2[i] < 0 || sc->txpow2[i] > 39)
				sc->txpow2[i] = 5;
		} else if (sc->mac_ver < 0x5390) {
			if (sc->txpow2[i] < 0 || sc->txpow2[i] > 31)
				sc->txpow2[i] = 5;
		}
		RUN_DPRINTF(sc, RUN_DEBUG_TXPWR,
		    "chan %d: power1=%d, power2=%d\n",
		    rt2860_rf2850[i].chan, sc->txpow1[i], sc->txpow2[i]);
	}
	/* Read power settings for 5GHz channels. */
	for (i = 0; i < 40; i += 2) {
		run_srom_read(sc, RT2860_EEPROM_PWR5GHZ_BASE1 + i / 2, &val);
		sc->txpow1[i + 14] = (int8_t)(val & 0xff);
		sc->txpow1[i + 15] = (int8_t)(val >> 8);

		run_srom_read(sc, RT2860_EEPROM_PWR5GHZ_BASE2 + i / 2, &val);
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
		RUN_DPRINTF(sc, RUN_DEBUG_TXPWR,
		    "chan %d: power1=%d, power2=%d\n",
		    rt2860_rf2850[14 + i].chan, sc->txpow1[14 + i],
		    sc->txpow2[14 + i]);
	}
}

static int
run_read_eeprom(struct run_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int8_t delta_2ghz, delta_5ghz;
	uint32_t tmp;
	uint16_t val;
	int ridx, ant, i;

	/* check whether the ROM is eFUSE ROM or EEPROM */
	sc->sc_srom_read = run_eeprom_read_2;
	if (sc->mac_ver >= 0x3070) {
		run_read(sc, RT3070_EFUSE_CTRL, &tmp);
		RUN_DPRINTF(sc, RUN_DEBUG_ROM, "EFUSE_CTRL=0x%08x\n", tmp);
		if ((tmp & RT3070_SEL_EFUSE) || sc->mac_ver == 0x3593)
			sc->sc_srom_read = run_efuse_read_2;
	}

	/* read ROM version */
	run_srom_read(sc, RT2860_EEPROM_VERSION, &val);
	RUN_DPRINTF(sc, RUN_DEBUG_ROM,
	    "EEPROM rev=%d, FAE=%d\n", val >> 8, val & 0xff);

	/* read MAC address */
	run_srom_read(sc, RT2860_EEPROM_MAC01, &val);
	ic->ic_macaddr[0] = val & 0xff;
	ic->ic_macaddr[1] = val >> 8;
	run_srom_read(sc, RT2860_EEPROM_MAC23, &val);
	ic->ic_macaddr[2] = val & 0xff;
	ic->ic_macaddr[3] = val >> 8;
	run_srom_read(sc, RT2860_EEPROM_MAC45, &val);
	ic->ic_macaddr[4] = val & 0xff;
	ic->ic_macaddr[5] = val >> 8;

	if (sc->mac_ver < 0x3593) {
		/* read vender BBP settings */
		for (i = 0; i < 10; i++) {
			run_srom_read(sc, RT2860_EEPROM_BBP_BASE + i, &val);
			sc->bbp[i].val = val & 0xff;
			sc->bbp[i].reg = val >> 8;
			RUN_DPRINTF(sc, RUN_DEBUG_ROM,
			    "BBP%d=0x%02x\n", sc->bbp[i].reg, sc->bbp[i].val);
		}
		if (sc->mac_ver >= 0x3071) {
			/* read vendor RF settings */
			for (i = 0; i < 10; i++) {
				run_srom_read(sc, RT3071_EEPROM_RF_BASE + i,
				   &val);
				sc->rf[i].val = val & 0xff;
				sc->rf[i].reg = val >> 8;
				RUN_DPRINTF(sc, RUN_DEBUG_ROM, "RF%d=0x%02x\n",
				    sc->rf[i].reg, sc->rf[i].val);
			}
		}
	}

	/* read RF frequency offset from EEPROM */
	run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_FREQ_LEDS :
	    RT3593_EEPROM_FREQ, &val);
	sc->freq = ((val & 0xff) != 0xff) ? val & 0xff : 0;
	RUN_DPRINTF(sc, RUN_DEBUG_ROM, "EEPROM freq offset %d\n",
	    sc->freq & 0xff);

	run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_FREQ_LEDS :
	    RT3593_EEPROM_FREQ_LEDS, &val);
	if (val >> 8 != 0xff) {
		/* read LEDs operating mode */
		sc->leds = val >> 8;
		run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_LED1 :
		    RT3593_EEPROM_LED1, &sc->led[0]);
		run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_LED2 :
		    RT3593_EEPROM_LED2, &sc->led[1]);
		run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_LED3 :
		    RT3593_EEPROM_LED3, &sc->led[2]);
	} else {
		/* broken EEPROM, use default settings */
		sc->leds = 0x01;
		sc->led[0] = 0x5555;
		sc->led[1] = 0x2221;
		sc->led[2] = 0x5627;	/* differs from RT2860 */
	}
	RUN_DPRINTF(sc, RUN_DEBUG_ROM,
	    "EEPROM LED mode=0x%02x, LEDs=0x%04x/0x%04x/0x%04x\n",
	    sc->leds, sc->led[0], sc->led[1], sc->led[2]);

	/* read RF information */
	if (sc->mac_ver == 0x5390 || sc->mac_ver ==0x5392)
		run_srom_read(sc, 0x00, &val);
	else
		run_srom_read(sc, RT2860_EEPROM_ANTENNA, &val);

	if (val == 0xffff) {
		device_printf(sc->sc_dev,
		    "invalid EEPROM antenna info, using default\n");
		if (sc->mac_ver == 0x3572) {
			/* default to RF3052 2T2R */
			sc->rf_rev = RT3070_RF_3052;
			sc->ntxchains = 2;
			sc->nrxchains = 2;
		} else if (sc->mac_ver >= 0x3070) {
			/* default to RF3020 1T1R */
			sc->rf_rev = RT3070_RF_3020;
			sc->ntxchains = 1;
			sc->nrxchains = 1;
		} else {
			/* default to RF2820 1T2R */
			sc->rf_rev = RT2860_RF_2820;
			sc->ntxchains = 1;
			sc->nrxchains = 2;
		}
	} else {
		if (sc->mac_ver == 0x5390 || sc->mac_ver ==0x5392) {
			sc->rf_rev = val;
			run_srom_read(sc, RT2860_EEPROM_ANTENNA, &val);
		} else
			sc->rf_rev = (val >> 8) & 0xf;
		sc->ntxchains = (val >> 4) & 0xf;
		sc->nrxchains = val & 0xf;
	}
	RUN_DPRINTF(sc, RUN_DEBUG_ROM, "EEPROM RF rev=0x%04x chains=%dT%dR\n",
	    sc->rf_rev, sc->ntxchains, sc->nrxchains);

	/* check if RF supports automatic Tx access gain control */
	run_srom_read(sc, RT2860_EEPROM_CONFIG, &val);
	RUN_DPRINTF(sc, RUN_DEBUG_ROM, "EEPROM CFG 0x%04x\n", val);
	/* check if driver should patch the DAC issue */
	if ((val >> 8) != 0xff)
		sc->patch_dac = (val >> 15) & 1;
	if ((val & 0xff) != 0xff) {
		sc->ext_5ghz_lna = (val >> 3) & 1;
		sc->ext_2ghz_lna = (val >> 2) & 1;
		/* check if RF supports automatic Tx access gain control */
		sc->calib_2ghz = sc->calib_5ghz = (val >> 1) & 1;
		/* check if we have a hardware radio switch */
		sc->rfswitch = val & 1;
	}

	/* Read Tx power settings. */
	if (sc->mac_ver == 0x3593)
		run_rt3593_get_txpower(sc);
	else
		run_get_txpower(sc);

	/* read Tx power compensation for each Tx rate */
	run_srom_read(sc, RT2860_EEPROM_DELTAPWR, &val);
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
	RUN_DPRINTF(sc, RUN_DEBUG_ROM | RUN_DEBUG_TXPWR,
	    "power compensation=%d (2GHz), %d (5GHz)\n", delta_2ghz, delta_5ghz);

	for (ridx = 0; ridx < 5; ridx++) {
		uint32_t reg;

		run_srom_read(sc, RT2860_EEPROM_RPWR + ridx * 2, &val);
		reg = val;
		run_srom_read(sc, RT2860_EEPROM_RPWR + ridx * 2 + 1, &val);
		reg |= (uint32_t)val << 16;

		sc->txpow20mhz[ridx] = reg;
		sc->txpow40mhz_2ghz[ridx] = b4inc(reg, delta_2ghz);
		sc->txpow40mhz_5ghz[ridx] = b4inc(reg, delta_5ghz);

		RUN_DPRINTF(sc, RUN_DEBUG_ROM | RUN_DEBUG_TXPWR,
		    "ridx %d: power 20MHz=0x%08x, 40MHz/2GHz=0x%08x, "
		    "40MHz/5GHz=0x%08x\n", ridx, sc->txpow20mhz[ridx],
		    sc->txpow40mhz_2ghz[ridx], sc->txpow40mhz_5ghz[ridx]);
	}

	/* Read RSSI offsets and LNA gains from EEPROM. */
	run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_RSSI1_2GHZ :
	    RT3593_EEPROM_RSSI1_2GHZ, &val);
	sc->rssi_2ghz[0] = val & 0xff;	/* Ant A */
	sc->rssi_2ghz[1] = val >> 8;	/* Ant B */
	run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_RSSI2_2GHZ :
	    RT3593_EEPROM_RSSI2_2GHZ, &val);
	if (sc->mac_ver >= 0x3070) {
		if (sc->mac_ver == 0x3593) {
			sc->txmixgain_2ghz = 0;
			sc->rssi_2ghz[2] = val & 0xff;	/* Ant C */
		} else {
			/*
			 * On RT3070 chips (limited to 2 Rx chains), this ROM
			 * field contains the Tx mixer gain for the 2GHz band.
			 */
			if ((val & 0xff) != 0xff)
				sc->txmixgain_2ghz = val & 0x7;
		}
		RUN_DPRINTF(sc, RUN_DEBUG_ROM, "tx mixer gain=%u (2GHz)\n",
		    sc->txmixgain_2ghz);
	} else
		sc->rssi_2ghz[2] = val & 0xff;	/* Ant C */
	if (sc->mac_ver == 0x3593)
		run_srom_read(sc, RT3593_EEPROM_LNA_5GHZ, &val);
	sc->lna[2] = val >> 8;		/* channel group 2 */

	run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_RSSI1_5GHZ :
	    RT3593_EEPROM_RSSI1_5GHZ, &val);
	sc->rssi_5ghz[0] = val & 0xff;	/* Ant A */
	sc->rssi_5ghz[1] = val >> 8;	/* Ant B */
	run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_RSSI2_5GHZ :
	    RT3593_EEPROM_RSSI2_5GHZ, &val);
	if (sc->mac_ver == 0x3572) {
		/*
		 * On RT3572 chips (limited to 2 Rx chains), this ROM
		 * field contains the Tx mixer gain for the 5GHz band.
		 */
		if ((val & 0xff) != 0xff)
			sc->txmixgain_5ghz = val & 0x7;
		RUN_DPRINTF(sc, RUN_DEBUG_ROM, "tx mixer gain=%u (5GHz)\n",
		    sc->txmixgain_5ghz);
	} else
		sc->rssi_5ghz[2] = val & 0xff;	/* Ant C */
	if (sc->mac_ver == 0x3593) {
		sc->txmixgain_5ghz = 0;
		run_srom_read(sc, RT3593_EEPROM_LNA_5GHZ, &val);
	}
	sc->lna[3] = val >> 8;		/* channel group 3 */

	run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_LNA :
	    RT3593_EEPROM_LNA, &val);
	sc->lna[0] = val & 0xff;	/* channel group 0 */
	sc->lna[1] = val >> 8;		/* channel group 1 */

	/* fix broken 5GHz LNA entries */
	if (sc->lna[2] == 0 || sc->lna[2] == 0xff) {
		RUN_DPRINTF(sc, RUN_DEBUG_ROM,
		    "invalid LNA for channel group %d\n", 2);
		sc->lna[2] = sc->lna[1];
	}
	if (sc->lna[3] == 0 || sc->lna[3] == 0xff) {
		RUN_DPRINTF(sc, RUN_DEBUG_ROM,
		    "invalid LNA for channel group %d\n", 3);
		sc->lna[3] = sc->lna[1];
	}

	/* fix broken RSSI offset entries */
	for (ant = 0; ant < 3; ant++) {
		if (sc->rssi_2ghz[ant] < -10 || sc->rssi_2ghz[ant] > 10) {
			RUN_DPRINTF(sc, RUN_DEBUG_ROM | RUN_DEBUG_RSSI,
			    "invalid RSSI%d offset: %d (2GHz)\n",
			    ant + 1, sc->rssi_2ghz[ant]);
			sc->rssi_2ghz[ant] = 0;
		}
		if (sc->rssi_5ghz[ant] < -10 || sc->rssi_5ghz[ant] > 10) {
			RUN_DPRINTF(sc, RUN_DEBUG_ROM | RUN_DEBUG_RSSI,
			    "invalid RSSI%d offset: %d (5GHz)\n",
			    ant + 1, sc->rssi_5ghz[ant]);
			sc->rssi_5ghz[ant] = 0;
		}
	}
	return (0);
}

static struct ieee80211_node *
run_node_alloc(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	return malloc(sizeof (struct run_node), M_80211_NODE,
	    M_NOWAIT | M_ZERO);
}

static int
run_media_change(struct ifnet *ifp)
{
	struct ieee80211vap *vap = ifp->if_softc;
	struct ieee80211com *ic = vap->iv_ic;
	const struct ieee80211_txparam *tp;
	struct run_softc *sc = ic->ic_softc;
	uint8_t rate, ridx;
	int error;

	RUN_LOCK(sc);

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET) {
		RUN_UNLOCK(sc);
		return (error);
	}

	tp = &vap->iv_txparms[ieee80211_chan2mode(ic->ic_curchan)];
	if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE) {
		struct ieee80211_node *ni;
		struct run_node	*rn;

		rate = ic->ic_sup_rates[ic->ic_curmode].
		    rs_rates[tp->ucastrate] & IEEE80211_RATE_VAL;
		for (ridx = 0; ridx < RT2860_RIDX_MAX; ridx++)
			if (rt2860_rates[ridx].rate == rate)
				break;
		ni = ieee80211_ref_node(vap->iv_bss);
		rn = RUN_NODE(ni);
		rn->fix_ridx = ridx;
		RUN_DPRINTF(sc, RUN_DEBUG_RATE, "rate=%d, fix_ridx=%d\n",
		    rate, rn->fix_ridx);
		ieee80211_free_node(ni);
	}

#if 0
	if ((ifp->if_flags & IFF_UP) &&
	    (ifp->if_drv_flags &  RUN_RUNNING)){
		run_init_locked(sc);
	}
#endif

	RUN_UNLOCK(sc);

	return (0);
}

static int
run_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	const struct ieee80211_txparam *tp;
	struct ieee80211com *ic = vap->iv_ic;
	struct run_softc *sc = ic->ic_softc;
	struct run_vap *rvp = RUN_VAP(vap);
	enum ieee80211_state ostate;
	uint32_t sta[3];
	uint8_t ratectl;
	uint8_t restart_ratectl = 0;
	uint8_t bid = 1 << rvp->rvp_id;

	ostate = vap->iv_state;
	RUN_DPRINTF(sc, RUN_DEBUG_STATE, "%s -> %s\n",
		ieee80211_state_name[ostate],
		ieee80211_state_name[nstate]);

	IEEE80211_UNLOCK(ic);
	RUN_LOCK(sc);

	ratectl = sc->ratectl_run; /* remember current state */
	sc->ratectl_run = RUN_RATECTL_OFF;
	usb_callout_stop(&sc->ratectl_ch);

	if (ostate == IEEE80211_S_RUN) {
		/* turn link LED off */
		run_set_leds(sc, RT2860_LED_RADIO);
	}

	switch (nstate) {
	case IEEE80211_S_INIT:
		restart_ratectl = 1;

		if (ostate != IEEE80211_S_RUN)
			break;

		ratectl &= ~bid;
		sc->runbmap &= ~bid;

		/* abort TSF synchronization if there is no vap running */
		if (--sc->running == 0)
			run_disable_tsf(sc);
		break;

	case IEEE80211_S_RUN:
		if (!(sc->runbmap & bid)) {
			if(sc->running++)
				restart_ratectl = 1;
			sc->runbmap |= bid;
		}

		m_freem(rvp->beacon_mbuf);
		rvp->beacon_mbuf = NULL;

		switch (vap->iv_opmode) {
		case IEEE80211_M_HOSTAP:
		case IEEE80211_M_MBSS:
			sc->ap_running |= bid;
			ic->ic_opmode = vap->iv_opmode;
			run_update_beacon_cb(vap);
			break;
		case IEEE80211_M_IBSS:
			sc->adhoc_running |= bid;
			if (!sc->ap_running)
				ic->ic_opmode = vap->iv_opmode;
			run_update_beacon_cb(vap);
			break;
		case IEEE80211_M_STA:
			sc->sta_running |= bid;
			if (!sc->ap_running && !sc->adhoc_running)
				ic->ic_opmode = vap->iv_opmode;

			/* read statistic counters (clear on read) */
			run_read_region_1(sc, RT2860_TX_STA_CNT0,
			    (uint8_t *)sta, sizeof sta);

			break;
		default:
			ic->ic_opmode = vap->iv_opmode;
			break;
		}

		if (vap->iv_opmode != IEEE80211_M_MONITOR) {
			struct ieee80211_node *ni;

			if (ic->ic_bsschan == IEEE80211_CHAN_ANYC) {
				RUN_UNLOCK(sc);
				IEEE80211_LOCK(ic);
				return (-1);
			}
			run_updateslot(ic);
			run_enable_mrr(sc);
			run_set_txpreamble(sc);
			run_set_basicrates(sc);
			ni = ieee80211_ref_node(vap->iv_bss);
			IEEE80211_ADDR_COPY(sc->sc_bssid, ni->ni_bssid);
			run_set_bssid(sc, sc->sc_bssid);
			ieee80211_free_node(ni);
			run_enable_tsf_sync(sc);

			/* enable automatic rate adaptation */
			tp = &vap->iv_txparms[ieee80211_chan2mode(ic->ic_curchan)];
			if (tp->ucastrate == IEEE80211_FIXED_RATE_NONE)
				ratectl |= bid;
		} else
			run_enable_tsf(sc);

		/* turn link LED on */
		run_set_leds(sc, RT2860_LED_RADIO |
		    (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan) ?
		     RT2860_LED_LINK_2GHZ : RT2860_LED_LINK_5GHZ));

		break;
	default:
		RUN_DPRINTF(sc, RUN_DEBUG_STATE, "undefined state\n");
		break;
	}

	/* restart amrr for running VAPs */
	if ((sc->ratectl_run = ratectl) && restart_ratectl)
		usb_callout_reset(&sc->ratectl_ch, hz, run_ratectl_to, sc);

	RUN_UNLOCK(sc);
	IEEE80211_LOCK(ic);

	return(rvp->newstate(vap, nstate, arg));
}

static int
run_wme_update(struct ieee80211com *ic)
{
	struct chanAccParams chp;
	struct run_softc *sc = ic->ic_softc;
	const struct wmeParams *ac;
	int aci, error = 0;

	ieee80211_wme_ic_getparams(ic, &chp);
	ac = chp.cap_wmeParams;

	/* update MAC TX configuration registers */
	RUN_LOCK(sc);
	for (aci = 0; aci < WME_NUM_AC; aci++) {
		error = run_write(sc, RT2860_EDCA_AC_CFG(aci),
		    ac[aci].wmep_logcwmax << 16 |
		    ac[aci].wmep_logcwmin << 12 |
		    ac[aci].wmep_aifsn    <<  8 |
		    ac[aci].wmep_txopLimit);
		if (error) goto err;
	}

	/* update SCH/DMA registers too */
	error = run_write(sc, RT2860_WMM_AIFSN_CFG,
	    ac[WME_AC_VO].wmep_aifsn  << 12 |
	    ac[WME_AC_VI].wmep_aifsn  <<  8 |
	    ac[WME_AC_BK].wmep_aifsn  <<  4 |
	    ac[WME_AC_BE].wmep_aifsn);
	if (error) goto err;
	error = run_write(sc, RT2860_WMM_CWMIN_CFG,
	    ac[WME_AC_VO].wmep_logcwmin << 12 |
	    ac[WME_AC_VI].wmep_logcwmin <<  8 |
	    ac[WME_AC_BK].wmep_logcwmin <<  4 |
	    ac[WME_AC_BE].wmep_logcwmin);
	if (error) goto err;
	error = run_write(sc, RT2860_WMM_CWMAX_CFG,
	    ac[WME_AC_VO].wmep_logcwmax << 12 |
	    ac[WME_AC_VI].wmep_logcwmax <<  8 |
	    ac[WME_AC_BK].wmep_logcwmax <<  4 |
	    ac[WME_AC_BE].wmep_logcwmax);
	if (error) goto err;
	error = run_write(sc, RT2860_WMM_TXOP0_CFG,
	    ac[WME_AC_BK].wmep_txopLimit << 16 |
	    ac[WME_AC_BE].wmep_txopLimit);
	if (error) goto err;
	error = run_write(sc, RT2860_WMM_TXOP1_CFG,
	    ac[WME_AC_VO].wmep_txopLimit << 16 |
	    ac[WME_AC_VI].wmep_txopLimit);

err:
	RUN_UNLOCK(sc);
	if (error)
		RUN_DPRINTF(sc, RUN_DEBUG_USB, "WME update failed\n");

	return (error);
}

static void
run_key_set_cb(void *arg)
{
	struct run_cmdq *cmdq = arg;
	struct ieee80211vap *vap = cmdq->arg1;
	struct ieee80211_key *k = cmdq->k;
	struct ieee80211com *ic = vap->iv_ic;
	struct run_softc *sc = ic->ic_softc;
	struct ieee80211_node *ni;
	u_int cipher = k->wk_cipher->ic_cipher;
	uint32_t attr;
	uint16_t base, associd;
	uint8_t mode, wcid, iv[8];

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	if (vap->iv_opmode == IEEE80211_M_HOSTAP)
		ni = ieee80211_find_vap_node(&ic->ic_sta, vap, cmdq->mac);
	else
		ni = vap->iv_bss;
	associd = (ni != NULL) ? ni->ni_associd : 0;

	/* map net80211 cipher to RT2860 security mode */
	switch (cipher) {
	case IEEE80211_CIPHER_WEP:
		if(k->wk_keylen < 8)
			mode = RT2860_MODE_WEP40;
		else
			mode = RT2860_MODE_WEP104;
		break;
	case IEEE80211_CIPHER_TKIP:
		mode = RT2860_MODE_TKIP;
		break;
	case IEEE80211_CIPHER_AES_CCM:
		mode = RT2860_MODE_AES_CCMP;
		break;
	default:
		RUN_DPRINTF(sc, RUN_DEBUG_KEY, "undefined case\n");
		return;
	}

	RUN_DPRINTF(sc, RUN_DEBUG_KEY,
	    "associd=%x, keyix=%d, mode=%x, type=%s, tx=%s, rx=%s\n",
	    associd, k->wk_keyix, mode,
	    (k->wk_flags & IEEE80211_KEY_GROUP) ? "group" : "pairwise",
	    (k->wk_flags & IEEE80211_KEY_XMIT) ? "on" : "off",
	    (k->wk_flags & IEEE80211_KEY_RECV) ? "on" : "off");

	if (k->wk_flags & IEEE80211_KEY_GROUP) {
		wcid = 0;	/* NB: update WCID0 for group keys */
		base = RT2860_SKEY(RUN_VAP(vap)->rvp_id, k->wk_keyix);
	} else {
		wcid = (vap->iv_opmode == IEEE80211_M_STA) ?
		    1 : RUN_AID2WCID(associd);
		base = RT2860_PKEY(wcid);
	}

	if (cipher == IEEE80211_CIPHER_TKIP) {
		if(run_write_region_1(sc, base, k->wk_key, 16))
			return;
		if(run_write_region_1(sc, base + 16, &k->wk_key[16], 8))	/* wk_txmic */
			return;
		if(run_write_region_1(sc, base + 24, &k->wk_key[24], 8))	/* wk_rxmic */
			return;
	} else {
		/* roundup len to 16-bit: XXX fix write_region_1() instead */
		if(run_write_region_1(sc, base, k->wk_key, (k->wk_keylen + 1) & ~1))
			return;
	}

	if (!(k->wk_flags & IEEE80211_KEY_GROUP) ||
	    (k->wk_flags & (IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV))) {
		/* set initial packet number in IV+EIV */
		if (cipher == IEEE80211_CIPHER_WEP) {
			memset(iv, 0, sizeof iv);
			iv[3] = vap->iv_def_txkey << 6;
		} else {
			if (cipher == IEEE80211_CIPHER_TKIP) {
				iv[0] = k->wk_keytsc >> 8;
				iv[1] = (iv[0] | 0x20) & 0x7f;
				iv[2] = k->wk_keytsc;
			} else /* CCMP */ {
				iv[0] = k->wk_keytsc;
				iv[1] = k->wk_keytsc >> 8;
				iv[2] = 0;
			}
			iv[3] = k->wk_keyix << 6 | IEEE80211_WEP_EXTIV;
			iv[4] = k->wk_keytsc >> 16;
			iv[5] = k->wk_keytsc >> 24;
			iv[6] = k->wk_keytsc >> 32;
			iv[7] = k->wk_keytsc >> 40;
		}
		if (run_write_region_1(sc, RT2860_IVEIV(wcid), iv, 8))
			return;
	}

	if (k->wk_flags & IEEE80211_KEY_GROUP) {
		/* install group key */
		if (run_read(sc, RT2860_SKEY_MODE_0_7, &attr))
			return;
		attr &= ~(0xf << (k->wk_keyix * 4));
		attr |= mode << (k->wk_keyix * 4);
		if (run_write(sc, RT2860_SKEY_MODE_0_7, attr))
			return;
	} else {
		/* install pairwise key */
		if (run_read(sc, RT2860_WCID_ATTR(wcid), &attr))
			return;
		attr = (attr & ~0xf) | (mode << 1) | RT2860_RX_PKEY_EN;
		if (run_write(sc, RT2860_WCID_ATTR(wcid), attr))
			return;
	}

	/* TODO create a pass-thru key entry? */

	/* need wcid to delete the right key later */
	k->wk_pad = wcid;
}

/*
 * Don't have to be deferred, but in order to keep order of
 * execution, i.e. with run_key_delete(), defer this and let
 * run_cmdq_cb() maintain the order.
 *
 * return 0 on error
 */
static int
run_key_set(struct ieee80211vap *vap, struct ieee80211_key *k)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct run_softc *sc = ic->ic_softc;
	uint32_t i;

	i = RUN_CMDQ_GET(&sc->cmdq_store);
	RUN_DPRINTF(sc, RUN_DEBUG_KEY, "cmdq_store=%d\n", i);
	sc->cmdq[i].func = run_key_set_cb;
	sc->cmdq[i].arg0 = NULL;
	sc->cmdq[i].arg1 = vap;
	sc->cmdq[i].k = k;
	IEEE80211_ADDR_COPY(sc->cmdq[i].mac, k->wk_macaddr);
	ieee80211_runtask(ic, &sc->cmdq_task);

	/*
	 * To make sure key will be set when hostapd
	 * calls iv_key_set() before if_init().
	 */
	if (vap->iv_opmode == IEEE80211_M_HOSTAP) {
		RUN_LOCK(sc);
		sc->cmdq_key_set = RUN_CMDQ_GO;
		RUN_UNLOCK(sc);
	}

	return (1);
}

/*
 * If wlan is destroyed without being brought down i.e. without
 * wlan down or wpa_cli terminate, this function is called after
 * vap is gone. Don't refer it.
 */
static void
run_key_delete_cb(void *arg)
{
	struct run_cmdq *cmdq = arg;
	struct run_softc *sc = cmdq->arg1;
	struct ieee80211_key *k = &cmdq->key;
	uint32_t attr;
	uint8_t wcid;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	if (k->wk_flags & IEEE80211_KEY_GROUP) {
		/* remove group key */
		RUN_DPRINTF(sc, RUN_DEBUG_KEY, "removing group key\n");
		run_read(sc, RT2860_SKEY_MODE_0_7, &attr);
		attr &= ~(0xf << (k->wk_keyix * 4));
		run_write(sc, RT2860_SKEY_MODE_0_7, attr);
	} else {
		/* remove pairwise key */
		RUN_DPRINTF(sc, RUN_DEBUG_KEY,
		    "removing key for wcid %x\n", k->wk_pad);
		/* matching wcid was written to wk_pad in run_key_set() */
		wcid = k->wk_pad;
		run_read(sc, RT2860_WCID_ATTR(wcid), &attr);
		attr &= ~0xf;
		run_write(sc, RT2860_WCID_ATTR(wcid), attr);
		run_set_region_4(sc, RT2860_WCID_ENTRY(wcid), 0, 8);
	}

	k->wk_pad = 0;
}

/*
 * return 0 on error
 */
static int
run_key_delete(struct ieee80211vap *vap, struct ieee80211_key *k)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct run_softc *sc = ic->ic_softc;
	struct ieee80211_key *k0;
	uint32_t i;

	/*
	 * When called back, key might be gone. So, make a copy
	 * of some values need to delete keys before deferring.
	 * But, because of LOR with node lock, cannot use lock here.
	 * So, use atomic instead.
	 */
	i = RUN_CMDQ_GET(&sc->cmdq_store);
	RUN_DPRINTF(sc, RUN_DEBUG_KEY, "cmdq_store=%d\n", i);
	sc->cmdq[i].func = run_key_delete_cb;
	sc->cmdq[i].arg0 = NULL;
	sc->cmdq[i].arg1 = sc;
	k0 = &sc->cmdq[i].key;
	k0->wk_flags = k->wk_flags;
	k0->wk_keyix = k->wk_keyix;
	/* matching wcid was written to wk_pad in run_key_set() */
	k0->wk_pad = k->wk_pad;
	ieee80211_runtask(ic, &sc->cmdq_task);
	return (1);	/* return fake success */

}

static void
run_ratectl_to(void *arg)
{
	struct run_softc *sc = arg;

	/* do it in a process context, so it can go sleep */
	ieee80211_runtask(&sc->sc_ic, &sc->ratectl_task);
	/* next timeout will be rescheduled in the callback task */
}

/* ARGSUSED */
static void
run_ratectl_cb(void *arg, int pending)
{
	struct run_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	if (vap == NULL)
		return;

	if (sc->rvp_cnt > 1 || vap->iv_opmode != IEEE80211_M_STA) {
		/*
		 * run_reset_livelock() doesn't do anything with AMRR,
		 * but Ralink wants us to call it every 1 sec. So, we
		 * piggyback here rather than creating another callout.
		 * Livelock may occur only in HOSTAP or IBSS mode
		 * (when h/w is sending beacons).
		 */
		RUN_LOCK(sc);
		run_reset_livelock(sc);
		/* just in case, there are some stats to drain */
		run_drain_fifo(sc);
		RUN_UNLOCK(sc);
	}

	ieee80211_iterate_nodes(&ic->ic_sta, run_iter_func, sc);

	RUN_LOCK(sc);
	if(sc->ratectl_run != RUN_RATECTL_OFF)
		usb_callout_reset(&sc->ratectl_ch, hz, run_ratectl_to, sc);
	RUN_UNLOCK(sc);
}

static void
run_drain_fifo(void *arg)
{
	struct run_softc *sc = arg;
	uint32_t stat;
	uint16_t (*wstat)[3];
	uint8_t wcid, mcs, pid;
	int8_t retry;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	for (;;) {
		/* drain Tx status FIFO (maxsize = 16) */
		run_read(sc, RT2860_TX_STAT_FIFO, &stat);
		RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "tx stat 0x%08x\n", stat);
		if (!(stat & RT2860_TXQ_VLD))
			break;

		wcid = (stat >> RT2860_TXQ_WCID_SHIFT) & 0xff;

		/* if no ACK was requested, no feedback is available */
		if (!(stat & RT2860_TXQ_ACKREQ) || wcid > RT2870_WCID_MAX ||
		    wcid == 0)
			continue;

		/*
		 * Even though each stat is Tx-complete-status like format,
		 * the device can poll stats. Because there is no guarantee
		 * that the referring node is still around when read the stats.
		 * So that, if we use ieee80211_ratectl_tx_update(), we will
		 * have hard time not to refer already freed node.
		 *
		 * To eliminate such page faults, we poll stats in softc.
		 * Then, update the rates later with ieee80211_ratectl_tx_update().
		 */
		wstat = &(sc->wcid_stats[wcid]);
		(*wstat)[RUN_TXCNT]++;
		if (stat & RT2860_TXQ_OK)
			(*wstat)[RUN_SUCCESS]++;
		else
			counter_u64_add(sc->sc_ic.ic_oerrors, 1);
		/*
		 * Check if there were retries, ie if the Tx success rate is
		 * different from the requested rate. Note that it works only
		 * because we do not allow rate fallback from OFDM to CCK.
		 */
		mcs = (stat >> RT2860_TXQ_MCS_SHIFT) & 0x7f;
		pid = (stat >> RT2860_TXQ_PID_SHIFT) & 0xf;
		if ((retry = pid -1 - mcs) > 0) {
			(*wstat)[RUN_TXCNT] += retry;
			(*wstat)[RUN_RETRY] += retry;
		}
	}
	RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "count=%d\n", sc->fifo_cnt);

	sc->fifo_cnt = 0;
}

static void
run_iter_func(void *arg, struct ieee80211_node *ni)
{
	struct run_softc *sc = arg;
	struct ieee80211_ratectl_tx_stats *txs = &sc->sc_txs;
	struct ieee80211vap *vap = ni->ni_vap;
	struct run_node *rn = RUN_NODE(ni);
	union run_stats sta[2];
	uint16_t (*wstat)[3];
	int error;

	RUN_LOCK(sc);

	/* Check for special case */
	if (sc->rvp_cnt <= 1 && vap->iv_opmode == IEEE80211_M_STA &&
	    ni != vap->iv_bss)
		goto fail;

	txs->flags = IEEE80211_RATECTL_TX_STATS_NODE |
		     IEEE80211_RATECTL_TX_STATS_RETRIES;
	txs->ni = ni;
	if (sc->rvp_cnt <= 1 && (vap->iv_opmode == IEEE80211_M_IBSS ||
	    vap->iv_opmode == IEEE80211_M_STA)) {
		/* read statistic counters (clear on read) and update AMRR state */
		error = run_read_region_1(sc, RT2860_TX_STA_CNT0, (uint8_t *)sta,
		    sizeof sta);
		if (error != 0)
			goto fail;

		/* count failed TX as errors */
		if_inc_counter(vap->iv_ifp, IFCOUNTER_OERRORS,
		    le16toh(sta[0].error.fail));

		txs->nretries = le16toh(sta[1].tx.retry);
		txs->nsuccess = le16toh(sta[1].tx.success);
		/* nretries??? */
		txs->nframes = txs->nretries + txs->nsuccess +
		    le16toh(sta[0].error.fail);

		RUN_DPRINTF(sc, RUN_DEBUG_RATE,
		    "retrycnt=%d success=%d failcnt=%d\n",
		    txs->nretries, txs->nsuccess, le16toh(sta[0].error.fail));
	} else {
		wstat = &(sc->wcid_stats[RUN_AID2WCID(ni->ni_associd)]);

		if (wstat == &(sc->wcid_stats[0]) ||
		    wstat > &(sc->wcid_stats[RT2870_WCID_MAX]))
			goto fail;

		txs->nretries = (*wstat)[RUN_RETRY];
		txs->nsuccess = (*wstat)[RUN_SUCCESS];
		txs->nframes = (*wstat)[RUN_TXCNT];
		RUN_DPRINTF(sc, RUN_DEBUG_RATE,
		    "retrycnt=%d txcnt=%d success=%d\n",
		    txs->nretries, txs->nframes, txs->nsuccess);

		memset(wstat, 0, sizeof(*wstat));
	}

	ieee80211_ratectl_tx_update(vap, txs);
	rn->amrr_ridx = ieee80211_ratectl_rate(ni, NULL, 0);

fail:
	RUN_UNLOCK(sc);

	RUN_DPRINTF(sc, RUN_DEBUG_RATE, "ridx=%d\n", rn->amrr_ridx);
}

static void
run_newassoc_cb(void *arg)
{
	struct run_cmdq *cmdq = arg;
	struct ieee80211_node *ni = cmdq->arg1;
	struct run_softc *sc = ni->ni_vap->iv_ic->ic_softc;
	uint8_t wcid = cmdq->wcid;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	run_write_region_1(sc, RT2860_WCID_ENTRY(wcid),
	    ni->ni_macaddr, IEEE80211_ADDR_LEN);

	memset(&(sc->wcid_stats[wcid]), 0, sizeof(sc->wcid_stats[wcid]));
}

static void
run_newassoc(struct ieee80211_node *ni, int isnew)
{
	struct run_node *rn = RUN_NODE(ni);
	struct ieee80211_rateset *rs = &ni->ni_rates;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = vap->iv_ic;
	struct run_softc *sc = ic->ic_softc;
	uint8_t rate;
	uint8_t ridx;
	uint8_t wcid;
	int i, j;

	wcid = (vap->iv_opmode == IEEE80211_M_STA) ?
	    1 : RUN_AID2WCID(ni->ni_associd);

	if (wcid > RT2870_WCID_MAX) {
		device_printf(sc->sc_dev, "wcid=%d out of range\n", wcid);
		return;
	}

	/* only interested in true associations */
	if (isnew && ni->ni_associd != 0) {

		/*
		 * This function could is called though timeout function.
		 * Need to defer.
		 */
		uint32_t cnt = RUN_CMDQ_GET(&sc->cmdq_store);
		RUN_DPRINTF(sc, RUN_DEBUG_STATE, "cmdq_store=%d\n", cnt);
		sc->cmdq[cnt].func = run_newassoc_cb;
		sc->cmdq[cnt].arg0 = NULL;
		sc->cmdq[cnt].arg1 = ni;
		sc->cmdq[cnt].wcid = wcid;
		ieee80211_runtask(ic, &sc->cmdq_task);
	}

	RUN_DPRINTF(sc, RUN_DEBUG_STATE,
	    "new assoc isnew=%d associd=%x addr=%s\n",
	    isnew, ni->ni_associd, ether_sprintf(ni->ni_macaddr));

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
		RUN_DPRINTF(sc, RUN_DEBUG_STATE | RUN_DEBUG_RATE,
		    "rate=0x%02x ridx=%d ctl_ridx=%d\n",
		    rs->rs_rates[i], rn->ridx[i], rn->ctl_ridx[i]);
	}
	rate = vap->iv_txparms[ieee80211_chan2mode(ic->ic_curchan)].mgmtrate;
	for (ridx = 0; ridx < RT2860_RIDX_MAX; ridx++)
		if (rt2860_rates[ridx].rate == rate)
			break;
	rn->mgt_ridx = ridx;
	RUN_DPRINTF(sc, RUN_DEBUG_STATE | RUN_DEBUG_RATE,
	    "rate=%d, mgmt_ridx=%d\n", rate, rn->mgt_ridx);

	RUN_LOCK(sc);
	if(sc->ratectl_run != RUN_RATECTL_OFF)
		usb_callout_reset(&sc->ratectl_ch, hz, run_ratectl_to, sc);
	RUN_UNLOCK(sc);
}

/*
 * Return the Rx chain with the highest RSSI for a given frame.
 */
static __inline uint8_t
run_maxrssi_chain(struct run_softc *sc, const struct rt2860_rxwi *rxwi)
{
	uint8_t rxchain = 0;

	if (sc->nrxchains > 1) {
		if (rxwi->rssi[1] > rxwi->rssi[rxchain])
			rxchain = 1;
		if (sc->nrxchains > 2)
			if (rxwi->rssi[2] > rxwi->rssi[rxchain])
				rxchain = 2;
	}
	return (rxchain);
}

static void
run_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m, int subtype,
    const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct run_softc *sc = vap->iv_ic->ic_softc;
	struct run_vap *rvp = RUN_VAP(vap);
	uint64_t ni_tstamp, rx_tstamp;

	rvp->recv_mgmt(ni, m, subtype, rxs, rssi, nf);

	if (vap->iv_state == IEEE80211_S_RUN &&
	    (subtype == IEEE80211_FC0_SUBTYPE_BEACON ||
	    subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)) {
		ni_tstamp = le64toh(ni->ni_tstamp.tsf);
		RUN_LOCK(sc);
		run_get_tsf(sc, &rx_tstamp);
		RUN_UNLOCK(sc);
		rx_tstamp = le64toh(rx_tstamp);

		if (ni_tstamp >= rx_tstamp) {
			RUN_DPRINTF(sc, RUN_DEBUG_RECV | RUN_DEBUG_BEACON,
			    "ibss merge, tsf %ju tstamp %ju\n",
			    (uintmax_t)rx_tstamp, (uintmax_t)ni_tstamp);
			(void) ieee80211_ibss_merge(ni);
		}
	}
}

static void
run_rx_frame(struct run_softc *sc, struct mbuf *m, uint32_t dmalen)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct rt2870_rxd *rxd;
	struct rt2860_rxwi *rxwi;
	uint32_t flags;
	uint16_t len, rxwisize;
	uint8_t ant, rssi;
	int8_t nf;

	rxwisize = sizeof(struct rt2860_rxwi);
	if (sc->mac_ver == 0x5592)
		rxwisize += sizeof(uint64_t);
	else if (sc->mac_ver == 0x3593)
		rxwisize += sizeof(uint32_t);

	if (__predict_false(dmalen <
	    rxwisize + sizeof(struct ieee80211_frame_ack))) {
		RUN_DPRINTF(sc, RUN_DEBUG_RECV,
		    "payload is too short: dma length %u < %zu\n",
		    dmalen, rxwisize + sizeof(struct ieee80211_frame_ack));
		goto fail;
	}

	rxwi = mtod(m, struct rt2860_rxwi *);
	len = le16toh(rxwi->len) & 0xfff;

	if (__predict_false(len > dmalen - rxwisize)) {
		RUN_DPRINTF(sc, RUN_DEBUG_RECV,
		    "bad RXWI length %u > %u\n", len, dmalen);
		goto fail;
	}

	/* Rx descriptor is located at the end */
	rxd = (struct rt2870_rxd *)(mtod(m, caddr_t) + dmalen);
	flags = le32toh(rxd->flags);

	if (__predict_false(flags & (RT2860_RX_CRCERR | RT2860_RX_ICVERR))) {
		RUN_DPRINTF(sc, RUN_DEBUG_RECV, "%s error.\n",
		    (flags & RT2860_RX_CRCERR)?"CRC":"ICV");
		goto fail;
	}

	if (flags & RT2860_RX_L2PAD) {
		/*
		 * XXX OpenBSD removes padding between header
		 * and payload here...
		 */
		RUN_DPRINTF(sc, RUN_DEBUG_RECV,
		    "received RT2860_RX_L2PAD frame\n");
		len += 2;
	}

	m->m_data += rxwisize;
	m->m_pkthdr.len = m->m_len = len;

	wh = mtod(m, struct ieee80211_frame *);

	/* XXX wrong for monitor mode */
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
		m->m_flags |= M_WEP;
	}

	if (len >= sizeof(struct ieee80211_frame_min)) {
		ni = ieee80211_find_rxnode(ic,
		    mtod(m, struct ieee80211_frame_min *));
	} else
		ni = NULL;

	if (__predict_false(flags & RT2860_RX_MICERR)) {
		/* report MIC failures to net80211 for TKIP */
		if (ni != NULL)
			ieee80211_notify_michael_failure(ni->ni_vap, wh,
			    rxwi->keyidx);
		RUN_DPRINTF(sc, RUN_DEBUG_RECV,
		    "MIC error. Someone is lying.\n");
		goto fail;
	}

	ant = run_maxrssi_chain(sc, rxwi);
	rssi = rxwi->rssi[ant];
	nf = run_rssi2dbm(sc, rssi, ant);

	if (__predict_false(ieee80211_radiotap_active(ic))) {
		struct run_rx_radiotap_header *tap = &sc->sc_rxtap;
		uint16_t phy;

		tap->wr_flags = 0;
		tap->wr_antsignal = rssi;
		tap->wr_antenna = ant;
		tap->wr_dbm_antsignal = run_rssi2dbm(sc, rssi, ant);
		tap->wr_rate = 2;	/* in case it can't be found below */
		RUN_LOCK(sc);
		run_get_tsf(sc, &tap->wr_tsf);
		RUN_UNLOCK(sc);
		phy = le16toh(rxwi->phy);
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
	}

	if (ni != NULL) {
		(void)ieee80211_input(ni, m, rssi, nf);
		ieee80211_free_node(ni);
	} else {
		(void)ieee80211_input_all(ic, m, rssi, nf);
	}

	return;

fail:
	m_freem(m);
	counter_u64_add(ic->ic_ierrors, 1);
}

static void
run_bulk_rx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct run_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct mbuf *m = NULL;
	struct mbuf *m0;
	uint32_t dmalen, mbuf_len;
	uint16_t rxwisize;
	int xferlen;

	rxwisize = sizeof(struct rt2860_rxwi);
	if (sc->mac_ver == 0x5592)
		rxwisize += sizeof(uint64_t);
	else if (sc->mac_ver == 0x3593)
		rxwisize += sizeof(uint32_t);

	usbd_xfer_status(xfer, &xferlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		RUN_DPRINTF(sc, RUN_DEBUG_RECV,
		    "rx done, actlen=%d\n", xferlen);

		if (xferlen < (int)(sizeof(uint32_t) + rxwisize +
		    sizeof(struct rt2870_rxd))) {
			RUN_DPRINTF(sc, RUN_DEBUG_RECV_DESC | RUN_DEBUG_USB,
			    "xfer too short %d\n", xferlen);
			goto tr_setup;
		}

		m = sc->rx_m;
		sc->rx_m = NULL;

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		if (sc->rx_m == NULL) {
			sc->rx_m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR,
			    MJUMPAGESIZE /* xfer can be bigger than MCLBYTES */);
		}
		if (sc->rx_m == NULL) {
			RUN_DPRINTF(sc, RUN_DEBUG_RECV | RUN_DEBUG_RECV_DESC,
			    "could not allocate mbuf - idle with stall\n");
			counter_u64_add(ic->ic_ierrors, 1);
			usbd_xfer_set_stall(xfer);
			usbd_xfer_set_frames(xfer, 0);
		} else {
			/*
			 * Directly loading a mbuf cluster into DMA to
			 * save some data copying. This works because
			 * there is only one cluster.
			 */
			usbd_xfer_set_frame_data(xfer, 0, 
			    mtod(sc->rx_m, caddr_t), RUN_MAX_RXSZ);
			usbd_xfer_set_frames(xfer, 1);
		}
		usbd_transfer_submit(xfer);
		break;

	default:	/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			if (error == USB_ERR_TIMEOUT)
				device_printf(sc->sc_dev, "device timeout\n");
			counter_u64_add(ic->ic_ierrors, 1);
			goto tr_setup;
		}
		if (sc->rx_m != NULL) {
			m_freem(sc->rx_m);
			sc->rx_m = NULL;
		}
		break;
	}

	if (m == NULL)
		return;

	/* inputting all the frames must be last */

	RUN_UNLOCK(sc);

	m->m_pkthdr.len = m->m_len = xferlen;

	/* HW can aggregate multiple 802.11 frames in a single USB xfer */
	for(;;) {
		dmalen = le32toh(*mtod(m, uint32_t *)) & 0xffff;

		if ((dmalen >= (uint32_t)-8) || (dmalen == 0) ||
		    ((dmalen & 3) != 0)) {
			RUN_DPRINTF(sc, RUN_DEBUG_RECV_DESC | RUN_DEBUG_USB,
			    "bad DMA length %u\n", dmalen);
			break;
		}
		if ((dmalen + 8) > (uint32_t)xferlen) {
			RUN_DPRINTF(sc, RUN_DEBUG_RECV_DESC | RUN_DEBUG_USB,
			    "bad DMA length %u > %d\n",
			dmalen + 8, xferlen);
			break;
		}

		/* If it is the last one or a single frame, we won't copy. */
		if ((xferlen -= dmalen + 8) <= 8) {
			/* trim 32-bit DMA-len header */
			m->m_data += 4;
			m->m_pkthdr.len = m->m_len -= 4;
			run_rx_frame(sc, m, dmalen);
			m = NULL;	/* don't free source buffer */
			break;
		}

		mbuf_len = dmalen + sizeof(struct rt2870_rxd);
		if (__predict_false(mbuf_len > MCLBYTES)) {
			RUN_DPRINTF(sc, RUN_DEBUG_RECV_DESC | RUN_DEBUG_USB,
			    "payload is too big: mbuf_len %u\n", mbuf_len);
			counter_u64_add(ic->ic_ierrors, 1);
			break;
		}

		/* copy aggregated frames to another mbuf */
		m0 = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (__predict_false(m0 == NULL)) {
			RUN_DPRINTF(sc, RUN_DEBUG_RECV_DESC,
			    "could not allocate mbuf\n");
			counter_u64_add(ic->ic_ierrors, 1);
			break;
		}
		m_copydata(m, 4 /* skip 32-bit DMA-len header */,
		    mbuf_len, mtod(m0, caddr_t));
		m0->m_pkthdr.len = m0->m_len = mbuf_len;
		run_rx_frame(sc, m0, dmalen);

		/* update data ptr */
		m->m_data += mbuf_len + 4;
		m->m_pkthdr.len = m->m_len -= mbuf_len + 4;
	}

	/* make sure we free the source buffer, if any */
	m_freem(m);

	RUN_LOCK(sc);
}

static void
run_tx_free(struct run_endpoint_queue *pq,
    struct run_tx_data *data, int txerr)
{

	ieee80211_tx_complete(data->ni, data->m, txerr);

	data->m = NULL;
	data->ni = NULL;

	STAILQ_INSERT_TAIL(&pq->tx_fh, data, next);
	pq->tx_nfree++;
}

static void
run_bulk_tx_callbackN(struct usb_xfer *xfer, usb_error_t error, u_int index)
{
	struct run_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct run_tx_data *data;
	struct ieee80211vap *vap = NULL;
	struct usb_page_cache *pc;
	struct run_endpoint_queue *pq = &sc->sc_epq[index];
	struct mbuf *m;
	usb_frlength_t size;
	int actlen;
	int sumlen;

	usbd_xfer_status(xfer, &actlen, &sumlen, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		RUN_DPRINTF(sc, RUN_DEBUG_XMIT | RUN_DEBUG_USB,
		    "transfer complete: %d bytes @ index %d\n", actlen, index);

		data = usbd_xfer_get_priv(xfer);
		run_tx_free(pq, data, 0);
		usbd_xfer_set_priv(xfer, NULL);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		data = STAILQ_FIRST(&pq->tx_qh);
		if (data == NULL)
			break;

		STAILQ_REMOVE_HEAD(&pq->tx_qh, next);

		m = data->m;
		size = (sc->mac_ver == 0x5592) ?
		    sizeof(data->desc) + sizeof(uint32_t) : sizeof(data->desc);
		if ((m->m_pkthdr.len +
		    size + 3 + 8) > RUN_MAX_TXSZ) {
			RUN_DPRINTF(sc, RUN_DEBUG_XMIT_DESC | RUN_DEBUG_USB,
			    "data overflow, %u bytes\n", m->m_pkthdr.len);
			run_tx_free(pq, data, 1);
			goto tr_setup;
		}

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_in(pc, 0, &data->desc, size);
		usbd_m_copy_in(pc, size, m, 0, m->m_pkthdr.len);
		size += m->m_pkthdr.len;
		/*
		 * Align end on a 4-byte boundary, pad 8 bytes (CRC +
		 * 4-byte padding), and be sure to zero those trailing
		 * bytes:
		 */
		usbd_frame_zero(pc, size, ((-size) & 3) + 8);
		size += ((-size) & 3) + 8;

		vap = data->ni->ni_vap;
		if (ieee80211_radiotap_active_vap(vap)) {
			struct run_tx_radiotap_header *tap = &sc->sc_txtap;
			struct rt2860_txwi *txwi = 
			    (struct rt2860_txwi *)(&data->desc + sizeof(struct rt2870_txd));
			tap->wt_flags = 0;
			tap->wt_rate = rt2860_rates[data->ridx].rate;
			tap->wt_hwqueue = index;
			if (le16toh(txwi->phy) & RT2860_PHY_SHPRE)
				tap->wt_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;

			ieee80211_radiotap_tx(vap, m);
		}

		RUN_DPRINTF(sc, RUN_DEBUG_XMIT | RUN_DEBUG_USB,
		    "sending frame len=%u/%u @ index %d\n",
		    m->m_pkthdr.len, size, index);

		usbd_xfer_set_frame_len(xfer, 0, size);
		usbd_xfer_set_priv(xfer, data);
		usbd_transfer_submit(xfer);
		run_start(sc);

		break;

	default:
		RUN_DPRINTF(sc, RUN_DEBUG_XMIT | RUN_DEBUG_USB,
		    "USB transfer error, %s\n", usbd_errstr(error));

		data = usbd_xfer_get_priv(xfer);

		if (data != NULL) {
			if(data->ni != NULL)
				vap = data->ni->ni_vap;
			run_tx_free(pq, data, error);
			usbd_xfer_set_priv(xfer, NULL);
		}

		if (vap == NULL)
			vap = TAILQ_FIRST(&ic->ic_vaps);

		if (error != USB_ERR_CANCELLED) {
			if (error == USB_ERR_TIMEOUT) {
				device_printf(sc->sc_dev, "device timeout\n");
				uint32_t i = RUN_CMDQ_GET(&sc->cmdq_store);
				RUN_DPRINTF(sc, RUN_DEBUG_XMIT | RUN_DEBUG_USB,
				    "cmdq_store=%d\n", i);
				sc->cmdq[i].func = run_usb_timeout_cb;
				sc->cmdq[i].arg0 = vap;
				ieee80211_runtask(ic, &sc->cmdq_task);
			}

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

static void
run_bulk_tx_callback0(struct usb_xfer *xfer, usb_error_t error)
{
	run_bulk_tx_callbackN(xfer, error, 0);
}

static void
run_bulk_tx_callback1(struct usb_xfer *xfer, usb_error_t error)
{
	run_bulk_tx_callbackN(xfer, error, 1);
}

static void
run_bulk_tx_callback2(struct usb_xfer *xfer, usb_error_t error)
{
	run_bulk_tx_callbackN(xfer, error, 2);
}

static void
run_bulk_tx_callback3(struct usb_xfer *xfer, usb_error_t error)
{
	run_bulk_tx_callbackN(xfer, error, 3);
}

static void
run_bulk_tx_callback4(struct usb_xfer *xfer, usb_error_t error)
{
	run_bulk_tx_callbackN(xfer, error, 4);
}

static void
run_bulk_tx_callback5(struct usb_xfer *xfer, usb_error_t error)
{
	run_bulk_tx_callbackN(xfer, error, 5);
}

static void
run_set_tx_desc(struct run_softc *sc, struct run_tx_data *data)
{
	struct mbuf *m = data->m;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = data->ni->ni_vap;
	struct ieee80211_frame *wh;
	struct rt2870_txd *txd;
	struct rt2860_txwi *txwi;
	uint16_t xferlen, txwisize;
	uint16_t mcs;
	uint8_t ridx = data->ridx;
	uint8_t pad;

	/* get MCS code from rate index */
	mcs = rt2860_rates[ridx].mcs;

	txwisize = (sc->mac_ver == 0x5592) ?
	    sizeof(*txwi) + sizeof(uint32_t) : sizeof(*txwi);
	xferlen = txwisize + m->m_pkthdr.len;

	/* roundup to 32-bit alignment */
	xferlen = (xferlen + 3) & ~3;

	txd = (struct rt2870_txd *)&data->desc;
	txd->len = htole16(xferlen);

	wh = mtod(m, struct ieee80211_frame *);

	/*
	 * Ether both are true or both are false, the header
	 * are nicely aligned to 32-bit. So, no L2 padding.
	 */
	if(IEEE80211_HAS_ADDR4(wh) == IEEE80211_QOS_HAS_SEQ(wh))
		pad = 0;
	else
		pad = 2;

	/* setup TX Wireless Information */
	txwi = (struct rt2860_txwi *)(txd + 1);
	txwi->len = htole16(m->m_pkthdr.len - pad);
	if (rt2860_rates[ridx].phy == IEEE80211_T_DS) {
		mcs |= RT2860_PHY_CCK;
		if (ridx != RT2860_RIDX_CCK1 &&
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			mcs |= RT2860_PHY_SHPRE;
	} else
		mcs |= RT2860_PHY_OFDM;
	txwi->phy = htole16(mcs);

	/* check if RTS/CTS or CTS-to-self protection is required */
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (m->m_pkthdr.len + IEEE80211_CRC_LEN > vap->iv_rtsthreshold ||
	     ((ic->ic_flags & IEEE80211_F_USEPROT) &&
	      rt2860_rates[ridx].phy == IEEE80211_T_OFDM)))
		txwi->txop |= RT2860_TX_TXOP_HT;
	else
		txwi->txop |= RT2860_TX_TXOP_BACKOFF;

	if (vap->iv_opmode != IEEE80211_M_STA && !IEEE80211_QOS_HAS_SEQ(wh))
		txwi->xflags |= RT2860_TX_NSEQ;
}

/* This function must be called locked */
static int
run_tx(struct run_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_frame *wh;
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct run_node *rn = RUN_NODE(ni);
	struct run_tx_data *data;
	struct rt2870_txd *txd;
	struct rt2860_txwi *txwi;
	uint16_t qos;
	uint16_t dur;
	uint16_t qid;
	uint8_t type;
	uint8_t tid;
	uint8_t ridx;
	uint8_t ctl_ridx;
	uint8_t qflags;
	uint8_t xflags = 0;
	int hasqos;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	wh = mtod(m, struct ieee80211_frame *);

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	/*
	 * There are 7 bulk endpoints: 1 for RX
	 * and 6 for TX (4 EDCAs + HCCA + Prio).
	 * Update 03-14-2009:  some devices like the Planex GW-US300MiniS
	 * seem to have only 4 TX bulk endpoints (Fukaumi Naoki).
	 */
	if ((hasqos = IEEE80211_QOS_HAS_SEQ(wh))) {
		uint8_t *frm;

		frm = ieee80211_getqos(wh);
		qos = le16toh(*(const uint16_t *)frm);
		tid = qos & IEEE80211_QOS_TID;
		qid = TID_TO_WME_AC(tid);
	} else {
		qos = 0;
		tid = 0;
		qid = WME_AC_BE;
	}
	qflags = (qid < 4) ? RT2860_TX_QSEL_EDCA : RT2860_TX_QSEL_HCCA;

	RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "qos %d\tqid %d\ttid %d\tqflags %x\n",
	    qos, qid, tid, qflags);

	/* pickup a rate index */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA || m->m_flags & M_EAPOL) {
		ridx = (ic->ic_curmode == IEEE80211_MODE_11A) ?
		    RT2860_RIDX_OFDM6 : RT2860_RIDX_CCK1;
		ctl_ridx = rt2860_rates[ridx].ctl_ridx;
	} else {
		if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE)
			ridx = rn->fix_ridx;
		else
			ridx = rn->amrr_ridx;
		ctl_ridx = rt2860_rates[ridx].ctl_ridx;
	}

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (!hasqos || (qos & IEEE80211_QOS_ACKPOLICY) !=
	     IEEE80211_QOS_ACKPOLICY_NOACK)) {
		xflags |= RT2860_TX_ACK;
		if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
			dur = rt2860_rates[ctl_ridx].sp_ack_dur;
		else
			dur = rt2860_rates[ctl_ridx].lp_ack_dur;
		USETW(wh->i_dur, dur);
	}

	/* reserve slots for mgmt packets, just in case */
	if (sc->sc_epq[qid].tx_nfree < 3) {
		RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "tx ring %d is full\n", qid);
		return (-1);
	}

	data = STAILQ_FIRST(&sc->sc_epq[qid].tx_fh);
	STAILQ_REMOVE_HEAD(&sc->sc_epq[qid].tx_fh, next);
	sc->sc_epq[qid].tx_nfree--;

	txd = (struct rt2870_txd *)&data->desc;
	txd->flags = qflags;
	txwi = (struct rt2860_txwi *)(txd + 1);
	txwi->xflags = xflags;
	if (IEEE80211_IS_MULTICAST(wh->i_addr1))
		txwi->wcid = 0;
	else
		txwi->wcid = (vap->iv_opmode == IEEE80211_M_STA) ?
		    1 : RUN_AID2WCID(ni->ni_associd);

	/* clear leftover garbage bits */
	txwi->flags = 0;
	txwi->txop = 0;

	data->m = m;
	data->ni = ni;
	data->ridx = ridx;

	run_set_tx_desc(sc, data);

	/*
	 * The chip keeps track of 2 kind of Tx stats,
	 *  * TX_STAT_FIFO, for per WCID stats, and
	 *  * TX_STA_CNT0 for all-TX-in-one stats.
	 *
	 * To use FIFO stats, we need to store MCS into the driver-private
 	 * PacketID field. So that, we can tell whose stats when we read them.
 	 * We add 1 to the MCS because setting the PacketID field to 0 means
 	 * that we don't want feedback in TX_STAT_FIFO.
 	 * And, that's what we want for STA mode, since TX_STA_CNT0 does the job.
 	 *
 	 * FIFO stats doesn't count Tx with WCID 0xff, so we do this in run_tx().
 	 */
	if (sc->rvp_cnt > 1 || vap->iv_opmode == IEEE80211_M_HOSTAP ||
	    vap->iv_opmode == IEEE80211_M_MBSS) {
		uint16_t pid = (rt2860_rates[ridx].mcs + 1) & 0xf;
		txwi->len |= htole16(pid << RT2860_TX_PID_SHIFT);

		/*
		 * Unlike PCI based devices, we don't get any interrupt from
		 * USB devices, so we simulate FIFO-is-full interrupt here.
		 * Ralink recommends to drain FIFO stats every 100 ms, but 16 slots
		 * quickly get fulled. To prevent overflow, increment a counter on
		 * every FIFO stat request, so we know how many slots are left.
		 * We do this only in HOSTAP or multiple vap mode since FIFO stats
		 * are used only in those modes.
		 * We just drain stats. AMRR gets updated every 1 sec by
		 * run_ratectl_cb() via callout.
		 * Call it early. Otherwise overflow.
		 */
		if (sc->fifo_cnt++ == 10) {
			/*
			 * With multiple vaps or if_bridge, if_start() is called
			 * with a non-sleepable lock, tcpinp. So, need to defer.
			 */
			uint32_t i = RUN_CMDQ_GET(&sc->cmdq_store);
			RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "cmdq_store=%d\n", i);
			sc->cmdq[i].func = run_drain_fifo;
			sc->cmdq[i].arg0 = sc;
			ieee80211_runtask(ic, &sc->cmdq_task);
		}
	}

        STAILQ_INSERT_TAIL(&sc->sc_epq[qid].tx_qh, data, next);

	usbd_transfer_start(sc->sc_xfer[qid]);

	RUN_DPRINTF(sc, RUN_DEBUG_XMIT,
	    "sending data frame len=%d rate=%d qid=%d\n",
	    m->m_pkthdr.len + (int)(sizeof(struct rt2870_txd) +
	    sizeof(struct rt2860_txwi)), rt2860_rates[ridx].rate, qid);

	return (0);
}

static int
run_tx_mgt(struct run_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct run_node *rn = RUN_NODE(ni);
	struct run_tx_data *data;
	struct ieee80211_frame *wh;
	struct rt2870_txd *txd;
	struct rt2860_txwi *txwi;
	uint16_t dur;
	uint8_t ridx = rn->mgt_ridx;
	uint8_t xflags = 0;
	uint8_t wflags = 0;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	wh = mtod(m, struct ieee80211_frame *);

	/* tell hardware to add timestamp for probe responses */
	if ((wh->i_fc[0] &
	    (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
	    (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP))
		wflags |= RT2860_TX_TS;
	else if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		xflags |= RT2860_TX_ACK;

		dur = ieee80211_ack_duration(ic->ic_rt, rt2860_rates[ridx].rate, 
		    ic->ic_flags & IEEE80211_F_SHPREAMBLE);
		USETW(wh->i_dur, dur);
	}

	if (sc->sc_epq[0].tx_nfree == 0)
		/* let caller free mbuf */
		return (EIO);
	data = STAILQ_FIRST(&sc->sc_epq[0].tx_fh);
	STAILQ_REMOVE_HEAD(&sc->sc_epq[0].tx_fh, next);
	sc->sc_epq[0].tx_nfree--;

	txd = (struct rt2870_txd *)&data->desc;
	txd->flags = RT2860_TX_QSEL_EDCA;
	txwi = (struct rt2860_txwi *)(txd + 1);
	txwi->wcid = 0xff;
	txwi->flags = wflags;
	txwi->xflags = xflags;
	txwi->txop = 0;	/* clear leftover garbage bits */

	data->m = m;
	data->ni = ni;
	data->ridx = ridx;

	run_set_tx_desc(sc, data);

	RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "sending mgt frame len=%d rate=%d\n",
	    m->m_pkthdr.len + (int)(sizeof(struct rt2870_txd) +
	    sizeof(struct rt2860_txwi)), rt2860_rates[ridx].rate);

	STAILQ_INSERT_TAIL(&sc->sc_epq[0].tx_qh, data, next);

	usbd_transfer_start(sc->sc_xfer[0]);

	return (0);
}

static int
run_sendprot(struct run_softc *sc,
    const struct mbuf *m, struct ieee80211_node *ni, int prot, int rate)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct run_tx_data *data;
	struct rt2870_txd *txd;
	struct rt2860_txwi *txwi;
	struct mbuf *mprot;
	int ridx;
	int protrate;
	uint8_t wflags = 0;
	uint8_t xflags = 0;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	/* check that there are free slots before allocating the mbuf */
	if (sc->sc_epq[0].tx_nfree == 0)
		/* let caller free mbuf */
		return (ENOBUFS);

	mprot = ieee80211_alloc_prot(ni, m, rate, prot);
	if (mprot == NULL) {
		if_inc_counter(ni->ni_vap->iv_ifp, IFCOUNTER_OERRORS, 1);
		RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "could not allocate mbuf\n");
		return (ENOBUFS);
	}

	protrate = ieee80211_ctl_rate(ic->ic_rt, rate);
	wflags = RT2860_TX_FRAG;
	xflags = 0;
	if (prot == IEEE80211_PROT_RTSCTS)
		xflags |= RT2860_TX_ACK;

        data = STAILQ_FIRST(&sc->sc_epq[0].tx_fh);
        STAILQ_REMOVE_HEAD(&sc->sc_epq[0].tx_fh, next);
        sc->sc_epq[0].tx_nfree--;

	txd = (struct rt2870_txd *)&data->desc;
	txd->flags = RT2860_TX_QSEL_EDCA;
	txwi = (struct rt2860_txwi *)(txd + 1);
	txwi->wcid = 0xff;
	txwi->flags = wflags;
	txwi->xflags = xflags;
	txwi->txop = 0;	/* clear leftover garbage bits */

	data->m = mprot;
	data->ni = ieee80211_ref_node(ni);

	for (ridx = 0; ridx < RT2860_RIDX_MAX; ridx++)
		if (rt2860_rates[ridx].rate == protrate)
			break;
	data->ridx = ridx;

	run_set_tx_desc(sc, data);

        RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "sending prot len=%u rate=%u\n",
            m->m_pkthdr.len, rate);

        STAILQ_INSERT_TAIL(&sc->sc_epq[0].tx_qh, data, next);

	usbd_transfer_start(sc->sc_xfer[0]);

	return (0);
}

static int
run_tx_param(struct run_softc *sc, struct mbuf *m, struct ieee80211_node *ni,
    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct run_tx_data *data;
	struct rt2870_txd *txd;
	struct rt2860_txwi *txwi;
	uint8_t ridx;
	uint8_t rate;
	uint8_t opflags = 0;
	uint8_t xflags = 0;
	int error;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	KASSERT(params != NULL, ("no raw xmit params"));

	rate = params->ibp_rate0;
	if (!ieee80211_isratevalid(ic->ic_rt, rate)) {
		/* let caller free mbuf */
		return (EINVAL);
	}

	if ((params->ibp_flags & IEEE80211_BPF_NOACK) == 0)
		xflags |= RT2860_TX_ACK;
	if (params->ibp_flags & (IEEE80211_BPF_RTS|IEEE80211_BPF_CTS)) {
		error = run_sendprot(sc, m, ni,
		    params->ibp_flags & IEEE80211_BPF_RTS ?
			IEEE80211_PROT_RTSCTS : IEEE80211_PROT_CTSONLY,
		    rate);
		if (error) {
			/* let caller free mbuf */
			return error;
		}
		opflags |= /*XXX RT2573_TX_LONG_RETRY |*/ RT2860_TX_TXOP_SIFS;
	}

	if (sc->sc_epq[0].tx_nfree == 0) {
		/* let caller free mbuf */
		RUN_DPRINTF(sc, RUN_DEBUG_XMIT,
		    "sending raw frame, but tx ring is full\n");
		return (EIO);
	}
        data = STAILQ_FIRST(&sc->sc_epq[0].tx_fh);
        STAILQ_REMOVE_HEAD(&sc->sc_epq[0].tx_fh, next);
        sc->sc_epq[0].tx_nfree--;

	txd = (struct rt2870_txd *)&data->desc;
	txd->flags = RT2860_TX_QSEL_EDCA;
	txwi = (struct rt2860_txwi *)(txd + 1);
	txwi->wcid = 0xff;
	txwi->xflags = xflags;
	txwi->txop = opflags;
	txwi->flags = 0;	/* clear leftover garbage bits */

        data->m = m;
        data->ni = ni;
	for (ridx = 0; ridx < RT2860_RIDX_MAX; ridx++)
		if (rt2860_rates[ridx].rate == rate)
			break;
	data->ridx = ridx;

        run_set_tx_desc(sc, data);

        RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "sending raw frame len=%u rate=%u\n",
            m->m_pkthdr.len, rate);

        STAILQ_INSERT_TAIL(&sc->sc_epq[0].tx_qh, data, next);

	usbd_transfer_start(sc->sc_xfer[0]);

        return (0);
}

static int
run_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_bpf_params *params)
{
	struct run_softc *sc = ni->ni_ic->ic_softc;
	int error = 0;
 
	RUN_LOCK(sc);

	/* prevent management frames from being sent if we're not ready */
	if (!(sc->sc_flags & RUN_RUNNING)) {
		error = ENETDOWN;
		goto done;
	}

	if (params == NULL) {
		/* tx mgt packet */
		if ((error = run_tx_mgt(sc, m, ni)) != 0) {
			RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "mgt tx failed\n");
			goto done;
		}
	} else {
		/* tx raw packet with param */
		if ((error = run_tx_param(sc, m, ni, params)) != 0) {
			RUN_DPRINTF(sc, RUN_DEBUG_XMIT, "tx with param failed\n");
			goto done;
		}
	}

done:
	RUN_UNLOCK(sc);

	if (error != 0) {
		if(m != NULL)
			m_freem(m);
	}

	return (error);
}

static int
run_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct run_softc *sc = ic->ic_softc;
	int error;

	RUN_LOCK(sc);
	if ((sc->sc_flags & RUN_RUNNING) == 0) {
		RUN_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		RUN_UNLOCK(sc);
		return (error);
	}
	run_start(sc);
	RUN_UNLOCK(sc);

	return (0);
}

static void
run_start(struct run_softc *sc)
{
	struct ieee80211_node *ni;
	struct mbuf *m;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	if ((sc->sc_flags & RUN_RUNNING) == 0)
		return;

	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		if (run_tx(sc, m, ni) != 0) {
			mbufq_prepend(&sc->sc_snd, m);
			break;
		}
	}
}

static void
run_parent(struct ieee80211com *ic)
{
	struct run_softc *sc = ic->ic_softc;
	int startall = 0;

	RUN_LOCK(sc);
	if (sc->sc_detached) {
		RUN_UNLOCK(sc);
		return;
	}

	if (ic->ic_nrunning > 0) {
		if (!(sc->sc_flags & RUN_RUNNING)) {
			startall = 1;
			run_init_locked(sc);
		} else
			run_update_promisc_locked(sc);
	} else if ((sc->sc_flags & RUN_RUNNING) && sc->rvp_cnt <= 1)
		run_stop(sc);
	RUN_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

static void
run_iq_calib(struct run_softc *sc, u_int chan)
{
	uint16_t val;

	/* Tx0 IQ gain. */
	run_bbp_write(sc, 158, 0x2c);
	if (chan <= 14)
		run_efuse_read(sc, RT5390_EEPROM_IQ_GAIN_CAL_TX0_2GHZ, &val, 1);
	else if (chan <= 64) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_GAIN_CAL_TX0_CH36_TO_CH64_5GHZ,
		    &val, 1);
	} else if (chan <= 138) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_GAIN_CAL_TX0_CH100_TO_CH138_5GHZ,
		    &val, 1);
	} else if (chan <= 165) {
		run_efuse_read(sc,
	    RT5390_EEPROM_IQ_GAIN_CAL_TX0_CH140_TO_CH165_5GHZ,
		    &val, 1);
	} else
		val = 0;
	run_bbp_write(sc, 159, val);

	/* Tx0 IQ phase. */
	run_bbp_write(sc, 158, 0x2d);
	if (chan <= 14) {
		run_efuse_read(sc, RT5390_EEPROM_IQ_PHASE_CAL_TX0_2GHZ,
		    &val, 1);
	} else if (chan <= 64) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX0_CH36_TO_CH64_5GHZ,
		    &val, 1);
	} else if (chan <= 138) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX0_CH100_TO_CH138_5GHZ,
		    &val, 1);
	} else if (chan <= 165) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX0_CH140_TO_CH165_5GHZ,
		    &val, 1);
	} else
		val = 0;
	run_bbp_write(sc, 159, val);

	/* Tx1 IQ gain. */
	run_bbp_write(sc, 158, 0x4a);
	if (chan <= 14) {
		run_efuse_read(sc, RT5390_EEPROM_IQ_GAIN_CAL_TX1_2GHZ,
		    &val, 1);
	} else if (chan <= 64) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_GAIN_CAL_TX1_CH36_TO_CH64_5GHZ,
		    &val, 1);
	} else if (chan <= 138) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_GAIN_CAL_TX1_CH100_TO_CH138_5GHZ,
		    &val, 1);
	} else if (chan <= 165) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_GAIN_CAL_TX1_CH140_TO_CH165_5GHZ,
		    &val, 1);
	} else
		val = 0;
	run_bbp_write(sc, 159, val);

	/* Tx1 IQ phase. */
	run_bbp_write(sc, 158, 0x4b);
	if (chan <= 14) {
		run_efuse_read(sc, RT5390_EEPROM_IQ_PHASE_CAL_TX1_2GHZ,
		    &val, 1);
	} else if (chan <= 64) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX1_CH36_TO_CH64_5GHZ,
		    &val, 1);
	} else if (chan <= 138) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX1_CH100_TO_CH138_5GHZ,
		    &val, 1);
	} else if (chan <= 165) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX1_CH140_TO_CH165_5GHZ,
		    &val, 1);
	} else
		val = 0;
	run_bbp_write(sc, 159, val);

	/* RF IQ compensation control. */
	run_bbp_write(sc, 158, 0x04);
	run_efuse_read(sc, RT5390_EEPROM_RF_IQ_COMPENSATION_CTL,
	    &val, 1);
	run_bbp_write(sc, 159, val);

	/* RF IQ imbalance compensation control. */
	run_bbp_write(sc, 158, 0x03);
	run_efuse_read(sc,
	    RT5390_EEPROM_RF_IQ_IMBALANCE_COMPENSATION_CTL, &val, 1);
	run_bbp_write(sc, 159, val);
}

static void
run_set_agc(struct run_softc *sc, uint8_t agc)
{
	uint8_t bbp;

	if (sc->mac_ver == 0x3572) {
		run_bbp_read(sc, 27, &bbp);
		bbp &= ~(0x3 << 5);
		run_bbp_write(sc, 27, bbp | 0 << 5);	/* select Rx0 */
		run_bbp_write(sc, 66, agc);
		run_bbp_write(sc, 27, bbp | 1 << 5);	/* select Rx1 */
		run_bbp_write(sc, 66, agc);
	} else
		run_bbp_write(sc, 66, agc);
}

static void
run_select_chan_group(struct run_softc *sc, int group)
{
	uint32_t tmp;
	uint8_t agc;

	run_bbp_write(sc, 62, 0x37 - sc->lna[group]);
	run_bbp_write(sc, 63, 0x37 - sc->lna[group]);
	run_bbp_write(sc, 64, 0x37 - sc->lna[group]);
	if (sc->mac_ver < 0x3572)
		run_bbp_write(sc, 86, 0x00);

	if (sc->mac_ver == 0x3593) {
		run_bbp_write(sc, 77, 0x98);
		run_bbp_write(sc, 83, (group == 0) ? 0x8a : 0x9a);
	}

	if (group == 0) {
		if (sc->ext_2ghz_lna) {
			if (sc->mac_ver >= 0x5390)
				run_bbp_write(sc, 75, 0x52);
			else {
				run_bbp_write(sc, 82, 0x62);
				run_bbp_write(sc, 75, 0x46);
			}
		} else {
			if (sc->mac_ver == 0x5592) {
				run_bbp_write(sc, 79, 0x1c);
				run_bbp_write(sc, 80, 0x0e);
				run_bbp_write(sc, 81, 0x3a);
				run_bbp_write(sc, 82, 0x62);

				run_bbp_write(sc, 195, 0x80);
				run_bbp_write(sc, 196, 0xe0);
				run_bbp_write(sc, 195, 0x81);
				run_bbp_write(sc, 196, 0x1f);
				run_bbp_write(sc, 195, 0x82);
				run_bbp_write(sc, 196, 0x38);
				run_bbp_write(sc, 195, 0x83);
				run_bbp_write(sc, 196, 0x32);
				run_bbp_write(sc, 195, 0x85);
				run_bbp_write(sc, 196, 0x28);
				run_bbp_write(sc, 195, 0x86);
				run_bbp_write(sc, 196, 0x19);
			} else if (sc->mac_ver >= 0x5390)
				run_bbp_write(sc, 75, 0x50);
			else {
				run_bbp_write(sc, 82,
				    (sc->mac_ver == 0x3593) ? 0x62 : 0x84);
				run_bbp_write(sc, 75, 0x50);
			}
		}
	} else {
		if (sc->mac_ver == 0x5592) {
			run_bbp_write(sc, 79, 0x18);
			run_bbp_write(sc, 80, 0x08);
			run_bbp_write(sc, 81, 0x38);
			run_bbp_write(sc, 82, 0x92);

			run_bbp_write(sc, 195, 0x80);
			run_bbp_write(sc, 196, 0xf0);
			run_bbp_write(sc, 195, 0x81);
			run_bbp_write(sc, 196, 0x1e);
			run_bbp_write(sc, 195, 0x82);
			run_bbp_write(sc, 196, 0x28);
			run_bbp_write(sc, 195, 0x83);
			run_bbp_write(sc, 196, 0x20);
			run_bbp_write(sc, 195, 0x85);
			run_bbp_write(sc, 196, 0x7f);
			run_bbp_write(sc, 195, 0x86);
			run_bbp_write(sc, 196, 0x7f);
		} else if (sc->mac_ver == 0x3572)
			run_bbp_write(sc, 82, 0x94);
		else
			run_bbp_write(sc, 82,
			    (sc->mac_ver == 0x3593) ? 0x82 : 0xf2);
		if (sc->ext_5ghz_lna)
			run_bbp_write(sc, 75, 0x46);
		else 
			run_bbp_write(sc, 75, 0x50);
	}

	run_read(sc, RT2860_TX_BAND_CFG, &tmp);
	tmp &= ~(RT2860_5G_BAND_SEL_N | RT2860_5G_BAND_SEL_P);
	tmp |= (group == 0) ? RT2860_5G_BAND_SEL_N : RT2860_5G_BAND_SEL_P;
	run_write(sc, RT2860_TX_BAND_CFG, tmp);

	/* enable appropriate Power Amplifiers and Low Noise Amplifiers */
	tmp = RT2860_RFTR_EN | RT2860_TRSW_EN | RT2860_LNA_PE0_EN;
	if (sc->mac_ver == 0x3593)
		tmp |= 1 << 29 | 1 << 28;
	if (sc->nrxchains > 1)
		tmp |= RT2860_LNA_PE1_EN;
	if (group == 0) {	/* 2GHz */
		tmp |= RT2860_PA_PE_G0_EN;
		if (sc->ntxchains > 1)
			tmp |= RT2860_PA_PE_G1_EN;
		if (sc->mac_ver == 0x3593) {
			if (sc->ntxchains > 2)
				tmp |= 1 << 25;
		}
	} else {		/* 5GHz */
		tmp |= RT2860_PA_PE_A0_EN;
		if (sc->ntxchains > 1)
			tmp |= RT2860_PA_PE_A1_EN;
	}
	if (sc->mac_ver == 0x3572) {
		run_rt3070_rf_write(sc, 8, 0x00);
		run_write(sc, RT2860_TX_PIN_CFG, tmp);
		run_rt3070_rf_write(sc, 8, 0x80);
	} else
		run_write(sc, RT2860_TX_PIN_CFG, tmp);

	if (sc->mac_ver == 0x5592) {
		run_bbp_write(sc, 195, 0x8d);
		run_bbp_write(sc, 196, 0x1a);
	}

	if (sc->mac_ver == 0x3593) {
		run_read(sc, RT2860_GPIO_CTRL, &tmp);
		tmp &= ~0x01010000;
		if (group == 0)
			tmp |= 0x00010000;
		tmp = (tmp & ~0x00009090) | 0x00000090;
		run_write(sc, RT2860_GPIO_CTRL, tmp);
	}

	/* set initial AGC value */
	if (group == 0) {	/* 2GHz band */
		if (sc->mac_ver >= 0x3070)
			agc = 0x1c + sc->lna[0] * 2;
		else
			agc = 0x2e + sc->lna[0];
	} else {		/* 5GHz band */
		if (sc->mac_ver == 0x5592)
			agc = 0x24 + sc->lna[group] * 2;
		else if (sc->mac_ver == 0x3572 || sc->mac_ver == 0x3593)
			agc = 0x22 + (sc->lna[group] * 5) / 3;
		else
			agc = 0x32 + (sc->lna[group] * 5) / 3;
	}
	run_set_agc(sc, agc);
}

static void
run_rt2870_set_chan(struct run_softc *sc, u_int chan)
{
	const struct rfprog *rfprog = rt2860_rf2850;
	uint32_t r2, r3, r4;
	int8_t txpow1, txpow2;
	int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rfprog[i].chan != chan; i++);

	r2 = rfprog[i].r2;
	if (sc->ntxchains == 1)
		r2 |= 1 << 14;		/* 1T: disable Tx chain 2 */
	if (sc->nrxchains == 1)
		r2 |= 1 << 17 | 1 << 6;	/* 1R: disable Rx chains 2 & 3 */
	else if (sc->nrxchains == 2)
		r2 |= 1 << 6;		/* 2R: disable Rx chain 3 */

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];
	txpow2 = sc->txpow2[i];

	/* Initialize RF R3 and R4. */
	r3 = rfprog[i].r3 & 0xffffc1ff;
	r4 = (rfprog[i].r4 & ~(0x001f87c0)) | (sc->freq << 15);
	if (chan > 14) {
		if (txpow1 >= 0) {
			txpow1 = (txpow1 > 0xf) ? (0xf) : (txpow1);
			r3 |= (txpow1 << 10) | (1 << 9);
		} else {
			txpow1 += 7;

			/* txpow1 is not possible larger than 15. */
			r3 |= (txpow1 << 10);
		}
		if (txpow2 >= 0) {
			txpow2 = (txpow2 > 0xf) ? (0xf) : (txpow2);
			r4 |= (txpow2 << 7) | (1 << 6);
		} else {
			txpow2 += 7;
			r4 |= (txpow2 << 7);
		}
	} else {
		/* Set Tx0 power. */
		r3 |= (txpow1 << 9);

		/* Set frequency offset and Tx1 power. */
		r4 |= (txpow2 << 6);
	}

	run_rt2870_rf_write(sc, rfprog[i].r1);
	run_rt2870_rf_write(sc, r2);
	run_rt2870_rf_write(sc, r3 & ~(1 << 2));
	run_rt2870_rf_write(sc, r4);

	run_delay(sc, 10);

	run_rt2870_rf_write(sc, rfprog[i].r1);
	run_rt2870_rf_write(sc, r2);
	run_rt2870_rf_write(sc, r3 | (1 << 2));
	run_rt2870_rf_write(sc, r4);

	run_delay(sc, 10);

	run_rt2870_rf_write(sc, rfprog[i].r1);
	run_rt2870_rf_write(sc, r2);
	run_rt2870_rf_write(sc, r3 & ~(1 << 2));
	run_rt2870_rf_write(sc, r4);
}

static void
run_rt3070_set_chan(struct run_softc *sc, u_int chan)
{
	int8_t txpow1, txpow2;
	uint8_t rf;
	int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++);

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];
	txpow2 = sc->txpow2[i];

	run_rt3070_rf_write(sc, 2, rt3070_freqs[i].n);

	/* RT3370/RT3390: RF R3 [7:4] is not reserved bits. */
	run_rt3070_rf_read(sc, 3, &rf);
	rf = (rf & ~0x0f) | rt3070_freqs[i].k;
	run_rt3070_rf_write(sc, 3, rf);

	run_rt3070_rf_read(sc, 6, &rf);
	rf = (rf & ~0x03) | rt3070_freqs[i].r;
	run_rt3070_rf_write(sc, 6, rf);

	/* set Tx0 power */
	run_rt3070_rf_read(sc, 12, &rf);
	rf = (rf & ~0x1f) | txpow1;
	run_rt3070_rf_write(sc, 12, rf);

	/* set Tx1 power */
	run_rt3070_rf_read(sc, 13, &rf);
	rf = (rf & ~0x1f) | txpow2;
	run_rt3070_rf_write(sc, 13, rf);

	run_rt3070_rf_read(sc, 1, &rf);
	rf &= ~0xfc;
	if (sc->ntxchains == 1)
		rf |= 1 << 7 | 1 << 5;	/* 1T: disable Tx chains 2 & 3 */
	else if (sc->ntxchains == 2)
		rf |= 1 << 7;		/* 2T: disable Tx chain 3 */
	if (sc->nrxchains == 1)
		rf |= 1 << 6 | 1 << 4;	/* 1R: disable Rx chains 2 & 3 */
	else if (sc->nrxchains == 2)
		rf |= 1 << 6;		/* 2R: disable Rx chain 3 */
	run_rt3070_rf_write(sc, 1, rf);

	/* set RF offset */
	run_rt3070_rf_read(sc, 23, &rf);
	rf = (rf & ~0x7f) | sc->freq;
	run_rt3070_rf_write(sc, 23, rf);

	/* program RF filter */
	run_rt3070_rf_read(sc, 24, &rf);	/* Tx */
	rf = (rf & ~0x3f) | sc->rf24_20mhz;
	run_rt3070_rf_write(sc, 24, rf);
	run_rt3070_rf_read(sc, 31, &rf);	/* Rx */
	rf = (rf & ~0x3f) | sc->rf24_20mhz;
	run_rt3070_rf_write(sc, 31, rf);

	/* enable RF tuning */
	run_rt3070_rf_read(sc, 7, &rf);
	run_rt3070_rf_write(sc, 7, rf | 0x01);
}

static void
run_rt3572_set_chan(struct run_softc *sc, u_int chan)
{
	int8_t txpow1, txpow2;
	uint32_t tmp;
	uint8_t rf;
	int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++);

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];
	txpow2 = sc->txpow2[i];

	if (chan <= 14) {
		run_bbp_write(sc, 25, sc->bbp25);
		run_bbp_write(sc, 26, sc->bbp26);
	} else {
		/* enable IQ phase correction */
		run_bbp_write(sc, 25, 0x09);
		run_bbp_write(sc, 26, 0xff);
	}

	run_rt3070_rf_write(sc, 2, rt3070_freqs[i].n);
	run_rt3070_rf_write(sc, 3, rt3070_freqs[i].k);
	run_rt3070_rf_read(sc, 6, &rf);
	rf  = (rf & ~0x0f) | rt3070_freqs[i].r;
	rf |= (chan <= 14) ? 0x08 : 0x04;
	run_rt3070_rf_write(sc, 6, rf);

	/* set PLL mode */
	run_rt3070_rf_read(sc, 5, &rf);
	rf &= ~(0x08 | 0x04);
	rf |= (chan <= 14) ? 0x04 : 0x08;
	run_rt3070_rf_write(sc, 5, rf);

	/* set Tx power for chain 0 */
	if (chan <= 14)
		rf = 0x60 | txpow1;
	else
		rf = 0xe0 | (txpow1 & 0xc) << 1 | (txpow1 & 0x3);
	run_rt3070_rf_write(sc, 12, rf);

	/* set Tx power for chain 1 */
	if (chan <= 14)
		rf = 0x60 | txpow2;
	else
		rf = 0xe0 | (txpow2 & 0xc) << 1 | (txpow2 & 0x3);
	run_rt3070_rf_write(sc, 13, rf);

	/* set Tx/Rx streams */
	run_rt3070_rf_read(sc, 1, &rf);
	rf &= ~0xfc;
	if (sc->ntxchains == 1)
		rf |= 1 << 7 | 1 << 5;  /* 1T: disable Tx chains 2 & 3 */
	else if (sc->ntxchains == 2)
		rf |= 1 << 7;           /* 2T: disable Tx chain 3 */
	if (sc->nrxchains == 1)
		rf |= 1 << 6 | 1 << 4;  /* 1R: disable Rx chains 2 & 3 */
	else if (sc->nrxchains == 2)
		rf |= 1 << 6;           /* 2R: disable Rx chain 3 */
	run_rt3070_rf_write(sc, 1, rf);

	/* set RF offset */
	run_rt3070_rf_read(sc, 23, &rf);
	rf = (rf & ~0x7f) | sc->freq;
	run_rt3070_rf_write(sc, 23, rf);

	/* program RF filter */
	rf = sc->rf24_20mhz;
	run_rt3070_rf_write(sc, 24, rf);	/* Tx */
	run_rt3070_rf_write(sc, 31, rf);	/* Rx */

	/* enable RF tuning */
	run_rt3070_rf_read(sc, 7, &rf);
	rf = (chan <= 14) ? 0xd8 : ((rf & ~0xc8) | 0x14);
	run_rt3070_rf_write(sc, 7, rf);

	/* TSSI */
	rf = (chan <= 14) ? 0xc3 : 0xc0;
	run_rt3070_rf_write(sc, 9, rf);

	/* set loop filter 1 */
	run_rt3070_rf_write(sc, 10, 0xf1);
	/* set loop filter 2 */
	run_rt3070_rf_write(sc, 11, (chan <= 14) ? 0xb9 : 0x00);

	/* set tx_mx2_ic */
	run_rt3070_rf_write(sc, 15, (chan <= 14) ? 0x53 : 0x43);
	/* set tx_mx1_ic */
	if (chan <= 14)
		rf = 0x48 | sc->txmixgain_2ghz;
	else
		rf = 0x78 | sc->txmixgain_5ghz;
	run_rt3070_rf_write(sc, 16, rf);

	/* set tx_lo1 */
	run_rt3070_rf_write(sc, 17, 0x23);
	/* set tx_lo2 */
	if (chan <= 14)
		rf = 0x93;
	else if (chan <= 64)
		rf = 0xb7;
	else if (chan <= 128)
		rf = 0x74;
	else
		rf = 0x72;
	run_rt3070_rf_write(sc, 19, rf);

	/* set rx_lo1 */
	if (chan <= 14)
		rf = 0xb3;
	else if (chan <= 64)
		rf = 0xf6;
	else if (chan <= 128)
		rf = 0xf4;
	else
		rf = 0xf3;
	run_rt3070_rf_write(sc, 20, rf);

	/* set pfd_delay */
	if (chan <= 14)
		rf = 0x15;
	else if (chan <= 64)
		rf = 0x3d;
	else
		rf = 0x01;
	run_rt3070_rf_write(sc, 25, rf);

	/* set rx_lo2 */
	run_rt3070_rf_write(sc, 26, (chan <= 14) ? 0x85 : 0x87);
	/* set ldo_rf_vc */
	run_rt3070_rf_write(sc, 27, (chan <= 14) ? 0x00 : 0x01);
	/* set drv_cc */
	run_rt3070_rf_write(sc, 29, (chan <= 14) ? 0x9b : 0x9f);

	run_read(sc, RT2860_GPIO_CTRL, &tmp);
	tmp &= ~0x8080;
	if (chan <= 14)
		tmp |= 0x80;
	run_write(sc, RT2860_GPIO_CTRL, tmp);

	/* enable RF tuning */
	run_rt3070_rf_read(sc, 7, &rf);
	run_rt3070_rf_write(sc, 7, rf | 0x01);

	run_delay(sc, 2);
}

static void
run_rt3593_set_chan(struct run_softc *sc, u_int chan)
{
	int8_t txpow1, txpow2, txpow3;
	uint8_t h20mhz, rf;
	int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++);

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];
	txpow2 = sc->txpow2[i];
	txpow3 = (sc->ntxchains == 3) ? sc->txpow3[i] : 0;

	if (chan <= 14) {
		run_bbp_write(sc, 25, sc->bbp25);
		run_bbp_write(sc, 26, sc->bbp26);
	} else {
		/* Enable IQ phase correction. */
		run_bbp_write(sc, 25, 0x09);
		run_bbp_write(sc, 26, 0xff);
	}

	run_rt3070_rf_write(sc, 8, rt3070_freqs[i].n);
	run_rt3070_rf_write(sc, 9, rt3070_freqs[i].k & 0x0f);
	run_rt3070_rf_read(sc, 11, &rf);
	rf = (rf & ~0x03) | (rt3070_freqs[i].r & 0x03);
	run_rt3070_rf_write(sc, 11, rf);

	/* Set pll_idoh. */
	run_rt3070_rf_read(sc, 11, &rf);
	rf &= ~0x4c;
	rf |= (chan <= 14) ? 0x44 : 0x48;
	run_rt3070_rf_write(sc, 11, rf);

	if (chan <= 14)
		rf = txpow1 & 0x1f;
	else
		rf = 0x40 | ((txpow1 & 0x18) << 1) | (txpow1 & 0x07);
	run_rt3070_rf_write(sc, 53, rf);

	if (chan <= 14)
		rf = txpow2 & 0x1f;
	else
		rf = 0x40 | ((txpow2 & 0x18) << 1) | (txpow2 & 0x07);
	run_rt3070_rf_write(sc, 55, rf);

	if (chan <= 14)
		rf = txpow3 & 0x1f;
	else
		rf = 0x40 | ((txpow3 & 0x18) << 1) | (txpow3 & 0x07);
	run_rt3070_rf_write(sc, 54, rf);

	rf = RT3070_RF_BLOCK | RT3070_PLL_PD;
	if (sc->ntxchains == 3)
		rf |= RT3070_TX0_PD | RT3070_TX1_PD | RT3070_TX2_PD;
	else
		rf |= RT3070_TX0_PD | RT3070_TX1_PD;
	rf |= RT3070_RX0_PD | RT3070_RX1_PD | RT3070_RX2_PD;
	run_rt3070_rf_write(sc, 1, rf);

	run_adjust_freq_offset(sc);

	run_rt3070_rf_write(sc, 31, (chan <= 14) ? 0xa0 : 0x80);

	h20mhz = (sc->rf24_20mhz & 0x20) >> 5; 
	run_rt3070_rf_read(sc, 30, &rf);
	rf = (rf & ~0x06) | (h20mhz << 1) | (h20mhz << 2);
	run_rt3070_rf_write(sc, 30, rf);

	run_rt3070_rf_read(sc, 36, &rf);
	if (chan <= 14)
		rf |= 0x80;
	else
		rf &= ~0x80;
	run_rt3070_rf_write(sc, 36, rf);

	/* Set vcolo_bs. */
	run_rt3070_rf_write(sc, 34, (chan <= 14) ? 0x3c : 0x20);
	/* Set pfd_delay. */
	run_rt3070_rf_write(sc, 12, (chan <= 14) ? 0x1a : 0x12);

	/* Set vco bias current control. */
	run_rt3070_rf_read(sc, 6, &rf);
	rf &= ~0xc0;
	if (chan <= 14)
		rf |= 0x40;
	else if (chan <= 128)
		rf |= 0x80;
	else
		rf |= 0x40;
	run_rt3070_rf_write(sc, 6, rf);
		
	run_rt3070_rf_read(sc, 30, &rf);
	rf = (rf & ~0x18) | 0x10;
	run_rt3070_rf_write(sc, 30, rf);

	run_rt3070_rf_write(sc, 10, (chan <= 14) ? 0xd3 : 0xd8);
	run_rt3070_rf_write(sc, 13, (chan <= 14) ? 0x12 : 0x23);

	run_rt3070_rf_read(sc, 51, &rf);
	rf = (rf & ~0x03) | 0x01;
	run_rt3070_rf_write(sc, 51, rf);
	/* Set tx_mx1_cc. */
	run_rt3070_rf_read(sc, 51, &rf);
	rf &= ~0x1c;
	rf |= (chan <= 14) ? 0x14 : 0x10;
	run_rt3070_rf_write(sc, 51, rf);
	/* Set tx_mx1_ic. */
	run_rt3070_rf_read(sc, 51, &rf);
	rf &= ~0xe0;
	rf |= (chan <= 14) ? 0x60 : 0x40;
	run_rt3070_rf_write(sc, 51, rf);
	/* Set tx_lo1_ic. */
	run_rt3070_rf_read(sc, 49, &rf);
	rf &= ~0x1c;
	rf |= (chan <= 14) ? 0x0c : 0x08;
	run_rt3070_rf_write(sc, 49, rf);
	/* Set tx_lo1_en. */
	run_rt3070_rf_read(sc, 50, &rf);
	run_rt3070_rf_write(sc, 50, rf & ~0x20);
	/* Set drv_cc. */
	run_rt3070_rf_read(sc, 57, &rf);
	rf &= ~0xfc;
	rf |= (chan <= 14) ?  0x6c : 0x3c;
	run_rt3070_rf_write(sc, 57, rf);
	/* Set rx_mix1_ic, rxa_lnactr, lna_vc, lna_inbias_en and lna_en. */
	run_rt3070_rf_write(sc, 44, (chan <= 14) ? 0x93 : 0x9b);
	/* Set drv_gnd_a, tx_vga_cc_a and tx_mx2_gain. */
	run_rt3070_rf_write(sc, 52, (chan <= 14) ? 0x45 : 0x05);
	/* Enable VCO calibration. */
	run_rt3070_rf_read(sc, 3, &rf);
	rf &= ~RT5390_VCOCAL;
	rf |= (chan <= 14) ? RT5390_VCOCAL : 0xbe;
	run_rt3070_rf_write(sc, 3, rf);

	if (chan <= 14)
		rf = 0x23;
	else if (chan <= 64)
		rf = 0x36;
	else if (chan <= 128)
		rf = 0x32;
	else
		rf = 0x30;
	run_rt3070_rf_write(sc, 39, rf);
	if (chan <= 14)
		rf = 0xbb;
	else if (chan <= 64)
		rf = 0xeb;
	else if (chan <= 128)
		rf = 0xb3;
	else
		rf = 0x9b;
	run_rt3070_rf_write(sc, 45, rf);

	/* Set FEQ/AEQ control. */
	run_bbp_write(sc, 105, 0x34);
}

static void
run_rt5390_set_chan(struct run_softc *sc, u_int chan)
{
	int8_t txpow1, txpow2;
	uint8_t rf;
	int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++);

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];
	txpow2 = sc->txpow2[i];

	run_rt3070_rf_write(sc, 8, rt3070_freqs[i].n);
	run_rt3070_rf_write(sc, 9, rt3070_freqs[i].k & 0x0f);
	run_rt3070_rf_read(sc, 11, &rf);
	rf = (rf & ~0x03) | (rt3070_freqs[i].r & 0x03);
	run_rt3070_rf_write(sc, 11, rf);

	run_rt3070_rf_read(sc, 49, &rf);
	rf = (rf & ~0x3f) | (txpow1 & 0x3f);
	/* The valid range of the RF R49 is 0x00 to 0x27. */
	if ((rf & 0x3f) > 0x27)
		rf = (rf & ~0x3f) | 0x27;
	run_rt3070_rf_write(sc, 49, rf);

	if (sc->mac_ver == 0x5392) {
		run_rt3070_rf_read(sc, 50, &rf);
		rf = (rf & ~0x3f) | (txpow2 & 0x3f);
		/* The valid range of the RF R50 is 0x00 to 0x27. */
		if ((rf & 0x3f) > 0x27)
			rf = (rf & ~0x3f) | 0x27;
		run_rt3070_rf_write(sc, 50, rf);
	}

	run_rt3070_rf_read(sc, 1, &rf);
	rf |= RT3070_RF_BLOCK | RT3070_PLL_PD | RT3070_RX0_PD | RT3070_TX0_PD;
	if (sc->mac_ver == 0x5392)
		rf |= RT3070_RX1_PD | RT3070_TX1_PD;
	run_rt3070_rf_write(sc, 1, rf);

	if (sc->mac_ver != 0x5392) {
		run_rt3070_rf_read(sc, 2, &rf);
		rf |= 0x80;
		run_rt3070_rf_write(sc, 2, rf);
		run_delay(sc, 10);
		rf &= 0x7f;
		run_rt3070_rf_write(sc, 2, rf);
	}

	run_adjust_freq_offset(sc);

	if (sc->mac_ver == 0x5392) {
		/* Fix for RT5392C. */
		if (sc->mac_rev >= 0x0223) {
			if (chan <= 4)
				rf = 0x0f;
			else if (chan >= 5 && chan <= 7)
				rf = 0x0e;
			else
				rf = 0x0d;
			run_rt3070_rf_write(sc, 23, rf);

			if (chan <= 4)
				rf = 0x0c;
			else if (chan == 5)
				rf = 0x0b;
			else if (chan >= 6 && chan <= 7)
				rf = 0x0a;
			else if (chan >= 8 && chan <= 10)
				rf = 0x09;
			else
				rf = 0x08;
			run_rt3070_rf_write(sc, 59, rf);
		} else {
			if (chan <= 11)
				rf = 0x0f;
			else
				rf = 0x0b;
			run_rt3070_rf_write(sc, 59, rf);
		}
	} else {
		/* Fix for RT5390F. */
		if (sc->mac_rev >= 0x0502) {
			if (chan <= 11)
				rf = 0x43;
			else
				rf = 0x23;
			run_rt3070_rf_write(sc, 55, rf);

			if (chan <= 11)
				rf = 0x0f;
			else if (chan == 12)
				rf = 0x0d;
			else
				rf = 0x0b;
			run_rt3070_rf_write(sc, 59, rf);
		} else {
			run_rt3070_rf_write(sc, 55, 0x44);
			run_rt3070_rf_write(sc, 59, 0x8f);
		}
	}

	/* Enable VCO calibration. */
	run_rt3070_rf_read(sc, 3, &rf);
	rf |= RT5390_VCOCAL;
	run_rt3070_rf_write(sc, 3, rf);
}

static void
run_rt5592_set_chan(struct run_softc *sc, u_int chan)
{
	const struct rt5592_freqs *freqs;
	uint32_t tmp;
	uint8_t reg, rf, txpow_bound;
	int8_t txpow1, txpow2;
	int i;

	run_read(sc, RT5592_DEBUG_INDEX, &tmp);
	freqs = (tmp & RT5592_SEL_XTAL) ?
	    rt5592_freqs_40mhz : rt5592_freqs_20mhz;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++, freqs++);

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];
	txpow2 = sc->txpow2[i];

	run_read(sc, RT3070_LDO_CFG0, &tmp);
	tmp &= ~0x1c000000;
	if (chan > 14)
		tmp |= 0x14000000;
	run_write(sc, RT3070_LDO_CFG0, tmp);

	/* N setting. */
	run_rt3070_rf_write(sc, 8, freqs->n & 0xff);
	run_rt3070_rf_read(sc, 9, &rf);
	rf &= ~(1 << 4);
	rf |= ((freqs->n & 0x0100) >> 8) << 4;
	run_rt3070_rf_write(sc, 9, rf);

	/* K setting. */
	run_rt3070_rf_read(sc, 9, &rf);
	rf &= ~0x0f;
	rf |= (freqs->k & 0x0f);
	run_rt3070_rf_write(sc, 9, rf);

	/* Mode setting. */
	run_rt3070_rf_read(sc, 11, &rf);
	rf &= ~0x0c;
	rf |= ((freqs->m - 0x8) & 0x3) << 2;
	run_rt3070_rf_write(sc, 11, rf);
	run_rt3070_rf_read(sc, 9, &rf);
	rf &= ~(1 << 7);
	rf |= (((freqs->m - 0x8) & 0x4) >> 2) << 7;
	run_rt3070_rf_write(sc, 9, rf);

	/* R setting. */
	run_rt3070_rf_read(sc, 11, &rf);
	rf &= ~0x03;
	rf |= (freqs->r - 0x1);
	run_rt3070_rf_write(sc, 11, rf);

	if (chan <= 14) {
		/* Initialize RF registers for 2GHZ. */
		for (i = 0; i < nitems(rt5592_2ghz_def_rf); i++) {
			run_rt3070_rf_write(sc, rt5592_2ghz_def_rf[i].reg,
			    rt5592_2ghz_def_rf[i].val);
		}

		rf = (chan <= 10) ? 0x07 : 0x06;
		run_rt3070_rf_write(sc, 23, rf);
		run_rt3070_rf_write(sc, 59, rf);

		run_rt3070_rf_write(sc, 55, 0x43);

		/* 
		 * RF R49/R50 Tx power ALC code.
		 * G-band bit<7:6>=1:0, bit<5:0> range from 0x0 ~ 0x27.
		 */
		reg = 2;
		txpow_bound = 0x27;
	} else {
		/* Initialize RF registers for 5GHZ. */
		for (i = 0; i < nitems(rt5592_5ghz_def_rf); i++) {
			run_rt3070_rf_write(sc, rt5592_5ghz_def_rf[i].reg,
			    rt5592_5ghz_def_rf[i].val);
		}
		for (i = 0; i < nitems(rt5592_chan_5ghz); i++) {
			if (chan >= rt5592_chan_5ghz[i].firstchan &&
			    chan <= rt5592_chan_5ghz[i].lastchan) {
				run_rt3070_rf_write(sc, rt5592_chan_5ghz[i].reg,
				    rt5592_chan_5ghz[i].val);
			}
		}

		/* 
		 * RF R49/R50 Tx power ALC code.
		 * A-band bit<7:6>=1:1, bit<5:0> range from 0x0 ~ 0x2b.
		 */
		reg = 3;
		txpow_bound = 0x2b;
	}

	/* RF R49 ch0 Tx power ALC code. */
	run_rt3070_rf_read(sc, 49, &rf);
	rf &= ~0xc0;
	rf |= (reg << 6);
	rf = (rf & ~0x3f) | (txpow1 & 0x3f);
	if ((rf & 0x3f) > txpow_bound)
		rf = (rf & ~0x3f) | txpow_bound;
	run_rt3070_rf_write(sc, 49, rf);

	/* RF R50 ch1 Tx power ALC code. */
	run_rt3070_rf_read(sc, 50, &rf);
	rf &= ~(1 << 7 | 1 << 6);
	rf |= (reg << 6);
	rf = (rf & ~0x3f) | (txpow2 & 0x3f);
	if ((rf & 0x3f) > txpow_bound)
		rf = (rf & ~0x3f) | txpow_bound;
	run_rt3070_rf_write(sc, 50, rf);

	/* Enable RF_BLOCK, PLL_PD, RX0_PD, and TX0_PD. */
	run_rt3070_rf_read(sc, 1, &rf);
	rf |= (RT3070_RF_BLOCK | RT3070_PLL_PD | RT3070_RX0_PD | RT3070_TX0_PD);
	if (sc->ntxchains > 1)
		rf |= RT3070_TX1_PD;
	if (sc->nrxchains > 1)
		rf |= RT3070_RX1_PD;
	run_rt3070_rf_write(sc, 1, rf);

	run_rt3070_rf_write(sc, 6, 0xe4);

	run_rt3070_rf_write(sc, 30, 0x10);
	run_rt3070_rf_write(sc, 31, 0x80);
	run_rt3070_rf_write(sc, 32, 0x80);

	run_adjust_freq_offset(sc);

	/* Enable VCO calibration. */
	run_rt3070_rf_read(sc, 3, &rf);
	rf |= RT5390_VCOCAL;
	run_rt3070_rf_write(sc, 3, rf);
}

static void
run_set_rx_antenna(struct run_softc *sc, int aux)
{
	uint32_t tmp;
	uint8_t bbp152;

	if (aux) {
		if (sc->rf_rev == RT5390_RF_5370) {
			run_bbp_read(sc, 152, &bbp152);
			run_bbp_write(sc, 152, bbp152 & ~0x80);
		} else {
			run_mcu_cmd(sc, RT2860_MCU_CMD_ANTSEL, 0);
			run_read(sc, RT2860_GPIO_CTRL, &tmp);
			run_write(sc, RT2860_GPIO_CTRL, (tmp & ~0x0808) | 0x08);
		}
	} else {
		if (sc->rf_rev == RT5390_RF_5370) {
			run_bbp_read(sc, 152, &bbp152);
			run_bbp_write(sc, 152, bbp152 | 0x80);
		} else {
			run_mcu_cmd(sc, RT2860_MCU_CMD_ANTSEL, 1);
			run_read(sc, RT2860_GPIO_CTRL, &tmp);
			run_write(sc, RT2860_GPIO_CTRL, tmp & ~0x0808);
		}
	}
}

static int
run_set_chan(struct run_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	u_int chan, group;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return (EINVAL);

	if (sc->mac_ver == 0x5592)
		run_rt5592_set_chan(sc, chan);
	else if (sc->mac_ver >= 0x5390)
		run_rt5390_set_chan(sc, chan);
	else if (sc->mac_ver == 0x3593)
		run_rt3593_set_chan(sc, chan);
	else if (sc->mac_ver == 0x3572)
		run_rt3572_set_chan(sc, chan);
	else if (sc->mac_ver >= 0x3070)
		run_rt3070_set_chan(sc, chan);
	else
		run_rt2870_set_chan(sc, chan);

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
	run_select_chan_group(sc, group);

	run_delay(sc, 10);

	/* Perform IQ calibration. */
	if (sc->mac_ver >= 0x5392)
		run_iq_calib(sc, chan);

	return (0);
}

static void
run_set_channel(struct ieee80211com *ic)
{
	struct run_softc *sc = ic->ic_softc;

	RUN_LOCK(sc);
	run_set_chan(sc, ic->ic_curchan);
	RUN_UNLOCK(sc);

	return;
}

static void
run_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct run_softc *sc = ic->ic_softc;
	uint8_t bands[IEEE80211_MODE_BYTES];

	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	ieee80211_add_channels_default_2ghz(chans, maxchans, nchans, bands, 0);

	if (sc->rf_rev == RT2860_RF_2750 || sc->rf_rev == RT2860_RF_2850 ||
	    sc->rf_rev == RT3070_RF_3052 || sc->rf_rev == RT3593_RF_3053 ||
	    sc->rf_rev == RT5592_RF_5592) {
		setbit(bands, IEEE80211_MODE_11A);
		ieee80211_add_channel_list_5ghz(chans, maxchans, nchans,
		    run_chan_5ghz, nitems(run_chan_5ghz), bands, 0);
	}
}

static void
run_scan_start(struct ieee80211com *ic)
{
	struct run_softc *sc = ic->ic_softc;

	RUN_LOCK(sc);

	/* abort TSF synchronization */
	run_disable_tsf(sc);
	run_set_bssid(sc, ieee80211broadcastaddr);

	RUN_UNLOCK(sc);

	return;
}

static void
run_scan_end(struct ieee80211com *ic)
{
	struct run_softc *sc = ic->ic_softc;

	RUN_LOCK(sc);

	run_enable_tsf_sync(sc);
	run_set_bssid(sc, sc->sc_bssid);

	RUN_UNLOCK(sc);

	return;
}

/*
 * Could be called from ieee80211_node_timeout()
 * (non-sleepable thread)
 */
static void
run_update_beacon(struct ieee80211vap *vap, int item)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_beacon_offsets *bo = &vap->iv_bcn_off;
	struct ieee80211_node *ni = vap->iv_bss;
	struct run_softc *sc = ic->ic_softc;
	struct run_vap *rvp = RUN_VAP(vap);
	int mcast = 0;
	uint32_t i;

	switch (item) {
	case IEEE80211_BEACON_ERP:
		run_updateslot(ic);
		break;
	case IEEE80211_BEACON_HTINFO:
		run_updateprot(ic);
		break;
	case IEEE80211_BEACON_TIM:
		mcast = 1;	/*TODO*/
		break;
	default:
		break;
	}

	setbit(bo->bo_flags, item);
	if (rvp->beacon_mbuf == NULL) {
		rvp->beacon_mbuf = ieee80211_beacon_alloc(ni);
		if (rvp->beacon_mbuf == NULL)
			return;
	}
	ieee80211_beacon_update(ni, rvp->beacon_mbuf, mcast);

	i = RUN_CMDQ_GET(&sc->cmdq_store);
	RUN_DPRINTF(sc, RUN_DEBUG_BEACON, "cmdq_store=%d\n", i);
	sc->cmdq[i].func = run_update_beacon_cb;
	sc->cmdq[i].arg0 = vap;
	ieee80211_runtask(ic, &sc->cmdq_task);

	return;
}

static void
run_update_beacon_cb(void *arg)
{
	struct ieee80211vap *vap = arg;
	struct ieee80211_node *ni = vap->iv_bss;
	struct run_vap *rvp = RUN_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct run_softc *sc = ic->ic_softc;
	struct rt2860_txwi txwi;
	struct mbuf *m;
	uint16_t txwisize;
	uint8_t ridx;

	if (ni->ni_chan == IEEE80211_CHAN_ANYC)
		return;
	if (ic->ic_bsschan == IEEE80211_CHAN_ANYC)
		return;

	/*
	 * No need to call ieee80211_beacon_update(), run_update_beacon()
	 * is taking care of appropriate calls.
	 */
	if (rvp->beacon_mbuf == NULL) {
		rvp->beacon_mbuf = ieee80211_beacon_alloc(ni);
		if (rvp->beacon_mbuf == NULL)
			return;
	}
	m = rvp->beacon_mbuf;

	memset(&txwi, 0, sizeof(txwi));
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

	txwisize = (sc->mac_ver == 0x5592) ?
	    sizeof(txwi) + sizeof(uint32_t) : sizeof(txwi);
	run_write_region_1(sc, RT2860_BCN_BASE(rvp->rvp_id), (uint8_t *)&txwi,
	    txwisize);
	run_write_region_1(sc, RT2860_BCN_BASE(rvp->rvp_id) + txwisize,
	    mtod(m, uint8_t *), (m->m_pkthdr.len + 1) & ~1);
}

static void
run_updateprot(struct ieee80211com *ic)
{
	struct run_softc *sc = ic->ic_softc;
	uint32_t i;

	i = RUN_CMDQ_GET(&sc->cmdq_store);
	RUN_DPRINTF(sc, RUN_DEBUG_BEACON, "cmdq_store=%d\n", i);
	sc->cmdq[i].func = run_updateprot_cb;
	sc->cmdq[i].arg0 = ic;
	ieee80211_runtask(ic, &sc->cmdq_task);
}

static void
run_updateprot_cb(void *arg)
{
	struct ieee80211com *ic = arg;
	struct run_softc *sc = ic->ic_softc;
	uint32_t tmp;

	tmp = RT2860_RTSTH_EN | RT2860_PROT_NAV_SHORT | RT2860_TXOP_ALLOW_ALL;
	/* setup protection frame rate (MCS code) */
	tmp |= (ic->ic_curmode == IEEE80211_MODE_11A) ?
	    rt2860_rates[RT2860_RIDX_OFDM6].mcs | RT2860_PHY_OFDM :
	    rt2860_rates[RT2860_RIDX_CCK11].mcs;

	/* CCK frames don't require protection */
	run_write(sc, RT2860_CCK_PROT_CFG, tmp);
	if (ic->ic_flags & IEEE80211_F_USEPROT) {
		if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
			tmp |= RT2860_PROT_CTRL_RTS_CTS;
		else if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
			tmp |= RT2860_PROT_CTRL_CTS;
	}
	run_write(sc, RT2860_OFDM_PROT_CFG, tmp);
}

static void
run_usb_timeout_cb(void *arg)
{
	struct ieee80211vap *vap = arg;
	struct run_softc *sc = vap->iv_ic->ic_softc;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	if(vap->iv_state == IEEE80211_S_RUN &&
	    vap->iv_opmode != IEEE80211_M_STA)
		run_reset_livelock(sc);
	else if (vap->iv_state == IEEE80211_S_SCAN) {
		RUN_DPRINTF(sc, RUN_DEBUG_USB | RUN_DEBUG_STATE,
		    "timeout caused by scan\n");
		/* cancel bgscan */
		ieee80211_cancel_scan(vap);
	} else
		RUN_DPRINTF(sc, RUN_DEBUG_USB | RUN_DEBUG_STATE,
		    "timeout by unknown cause\n");
}

static void
run_reset_livelock(struct run_softc *sc)
{
	uint32_t tmp;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	/*
	 * In IBSS or HostAP modes (when the hardware sends beacons), the MAC
	 * can run into a livelock and start sending CTS-to-self frames like
	 * crazy if protection is enabled.  Reset MAC/BBP for a while
	 */
	run_read(sc, RT2860_DEBUG, &tmp);
	RUN_DPRINTF(sc, RUN_DEBUG_RESET, "debug reg %08x\n", tmp);
	if ((tmp & (1 << 29)) && (tmp & (1 << 7 | 1 << 5))) {
		RUN_DPRINTF(sc, RUN_DEBUG_RESET,
		    "CTS-to-self livelock detected\n");
		run_write(sc, RT2860_MAC_SYS_CTRL, RT2860_MAC_SRST);
		run_delay(sc, 1);
		run_write(sc, RT2860_MAC_SYS_CTRL,
		    RT2860_MAC_RX_EN | RT2860_MAC_TX_EN);
	}
}

static void
run_update_promisc_locked(struct run_softc *sc)
{
        uint32_t tmp;

	run_read(sc, RT2860_RX_FILTR_CFG, &tmp);

	tmp |= RT2860_DROP_UC_NOME;
        if (sc->sc_ic.ic_promisc > 0)
		tmp &= ~RT2860_DROP_UC_NOME;

	run_write(sc, RT2860_RX_FILTR_CFG, tmp);

        RUN_DPRINTF(sc, RUN_DEBUG_RECV, "%s promiscuous mode\n",
	    (sc->sc_ic.ic_promisc > 0) ?  "entering" : "leaving");
}

static void
run_update_promisc(struct ieee80211com *ic)
{
	struct run_softc *sc = ic->ic_softc;

	if ((sc->sc_flags & RUN_RUNNING) == 0)
		return;

	RUN_LOCK(sc);
	run_update_promisc_locked(sc);
	RUN_UNLOCK(sc);
}

static void
run_enable_tsf_sync(struct run_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t tmp;

	RUN_DPRINTF(sc, RUN_DEBUG_BEACON, "rvp_id=%d ic_opmode=%d\n",
	    RUN_VAP(vap)->rvp_id, ic->ic_opmode);

	run_read(sc, RT2860_BCN_TIME_CFG, &tmp);
	tmp &= ~0x1fffff;
	tmp |= vap->iv_bss->ni_intval * 16;
	tmp |= RT2860_TSF_TIMER_EN | RT2860_TBTT_TIMER_EN;

	if (ic->ic_opmode == IEEE80211_M_STA) {
		/*
		 * Local TSF is always updated with remote TSF on beacon
		 * reception.
		 */
		tmp |= 1 << RT2860_TSF_SYNC_MODE_SHIFT;
	} else if (ic->ic_opmode == IEEE80211_M_IBSS) {
	        tmp |= RT2860_BCN_TX_EN;
	        /*
	         * Local TSF is updated with remote TSF on beacon reception
	         * only if the remote TSF is greater than local TSF.
	         */
	        tmp |= 2 << RT2860_TSF_SYNC_MODE_SHIFT;
	} else if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_MBSS) {
	        tmp |= RT2860_BCN_TX_EN;
	        /* SYNC with nobody */
	        tmp |= 3 << RT2860_TSF_SYNC_MODE_SHIFT;
	} else {
		RUN_DPRINTF(sc, RUN_DEBUG_BEACON,
		    "Enabling TSF failed. undefined opmode\n");
		return;
	}

	run_write(sc, RT2860_BCN_TIME_CFG, tmp);
}

static void
run_enable_tsf(struct run_softc *sc)
{
	uint32_t tmp;

	if (run_read(sc, RT2860_BCN_TIME_CFG, &tmp) == 0) {
		tmp &= ~(RT2860_BCN_TX_EN | RT2860_TBTT_TIMER_EN);
		tmp |= RT2860_TSF_TIMER_EN;
		run_write(sc, RT2860_BCN_TIME_CFG, tmp);
	}
}

static void
run_disable_tsf(struct run_softc *sc)
{
	uint32_t tmp;

	if (run_read(sc, RT2860_BCN_TIME_CFG, &tmp) == 0) {
		tmp &= ~(RT2860_BCN_TX_EN | RT2860_TSF_TIMER_EN |
		    RT2860_TBTT_TIMER_EN);
		run_write(sc, RT2860_BCN_TIME_CFG, tmp);
	}
}

static void
run_get_tsf(struct run_softc *sc, uint64_t *buf)
{
	run_read_region_1(sc, RT2860_TSF_TIMER_DW0, (uint8_t *)buf,
	    sizeof(*buf));
}

static void
run_enable_mrr(struct run_softc *sc)
{
#define	CCK(mcs)	(mcs)
#define	OFDM(mcs)	(1 << 3 | (mcs))
	run_write(sc, RT2860_LG_FBK_CFG0,
	    OFDM(6) << 28 |	/* 54->48 */
	    OFDM(5) << 24 |	/* 48->36 */
	    OFDM(4) << 20 |	/* 36->24 */
	    OFDM(3) << 16 |	/* 24->18 */
	    OFDM(2) << 12 |	/* 18->12 */
	    OFDM(1) <<  8 |	/* 12-> 9 */
	    OFDM(0) <<  4 |	/*  9-> 6 */
	    OFDM(0));		/*  6-> 6 */

	run_write(sc, RT2860_LG_FBK_CFG1,
	    CCK(2) << 12 |	/* 11->5.5 */
	    CCK(1) <<  8 |	/* 5.5-> 2 */
	    CCK(0) <<  4 |	/*   2-> 1 */
	    CCK(0));		/*   1-> 1 */
#undef OFDM
#undef CCK
}

static void
run_set_txpreamble(struct run_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	run_read(sc, RT2860_AUTO_RSP_CFG, &tmp);
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= RT2860_CCK_SHORT_EN;
	else
		tmp &= ~RT2860_CCK_SHORT_EN;
	run_write(sc, RT2860_AUTO_RSP_CFG, tmp);
}

static void
run_set_basicrates(struct run_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* set basic rates mask */
	if (ic->ic_curmode == IEEE80211_MODE_11B)
		run_write(sc, RT2860_LEGACY_BASIC_RATE, 0x003);
	else if (ic->ic_curmode == IEEE80211_MODE_11A)
		run_write(sc, RT2860_LEGACY_BASIC_RATE, 0x150);
	else	/* 11g */
		run_write(sc, RT2860_LEGACY_BASIC_RATE, 0x15f);
}

static void
run_set_leds(struct run_softc *sc, uint16_t which)
{
	(void)run_mcu_cmd(sc, RT2860_MCU_CMD_LEDS,
	    which | (sc->leds & 0x7f));
}

static void
run_set_bssid(struct run_softc *sc, const uint8_t *bssid)
{
	run_write(sc, RT2860_MAC_BSSID_DW0,
	    bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24);
	run_write(sc, RT2860_MAC_BSSID_DW1,
	    bssid[4] | bssid[5] << 8);
}

static void
run_set_macaddr(struct run_softc *sc, const uint8_t *addr)
{
	run_write(sc, RT2860_MAC_ADDR_DW0,
	    addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	run_write(sc, RT2860_MAC_ADDR_DW1,
	    addr[4] | addr[5] << 8 | 0xff << 16);
}

static void
run_updateslot(struct ieee80211com *ic)
{
	struct run_softc *sc = ic->ic_softc;
	uint32_t i;

	i = RUN_CMDQ_GET(&sc->cmdq_store);
	RUN_DPRINTF(sc, RUN_DEBUG_BEACON, "cmdq_store=%d\n", i);
	sc->cmdq[i].func = run_updateslot_cb;
	sc->cmdq[i].arg0 = ic;
	ieee80211_runtask(ic, &sc->cmdq_task);

	return;
}

/* ARGSUSED */
static void
run_updateslot_cb(void *arg)
{
	struct ieee80211com *ic = arg;
	struct run_softc *sc = ic->ic_softc;
	uint32_t tmp;

	run_read(sc, RT2860_BKOFF_SLOT_CFG, &tmp);
	tmp &= ~0xff;
	tmp |= IEEE80211_GET_SLOTTIME(ic);
	run_write(sc, RT2860_BKOFF_SLOT_CFG, tmp);
}

static void
run_update_mcast(struct ieee80211com *ic)
{
}

static int8_t
run_rssi2dbm(struct run_softc *sc, uint8_t rssi, uint8_t rxchain)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c = ic->ic_curchan;
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

	return (-12 - delta - rssi);
}

static void
run_rt5390_bbp_init(struct run_softc *sc)
{
	u_int i;
	uint8_t bbp;

	/* Apply maximum likelihood detection for 2 stream case. */
	run_bbp_read(sc, 105, &bbp);
	if (sc->nrxchains > 1)
		run_bbp_write(sc, 105, bbp | RT5390_MLD);

	/* Avoid data lost and CRC error. */
	run_bbp_read(sc, 4, &bbp);
	run_bbp_write(sc, 4, bbp | RT5390_MAC_IF_CTRL);

	if (sc->mac_ver == 0x5592) {
		for (i = 0; i < nitems(rt5592_def_bbp); i++) {
			run_bbp_write(sc, rt5592_def_bbp[i].reg,
			    rt5592_def_bbp[i].val);
		}
		for (i = 0; i < nitems(rt5592_bbp_r196); i++) {
			run_bbp_write(sc, 195, i + 0x80);
			run_bbp_write(sc, 196, rt5592_bbp_r196[i]);
		}
	} else {
		for (i = 0; i < nitems(rt5390_def_bbp); i++) {
			run_bbp_write(sc, rt5390_def_bbp[i].reg,
			    rt5390_def_bbp[i].val);
		}
	}
	if (sc->mac_ver == 0x5392) {
		run_bbp_write(sc, 88, 0x90);
		run_bbp_write(sc, 95, 0x9a);
		run_bbp_write(sc, 98, 0x12);
		run_bbp_write(sc, 106, 0x12);
		run_bbp_write(sc, 134, 0xd0);
		run_bbp_write(sc, 135, 0xf6);
		run_bbp_write(sc, 148, 0x84);
	}

	run_bbp_read(sc, 152, &bbp);
	run_bbp_write(sc, 152, bbp | 0x80);

	/* Fix BBP254 for RT5592C. */
	if (sc->mac_ver == 0x5592 && sc->mac_rev >= 0x0221) {
		run_bbp_read(sc, 254, &bbp);
		run_bbp_write(sc, 254, bbp | 0x80);
	}

	/* Disable hardware antenna diversity. */
	if (sc->mac_ver == 0x5390)
		run_bbp_write(sc, 154, 0);

	/* Initialize Rx CCK/OFDM frequency offset report. */
	run_bbp_write(sc, 142, 1);
	run_bbp_write(sc, 143, 57);
}

static int
run_bbp_init(struct run_softc *sc)
{
	int i, error, ntries;
	uint8_t bbp0;

	/* wait for BBP to wake up */
	for (ntries = 0; ntries < 20; ntries++) {
		if ((error = run_bbp_read(sc, 0, &bbp0)) != 0)
			return error;
		if (bbp0 != 0 && bbp0 != 0xff)
			break;
	}
	if (ntries == 20)
		return (ETIMEDOUT);

	/* initialize BBP registers to default values */
	if (sc->mac_ver >= 0x5390)
		run_rt5390_bbp_init(sc);
	else {
		for (i = 0; i < nitems(rt2860_def_bbp); i++) {
			run_bbp_write(sc, rt2860_def_bbp[i].reg,
			    rt2860_def_bbp[i].val);
		}
	}

	if (sc->mac_ver == 0x3593) {
		run_bbp_write(sc, 79, 0x13);
		run_bbp_write(sc, 80, 0x05);
		run_bbp_write(sc, 81, 0x33);
		run_bbp_write(sc, 86, 0x46);
		run_bbp_write(sc, 137, 0x0f);
	}
		
	/* fix BBP84 for RT2860E */
	if (sc->mac_ver == 0x2860 && sc->mac_rev != 0x0101)
		run_bbp_write(sc, 84, 0x19);

	if (sc->mac_ver >= 0x3070 && (sc->mac_ver != 0x3593 &&
	    sc->mac_ver != 0x5592)) {
		run_bbp_write(sc, 79, 0x13);
		run_bbp_write(sc, 80, 0x05);
		run_bbp_write(sc, 81, 0x33);
	} else if (sc->mac_ver == 0x2860 && sc->mac_rev == 0x0100) {
		run_bbp_write(sc, 69, 0x16);
		run_bbp_write(sc, 73, 0x12);
	}
	return (0);
}

static int
run_rt3070_rf_init(struct run_softc *sc)
{
	uint32_t tmp;
	uint8_t bbp4, mingain, rf, target;
	u_int i;

	run_rt3070_rf_read(sc, 30, &rf);
	/* toggle RF R30 bit 7 */
	run_rt3070_rf_write(sc, 30, rf | 0x80);
	run_delay(sc, 10);
	run_rt3070_rf_write(sc, 30, rf & ~0x80);

	/* initialize RF registers to default value */
	if (sc->mac_ver == 0x3572) {
		for (i = 0; i < nitems(rt3572_def_rf); i++) {
			run_rt3070_rf_write(sc, rt3572_def_rf[i].reg,
			    rt3572_def_rf[i].val);
		}
	} else {
		for (i = 0; i < nitems(rt3070_def_rf); i++) {
			run_rt3070_rf_write(sc, rt3070_def_rf[i].reg,
			    rt3070_def_rf[i].val);
		}
	}

	if (sc->mac_ver == 0x3070 && sc->mac_rev < 0x0201) {
		/* 
		 * Change voltage from 1.2V to 1.35V for RT3070.
		 * The DAC issue (RT3070_LDO_CFG0) has been fixed
		 * in RT3070(F).
		 */
		run_read(sc, RT3070_LDO_CFG0, &tmp);
		tmp = (tmp & ~0x0f000000) | 0x0d000000;
		run_write(sc, RT3070_LDO_CFG0, tmp);

	} else if (sc->mac_ver == 0x3071) {
		run_rt3070_rf_read(sc, 6, &rf);
		run_rt3070_rf_write(sc, 6, rf | 0x40);
		run_rt3070_rf_write(sc, 31, 0x14);

		run_read(sc, RT3070_LDO_CFG0, &tmp);
		tmp &= ~0x1f000000;
		if (sc->mac_rev < 0x0211)
			tmp |= 0x0d000000;	/* 1.3V */
		else
			tmp |= 0x01000000;	/* 1.2V */
		run_write(sc, RT3070_LDO_CFG0, tmp);

		/* patch LNA_PE_G1 */
		run_read(sc, RT3070_GPIO_SWITCH, &tmp);
		run_write(sc, RT3070_GPIO_SWITCH, tmp & ~0x20);

	} else if (sc->mac_ver == 0x3572) {
		run_rt3070_rf_read(sc, 6, &rf);
		run_rt3070_rf_write(sc, 6, rf | 0x40);

		/* increase voltage from 1.2V to 1.35V */
		run_read(sc, RT3070_LDO_CFG0, &tmp);
		tmp = (tmp & ~0x1f000000) | 0x0d000000;
		run_write(sc, RT3070_LDO_CFG0, tmp);

		if (sc->mac_rev < 0x0211 || !sc->patch_dac) {
			run_delay(sc, 1);	/* wait for 1msec */
			/* decrease voltage back to 1.2V */
			tmp = (tmp & ~0x1f000000) | 0x01000000;
			run_write(sc, RT3070_LDO_CFG0, tmp);
		}
	}

	/* select 20MHz bandwidth */
	run_rt3070_rf_read(sc, 31, &rf);
	run_rt3070_rf_write(sc, 31, rf & ~0x20);

	/* calibrate filter for 20MHz bandwidth */
	sc->rf24_20mhz = 0x1f;	/* default value */
	target = (sc->mac_ver < 0x3071) ? 0x16 : 0x13;
	run_rt3070_filter_calib(sc, 0x07, target, &sc->rf24_20mhz);

	/* select 40MHz bandwidth */
	run_bbp_read(sc, 4, &bbp4);
	run_bbp_write(sc, 4, (bbp4 & ~0x18) | 0x10);
	run_rt3070_rf_read(sc, 31, &rf);
	run_rt3070_rf_write(sc, 31, rf | 0x20);

	/* calibrate filter for 40MHz bandwidth */
	sc->rf24_40mhz = 0x2f;	/* default value */
	target = (sc->mac_ver < 0x3071) ? 0x19 : 0x15;
	run_rt3070_filter_calib(sc, 0x27, target, &sc->rf24_40mhz);

	/* go back to 20MHz bandwidth */
	run_bbp_read(sc, 4, &bbp4);
	run_bbp_write(sc, 4, bbp4 & ~0x18);

	if (sc->mac_ver == 0x3572) {
		/* save default BBP registers 25 and 26 values */
		run_bbp_read(sc, 25, &sc->bbp25);
		run_bbp_read(sc, 26, &sc->bbp26);
	} else if (sc->mac_rev < 0x0201 || sc->mac_rev < 0x0211)
		run_rt3070_rf_write(sc, 27, 0x03);

	run_read(sc, RT3070_OPT_14, &tmp);
	run_write(sc, RT3070_OPT_14, tmp | 1);

	if (sc->mac_ver == 0x3070 || sc->mac_ver == 0x3071) {
		run_rt3070_rf_read(sc, 17, &rf);
		rf &= ~RT3070_TX_LO1;
		if ((sc->mac_ver == 0x3070 ||
		     (sc->mac_ver == 0x3071 && sc->mac_rev >= 0x0211)) &&
		    !sc->ext_2ghz_lna)
			rf |= 0x20;	/* fix for long range Rx issue */
		mingain = (sc->mac_ver == 0x3070) ? 1 : 2;
		if (sc->txmixgain_2ghz >= mingain)
			rf = (rf & ~0x7) | sc->txmixgain_2ghz;
		run_rt3070_rf_write(sc, 17, rf);
	}

	if (sc->mac_ver == 0x3071) {
		run_rt3070_rf_read(sc, 1, &rf);
		rf &= ~(RT3070_RX0_PD | RT3070_TX0_PD);
		rf |= RT3070_RF_BLOCK | RT3070_RX1_PD | RT3070_TX1_PD;
		run_rt3070_rf_write(sc, 1, rf);

		run_rt3070_rf_read(sc, 15, &rf);
		run_rt3070_rf_write(sc, 15, rf & ~RT3070_TX_LO2);

		run_rt3070_rf_read(sc, 20, &rf);
		run_rt3070_rf_write(sc, 20, rf & ~RT3070_RX_LO1);

		run_rt3070_rf_read(sc, 21, &rf);
		run_rt3070_rf_write(sc, 21, rf & ~RT3070_RX_LO2);
	}

	if (sc->mac_ver == 0x3070 || sc->mac_ver == 0x3071) {
		/* fix Tx to Rx IQ glitch by raising RF voltage */
		run_rt3070_rf_read(sc, 27, &rf);
		rf &= ~0x77;
		if (sc->mac_rev < 0x0211)
			rf |= 0x03;
		run_rt3070_rf_write(sc, 27, rf);
	}
	return (0);
}

static void
run_rt3593_rf_init(struct run_softc *sc)
{
	uint32_t tmp;
	uint8_t rf;
	u_int i;

	/* Disable the GPIO bits 4 and 7 for LNA PE control. */
	run_read(sc, RT3070_GPIO_SWITCH, &tmp);
	tmp &= ~(1 << 4 | 1 << 7);
	run_write(sc, RT3070_GPIO_SWITCH, tmp);

	/* Initialize RF registers to default value. */
	for (i = 0; i < nitems(rt3593_def_rf); i++) {
		run_rt3070_rf_write(sc, rt3593_def_rf[i].reg,
		    rt3593_def_rf[i].val);
	}

	/* Toggle RF R2 to initiate calibration. */
	run_rt3070_rf_write(sc, 2, RT5390_RESCAL);

	/* Initialize RF frequency offset. */
	run_adjust_freq_offset(sc);

	run_rt3070_rf_read(sc, 18, &rf);
	run_rt3070_rf_write(sc, 18, rf | RT3593_AUTOTUNE_BYPASS);

	/*
	 * Increase voltage from 1.2V to 1.35V, wait for 1 msec to
	 * decrease voltage back to 1.2V.
	 */
	run_read(sc, RT3070_LDO_CFG0, &tmp);
	tmp = (tmp & ~0x1f000000) | 0x0d000000;
	run_write(sc, RT3070_LDO_CFG0, tmp);
	run_delay(sc, 1);
	tmp = (tmp & ~0x1f000000) | 0x01000000;
	run_write(sc, RT3070_LDO_CFG0, tmp);

	sc->rf24_20mhz = 0x1f;
	sc->rf24_40mhz = 0x2f;

	/* Save default BBP registers 25 and 26 values. */
	run_bbp_read(sc, 25, &sc->bbp25);
	run_bbp_read(sc, 26, &sc->bbp26);

	run_read(sc, RT3070_OPT_14, &tmp);
	run_write(sc, RT3070_OPT_14, tmp | 1);
}

static void
run_rt5390_rf_init(struct run_softc *sc)
{
	uint32_t tmp;
	uint8_t rf;
	u_int i;

	/* Toggle RF R2 to initiate calibration. */
	if (sc->mac_ver == 0x5390) {
		run_rt3070_rf_read(sc, 2, &rf);
		run_rt3070_rf_write(sc, 2, rf | RT5390_RESCAL);
		run_delay(sc, 10);
		run_rt3070_rf_write(sc, 2, rf & ~RT5390_RESCAL);
	} else {
		run_rt3070_rf_write(sc, 2, RT5390_RESCAL);
		run_delay(sc, 10);
	}

	/* Initialize RF registers to default value. */
	if (sc->mac_ver == 0x5592) {
		for (i = 0; i < nitems(rt5592_def_rf); i++) {
			run_rt3070_rf_write(sc, rt5592_def_rf[i].reg,
			    rt5592_def_rf[i].val);
		}
		/* Initialize RF frequency offset. */
		run_adjust_freq_offset(sc);
	} else if (sc->mac_ver == 0x5392) {
		for (i = 0; i < nitems(rt5392_def_rf); i++) {
			run_rt3070_rf_write(sc, rt5392_def_rf[i].reg,
			    rt5392_def_rf[i].val);
		}
		if (sc->mac_rev >= 0x0223) {
			run_rt3070_rf_write(sc, 23, 0x0f);
			run_rt3070_rf_write(sc, 24, 0x3e);
			run_rt3070_rf_write(sc, 51, 0x32);
			run_rt3070_rf_write(sc, 53, 0x22);
			run_rt3070_rf_write(sc, 56, 0xc1);
			run_rt3070_rf_write(sc, 59, 0x0f);
		}
	} else {
		for (i = 0; i < nitems(rt5390_def_rf); i++) {
			run_rt3070_rf_write(sc, rt5390_def_rf[i].reg,
			    rt5390_def_rf[i].val);
		}
		if (sc->mac_rev >= 0x0502) {
			run_rt3070_rf_write(sc, 6, 0xe0);
			run_rt3070_rf_write(sc, 25, 0x80);
			run_rt3070_rf_write(sc, 46, 0x73);
			run_rt3070_rf_write(sc, 53, 0x00);
			run_rt3070_rf_write(sc, 56, 0x42);
			run_rt3070_rf_write(sc, 61, 0xd1);
		}
	}

	sc->rf24_20mhz = 0x1f;	/* default value */
	sc->rf24_40mhz = (sc->mac_ver == 0x5592) ? 0 : 0x2f;

	if (sc->mac_rev < 0x0211)
		run_rt3070_rf_write(sc, 27, 0x3);

	run_read(sc, RT3070_OPT_14, &tmp);
	run_write(sc, RT3070_OPT_14, tmp | 1);
}

static int
run_rt3070_filter_calib(struct run_softc *sc, uint8_t init, uint8_t target,
    uint8_t *val)
{
	uint8_t rf22, rf24;
	uint8_t bbp55_pb, bbp55_sb, delta;
	int ntries;

	/* program filter */
	run_rt3070_rf_read(sc, 24, &rf24);
	rf24 = (rf24 & 0xc0) | init;	/* initial filter value */
	run_rt3070_rf_write(sc, 24, rf24);

	/* enable baseband loopback mode */
	run_rt3070_rf_read(sc, 22, &rf22);
	run_rt3070_rf_write(sc, 22, rf22 | 0x01);

	/* set power and frequency of passband test tone */
	run_bbp_write(sc, 24, 0x00);
	for (ntries = 0; ntries < 100; ntries++) {
		/* transmit test tone */
		run_bbp_write(sc, 25, 0x90);
		run_delay(sc, 10);
		/* read received power */
		run_bbp_read(sc, 55, &bbp55_pb);
		if (bbp55_pb != 0)
			break;
	}
	if (ntries == 100)
		return (ETIMEDOUT);

	/* set power and frequency of stopband test tone */
	run_bbp_write(sc, 24, 0x06);
	for (ntries = 0; ntries < 100; ntries++) {
		/* transmit test tone */
		run_bbp_write(sc, 25, 0x90);
		run_delay(sc, 10);
		/* read received power */
		run_bbp_read(sc, 55, &bbp55_sb);

		delta = bbp55_pb - bbp55_sb;
		if (delta > target)
			break;

		/* reprogram filter */
		rf24++;
		run_rt3070_rf_write(sc, 24, rf24);
	}
	if (ntries < 100) {
		if (rf24 != init)
			rf24--;	/* backtrack */
		*val = rf24;
		run_rt3070_rf_write(sc, 24, rf24);
	}

	/* restore initial state */
	run_bbp_write(sc, 24, 0x00);

	/* disable baseband loopback mode */
	run_rt3070_rf_read(sc, 22, &rf22);
	run_rt3070_rf_write(sc, 22, rf22 & ~0x01);

	return (0);
}

static void
run_rt3070_rf_setup(struct run_softc *sc)
{
	uint8_t bbp, rf;
	int i;

	if (sc->mac_ver == 0x3572) {
		/* enable DC filter */
		if (sc->mac_rev >= 0x0201)
			run_bbp_write(sc, 103, 0xc0);

		run_bbp_read(sc, 138, &bbp);
		if (sc->ntxchains == 1)
			bbp |= 0x20;	/* turn off DAC1 */
		if (sc->nrxchains == 1)
			bbp &= ~0x02;	/* turn off ADC1 */
		run_bbp_write(sc, 138, bbp);

		if (sc->mac_rev >= 0x0211) {
			/* improve power consumption */
			run_bbp_read(sc, 31, &bbp);
			run_bbp_write(sc, 31, bbp & ~0x03);
		}

		run_rt3070_rf_read(sc, 16, &rf);
		rf = (rf & ~0x07) | sc->txmixgain_2ghz;
		run_rt3070_rf_write(sc, 16, rf);

	} else if (sc->mac_ver == 0x3071) {
		if (sc->mac_rev >= 0x0211) {
			/* enable DC filter */
			run_bbp_write(sc, 103, 0xc0);

			/* improve power consumption */
			run_bbp_read(sc, 31, &bbp);
			run_bbp_write(sc, 31, bbp & ~0x03);
		}

		run_bbp_read(sc, 138, &bbp);
		if (sc->ntxchains == 1)
			bbp |= 0x20;	/* turn off DAC1 */
		if (sc->nrxchains == 1)
			bbp &= ~0x02;	/* turn off ADC1 */
		run_bbp_write(sc, 138, bbp);

		run_write(sc, RT2860_TX_SW_CFG1, 0);
		if (sc->mac_rev < 0x0211) {
			run_write(sc, RT2860_TX_SW_CFG2,
			    sc->patch_dac ? 0x2c : 0x0f);
		} else
			run_write(sc, RT2860_TX_SW_CFG2, 0);

	} else if (sc->mac_ver == 0x3070) {
		if (sc->mac_rev >= 0x0201) {
			/* enable DC filter */
			run_bbp_write(sc, 103, 0xc0);

			/* improve power consumption */
			run_bbp_read(sc, 31, &bbp);
			run_bbp_write(sc, 31, bbp & ~0x03);
		}

		if (sc->mac_rev < 0x0201) {
			run_write(sc, RT2860_TX_SW_CFG1, 0);
			run_write(sc, RT2860_TX_SW_CFG2, 0x2c);
		} else
			run_write(sc, RT2860_TX_SW_CFG2, 0);
	}

	/* initialize RF registers from ROM for >=RT3071*/
	if (sc->mac_ver >= 0x3071) {
		for (i = 0; i < 10; i++) {
			if (sc->rf[i].reg == 0 || sc->rf[i].reg == 0xff)
				continue;
			run_rt3070_rf_write(sc, sc->rf[i].reg, sc->rf[i].val);
		}
	}
}

static void
run_rt3593_rf_setup(struct run_softc *sc)
{
	uint8_t bbp, rf;

	if (sc->mac_rev >= 0x0211) {
		/* Enable DC filter. */
		run_bbp_write(sc, 103, 0xc0);
	}
	run_write(sc, RT2860_TX_SW_CFG1, 0);
	if (sc->mac_rev < 0x0211) {
		run_write(sc, RT2860_TX_SW_CFG2,
		    sc->patch_dac ? 0x2c : 0x0f);
	} else
		run_write(sc, RT2860_TX_SW_CFG2, 0);

	run_rt3070_rf_read(sc, 50, &rf);
	run_rt3070_rf_write(sc, 50, rf & ~RT3593_TX_LO2);

	run_rt3070_rf_read(sc, 51, &rf);
	rf = (rf & ~(RT3593_TX_LO1 | 0x0c)) |
	    ((sc->txmixgain_2ghz & 0x07) << 2);
	run_rt3070_rf_write(sc, 51, rf);

	run_rt3070_rf_read(sc, 38, &rf);
	run_rt3070_rf_write(sc, 38, rf & ~RT5390_RX_LO1);

	run_rt3070_rf_read(sc, 39, &rf);
	run_rt3070_rf_write(sc, 39, rf & ~RT5390_RX_LO2);

	run_rt3070_rf_read(sc, 1, &rf);
	run_rt3070_rf_write(sc, 1, rf & ~(RT3070_RF_BLOCK | RT3070_PLL_PD));

	run_rt3070_rf_read(sc, 30, &rf);
	rf = (rf & ~0x18) | 0x10;
	run_rt3070_rf_write(sc, 30, rf);

	/* Apply maximum likelihood detection for 2 stream case. */
	run_bbp_read(sc, 105, &bbp);
	if (sc->nrxchains > 1)
		run_bbp_write(sc, 105, bbp | RT5390_MLD);

	/* Avoid data lost and CRC error. */
	run_bbp_read(sc, 4, &bbp);
	run_bbp_write(sc, 4, bbp | RT5390_MAC_IF_CTRL);

	run_bbp_write(sc, 92, 0x02);
	run_bbp_write(sc, 82, 0x82);
	run_bbp_write(sc, 106, 0x05);
	run_bbp_write(sc, 104, 0x92);
	run_bbp_write(sc, 88, 0x90);
	run_bbp_write(sc, 148, 0xc8);
	run_bbp_write(sc, 47, 0x48);
	run_bbp_write(sc, 120, 0x50);

	run_bbp_write(sc, 163, 0x9d);

	/* SNR mapping. */
	run_bbp_write(sc, 142, 0x06);
	run_bbp_write(sc, 143, 0xa0);
	run_bbp_write(sc, 142, 0x07);
	run_bbp_write(sc, 143, 0xa1);
	run_bbp_write(sc, 142, 0x08);
	run_bbp_write(sc, 143, 0xa2);

	run_bbp_write(sc, 31, 0x08);
	run_bbp_write(sc, 68, 0x0b);
	run_bbp_write(sc, 105, 0x04);
}

static void
run_rt5390_rf_setup(struct run_softc *sc)
{
	uint8_t bbp, rf;

	if (sc->mac_rev >= 0x0211) {
		/* Enable DC filter. */
		run_bbp_write(sc, 103, 0xc0);

		if (sc->mac_ver != 0x5592) {
			/* Improve power consumption. */
			run_bbp_read(sc, 31, &bbp);
			run_bbp_write(sc, 31, bbp & ~0x03);
		}
	}

	run_bbp_read(sc, 138, &bbp);
	if (sc->ntxchains == 1)
		bbp |= 0x20;	/* turn off DAC1 */
	if (sc->nrxchains == 1)
		bbp &= ~0x02;	/* turn off ADC1 */
	run_bbp_write(sc, 138, bbp);

	run_rt3070_rf_read(sc, 38, &rf);
	run_rt3070_rf_write(sc, 38, rf & ~RT5390_RX_LO1);

	run_rt3070_rf_read(sc, 39, &rf);
	run_rt3070_rf_write(sc, 39, rf & ~RT5390_RX_LO2);

	/* Avoid data lost and CRC error. */
	run_bbp_read(sc, 4, &bbp);
	run_bbp_write(sc, 4, bbp | RT5390_MAC_IF_CTRL);

	run_rt3070_rf_read(sc, 30, &rf);
	rf = (rf & ~0x18) | 0x10;
	run_rt3070_rf_write(sc, 30, rf);

	if (sc->mac_ver != 0x5592) {
		run_write(sc, RT2860_TX_SW_CFG1, 0);
		if (sc->mac_rev < 0x0211) {
			run_write(sc, RT2860_TX_SW_CFG2,
			    sc->patch_dac ? 0x2c : 0x0f);
		} else
			run_write(sc, RT2860_TX_SW_CFG2, 0);
	}
}

static int
run_txrx_enable(struct run_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	int error, ntries;

	run_write(sc, RT2860_MAC_SYS_CTRL, RT2860_MAC_TX_EN);
	for (ntries = 0; ntries < 200; ntries++) {
		if ((error = run_read(sc, RT2860_WPDMA_GLO_CFG, &tmp)) != 0)
			return (error);
		if ((tmp & (RT2860_TX_DMA_BUSY | RT2860_RX_DMA_BUSY)) == 0)
			break;
		run_delay(sc, 50);
	}
	if (ntries == 200)
		return (ETIMEDOUT);

	run_delay(sc, 50);

	tmp |= RT2860_RX_DMA_EN | RT2860_TX_DMA_EN | RT2860_TX_WB_DDONE;
	run_write(sc, RT2860_WPDMA_GLO_CFG, tmp);

	/* enable Rx bulk aggregation (set timeout and limit) */
	tmp = RT2860_USB_TX_EN | RT2860_USB_RX_EN | RT2860_USB_RX_AGG_EN |
	    RT2860_USB_RX_AGG_TO(128) | RT2860_USB_RX_AGG_LMT(2);
	run_write(sc, RT2860_USB_DMA_CFG, tmp);

	/* set Rx filter */
	tmp = RT2860_DROP_CRC_ERR | RT2860_DROP_PHY_ERR;
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RT2860_DROP_UC_NOME | RT2860_DROP_DUPL |
		    RT2860_DROP_CTS | RT2860_DROP_BA | RT2860_DROP_ACK |
		    RT2860_DROP_VER_ERR | RT2860_DROP_CTRL_RSV |
		    RT2860_DROP_CFACK | RT2860_DROP_CFEND;
		if (ic->ic_opmode == IEEE80211_M_STA)
			tmp |= RT2860_DROP_RTS | RT2860_DROP_PSPOLL;
	}
	run_write(sc, RT2860_RX_FILTR_CFG, tmp);

	run_write(sc, RT2860_MAC_SYS_CTRL,
	    RT2860_MAC_RX_EN | RT2860_MAC_TX_EN);

	return (0);
}

static void
run_adjust_freq_offset(struct run_softc *sc)
{
	uint8_t rf, tmp;

	run_rt3070_rf_read(sc, 17, &rf);
	tmp = rf;
	rf = (rf & ~0x7f) | (sc->freq & 0x7f);
	rf = MIN(rf, 0x5f);

	if (tmp != rf)
		run_mcu_cmd(sc, 0x74, (tmp << 8 ) | rf);
}

static void
run_init_locked(struct run_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t tmp;
	uint8_t bbp1, bbp3;
	int i;
	int ridx;
	int ntries;

	if (ic->ic_nrunning > 1)
		return;

	run_stop(sc);

	if (run_load_microcode(sc) != 0) {
		device_printf(sc->sc_dev, "could not load 8051 microcode\n");
		goto fail;
	}

	for (ntries = 0; ntries < 100; ntries++) {
		if (run_read(sc, RT2860_ASIC_VER_ID, &tmp) != 0)
			goto fail;
		if (tmp != 0 && tmp != 0xffffffff)
			break;
		run_delay(sc, 10);
	}
	if (ntries == 100)
		goto fail;

	for (i = 0; i != RUN_EP_QUEUES; i++)
		run_setup_tx_list(sc, &sc->sc_epq[i]);

	run_set_macaddr(sc, vap ? vap->iv_myaddr : ic->ic_macaddr);

	for (ntries = 0; ntries < 100; ntries++) {
		if (run_read(sc, RT2860_WPDMA_GLO_CFG, &tmp) != 0)
			goto fail;
		if ((tmp & (RT2860_TX_DMA_BUSY | RT2860_RX_DMA_BUSY)) == 0)
			break;
		run_delay(sc, 10);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev, "timeout waiting for DMA engine\n");
		goto fail;
	}
	tmp &= 0xff0;
	tmp |= RT2860_TX_WB_DDONE;
	run_write(sc, RT2860_WPDMA_GLO_CFG, tmp);

	/* turn off PME_OEN to solve high-current issue */
	run_read(sc, RT2860_SYS_CTRL, &tmp);
	run_write(sc, RT2860_SYS_CTRL, tmp & ~RT2860_PME_OEN);

	run_write(sc, RT2860_MAC_SYS_CTRL,
	    RT2860_BBP_HRST | RT2860_MAC_SRST);
	run_write(sc, RT2860_USB_DMA_CFG, 0);

	if (run_reset(sc) != 0) {
		device_printf(sc->sc_dev, "could not reset chipset\n");
		goto fail;
	}

	run_write(sc, RT2860_MAC_SYS_CTRL, 0);

	/* init Tx power for all Tx rates (from EEPROM) */
	for (ridx = 0; ridx < 5; ridx++) {
		if (sc->txpow20mhz[ridx] == 0xffffffff)
			continue;
		run_write(sc, RT2860_TX_PWR_CFG(ridx), sc->txpow20mhz[ridx]);
	}

	for (i = 0; i < nitems(rt2870_def_mac); i++)
		run_write(sc, rt2870_def_mac[i].reg, rt2870_def_mac[i].val);
	run_write(sc, RT2860_WMM_AIFSN_CFG, 0x00002273);
	run_write(sc, RT2860_WMM_CWMIN_CFG, 0x00002344);
	run_write(sc, RT2860_WMM_CWMAX_CFG, 0x000034aa);

	if (sc->mac_ver >= 0x5390) {
		run_write(sc, RT2860_TX_SW_CFG0,
		    4 << RT2860_DLY_PAPE_EN_SHIFT | 4);
		if (sc->mac_ver >= 0x5392) {
			run_write(sc, RT2860_MAX_LEN_CFG, 0x00002fff);
			if (sc->mac_ver == 0x5592) {
				run_write(sc, RT2860_HT_FBK_CFG1, 0xedcba980);
				run_write(sc, RT2860_TXOP_HLDR_ET, 0x00000082);
			} else {
				run_write(sc, RT2860_HT_FBK_CFG1, 0xedcb4980);
				run_write(sc, RT2860_LG_FBK_CFG0, 0xedcba322);
			}
		}
	} else if (sc->mac_ver == 0x3593) {
		run_write(sc, RT2860_TX_SW_CFG0,
		    4 << RT2860_DLY_PAPE_EN_SHIFT | 2);
	} else if (sc->mac_ver >= 0x3070) {
		/* set delay of PA_PE assertion to 1us (unit of 0.25us) */
		run_write(sc, RT2860_TX_SW_CFG0,
		    4 << RT2860_DLY_PAPE_EN_SHIFT);
	}

	/* wait while MAC is busy */
	for (ntries = 0; ntries < 100; ntries++) {
		if (run_read(sc, RT2860_MAC_STATUS_REG, &tmp) != 0)
			goto fail;
		if (!(tmp & (RT2860_RX_STATUS_BUSY | RT2860_TX_STATUS_BUSY)))
			break;
		run_delay(sc, 10);
	}
	if (ntries == 100)
		goto fail;

	/* clear Host to MCU mailbox */
	run_write(sc, RT2860_H2M_BBPAGENT, 0);
	run_write(sc, RT2860_H2M_MAILBOX, 0);
	run_delay(sc, 10);

	if (run_bbp_init(sc) != 0) {
		device_printf(sc->sc_dev, "could not initialize BBP\n");
		goto fail;
	}

	/* abort TSF synchronization */
	run_disable_tsf(sc);

	/* clear RX WCID search table */
	run_set_region_4(sc, RT2860_WCID_ENTRY(0), 0, 512);
	/* clear WCID attribute table */
	run_set_region_4(sc, RT2860_WCID_ATTR(0), 0, 8 * 32);

	/* hostapd sets a key before init. So, don't clear it. */
	if (sc->cmdq_key_set != RUN_CMDQ_GO) {
		/* clear shared key table */
		run_set_region_4(sc, RT2860_SKEY(0, 0), 0, 8 * 32);
		/* clear shared key mode */
		run_set_region_4(sc, RT2860_SKEY_MODE_0_7, 0, 4);
	}

	run_read(sc, RT2860_US_CYC_CNT, &tmp);
	tmp = (tmp & ~0xff) | 0x1e;
	run_write(sc, RT2860_US_CYC_CNT, tmp);

	if (sc->mac_rev != 0x0101)
		run_write(sc, RT2860_TXOP_CTRL_CFG, 0x0000583f);

	run_write(sc, RT2860_WMM_TXOP0_CFG, 0);
	run_write(sc, RT2860_WMM_TXOP1_CFG, 48 << 16 | 96);

	/* write vendor-specific BBP values (from EEPROM) */
	if (sc->mac_ver < 0x3593) {
		for (i = 0; i < 10; i++) {
			if (sc->bbp[i].reg == 0 || sc->bbp[i].reg == 0xff)
				continue;
			run_bbp_write(sc, sc->bbp[i].reg, sc->bbp[i].val);
		}
	}

	/* select Main antenna for 1T1R devices */
	if (sc->rf_rev == RT3070_RF_3020 || sc->rf_rev == RT5390_RF_5370)
		run_set_rx_antenna(sc, 0);

	/* send LEDs operating mode to microcontroller */
	(void)run_mcu_cmd(sc, RT2860_MCU_CMD_LED1, sc->led[0]);
	(void)run_mcu_cmd(sc, RT2860_MCU_CMD_LED2, sc->led[1]);
	(void)run_mcu_cmd(sc, RT2860_MCU_CMD_LED3, sc->led[2]);

	if (sc->mac_ver >= 0x5390)
		run_rt5390_rf_init(sc);
	else if (sc->mac_ver == 0x3593)
		run_rt3593_rf_init(sc);
	else if (sc->mac_ver >= 0x3070)
		run_rt3070_rf_init(sc);

	/* disable non-existing Rx chains */
	run_bbp_read(sc, 3, &bbp3);
	bbp3 &= ~(1 << 3 | 1 << 4);
	if (sc->nrxchains == 2)
		bbp3 |= 1 << 3;
	else if (sc->nrxchains == 3)
		bbp3 |= 1 << 4;
	run_bbp_write(sc, 3, bbp3);

	/* disable non-existing Tx chains */
	run_bbp_read(sc, 1, &bbp1);
	if (sc->ntxchains == 1)
		bbp1 &= ~(1 << 3 | 1 << 4);
	run_bbp_write(sc, 1, bbp1);

	if (sc->mac_ver >= 0x5390)
		run_rt5390_rf_setup(sc);
	else if (sc->mac_ver == 0x3593)
		run_rt3593_rf_setup(sc);
	else if (sc->mac_ver >= 0x3070)
		run_rt3070_rf_setup(sc);

	/* select default channel */
	run_set_chan(sc, ic->ic_curchan);

	/* setup initial protection mode */
	run_updateprot_cb(ic);

	/* turn radio LED on */
	run_set_leds(sc, RT2860_LED_RADIO);

	sc->sc_flags |= RUN_RUNNING;
	sc->cmdq_run = RUN_CMDQ_GO;

	for (i = 0; i != RUN_N_XFER; i++)
		usbd_xfer_set_stall(sc->sc_xfer[i]);

	usbd_transfer_start(sc->sc_xfer[RUN_BULK_RX]);

	if (run_txrx_enable(sc) != 0)
		goto fail;

	return;

fail:
	run_stop(sc);
}

static void
run_stop(void *arg)
{
	struct run_softc *sc = (struct run_softc *)arg;
	uint32_t tmp;
	int i;
	int ntries;

	RUN_LOCK_ASSERT(sc, MA_OWNED);

	if (sc->sc_flags & RUN_RUNNING)
		run_set_leds(sc, 0);	/* turn all LEDs off */

	sc->sc_flags &= ~RUN_RUNNING;

	sc->ratectl_run = RUN_RATECTL_OFF;
	sc->cmdq_run = sc->cmdq_key_set;

	RUN_UNLOCK(sc);

	for(i = 0; i < RUN_N_XFER; i++)
		usbd_transfer_drain(sc->sc_xfer[i]);

	RUN_LOCK(sc);

	run_drain_mbufq(sc);

	if (sc->rx_m != NULL) {
		m_free(sc->rx_m);
		sc->rx_m = NULL;
	}

	/* Disable Tx/Rx DMA. */
	if (run_read(sc, RT2860_WPDMA_GLO_CFG, &tmp) != 0)
		return;
	tmp &= ~(RT2860_RX_DMA_EN | RT2860_TX_DMA_EN);
	run_write(sc, RT2860_WPDMA_GLO_CFG, tmp);

	for (ntries = 0; ntries < 100; ntries++) {
		if (run_read(sc, RT2860_WPDMA_GLO_CFG, &tmp) != 0)
			return;
		if ((tmp & (RT2860_TX_DMA_BUSY | RT2860_RX_DMA_BUSY)) == 0)
				break;
		run_delay(sc, 10);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev, "timeout waiting for DMA engine\n");
		return;
	}

	/* disable Tx/Rx */
	run_read(sc, RT2860_MAC_SYS_CTRL, &tmp);
	tmp &= ~(RT2860_MAC_RX_EN | RT2860_MAC_TX_EN);
	run_write(sc, RT2860_MAC_SYS_CTRL, tmp);

	/* wait for pending Tx to complete */
	for (ntries = 0; ntries < 100; ntries++) {
		if (run_read(sc, RT2860_TXRXQ_PCNT, &tmp) != 0) {
			RUN_DPRINTF(sc, RUN_DEBUG_XMIT | RUN_DEBUG_RESET,
			    "Cannot read Tx queue count\n");
			break;
		}
		if ((tmp & RT2860_TX2Q_PCNT_MASK) == 0) {
			RUN_DPRINTF(sc, RUN_DEBUG_XMIT | RUN_DEBUG_RESET,
			    "All Tx cleared\n");
			break;
		}
		run_delay(sc, 10);
	}
	if (ntries >= 100)
		RUN_DPRINTF(sc, RUN_DEBUG_XMIT | RUN_DEBUG_RESET,
		    "There are still pending Tx\n");
	run_delay(sc, 10);
	run_write(sc, RT2860_USB_DMA_CFG, 0);

	run_write(sc, RT2860_MAC_SYS_CTRL, RT2860_BBP_HRST | RT2860_MAC_SRST);
	run_write(sc, RT2860_MAC_SYS_CTRL, 0);

	for (i = 0; i != RUN_EP_QUEUES; i++)
		run_unsetup_tx_list(sc, &sc->sc_epq[i]);
}

static void
run_delay(struct run_softc *sc, u_int ms)
{
	usb_pause_mtx(mtx_owned(&sc->sc_mtx) ? 
	    &sc->sc_mtx : NULL, USB_MS_TO_TICKS(ms));
}

static device_method_t run_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		run_match),
	DEVMETHOD(device_attach,	run_attach),
	DEVMETHOD(device_detach,	run_detach),
	DEVMETHOD_END
};

static driver_t run_driver = {
	.name = "run",
	.methods = run_methods,
	.size = sizeof(struct run_softc)
};

static devclass_t run_devclass;

DRIVER_MODULE(run, uhub, run_driver, run_devclass, run_driver_loaded, NULL);
MODULE_DEPEND(run, wlan, 1, 1, 1);
MODULE_DEPEND(run, usb, 1, 1, 1);
MODULE_DEPEND(run, firmware, 1, 1, 1);
MODULE_VERSION(run, 1);
USB_PNP_HOST_INFO(run_devs);

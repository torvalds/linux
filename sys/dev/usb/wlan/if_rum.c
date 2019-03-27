/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2005-2007 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2006 Niall O'Higgins <niallo@openbsd.org>
 * Copyright (c) 2007-2008 Hans Petter Selasky <hselasky@FreeBSD.org>
 * Copyright (c) 2015 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 * Ralink Technology RT2501USB/RT2601USB chipset driver
 * http://www.ralinktech.com.tw/
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
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR rum_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/wlan/if_rumreg.h>
#include <dev/usb/wlan/if_rumvar.h>
#include <dev/usb/wlan/if_rumfw.h>

#ifdef USB_DEBUG
static int rum_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, rum, CTLFLAG_RW, 0, "USB rum");
SYSCTL_INT(_hw_usb_rum, OID_AUTO, debug, CTLFLAG_RWTUN, &rum_debug, 0,
    "Debug level");
#endif

static const STRUCT_USB_HOST_ID rum_devs[] = {
#define	RUM_DEV(v,p)  { USB_VP(USB_VENDOR_##v, USB_PRODUCT_##v##_##p) }
    RUM_DEV(ABOCOM, HWU54DM),
    RUM_DEV(ABOCOM, RT2573_2),
    RUM_DEV(ABOCOM, RT2573_3),
    RUM_DEV(ABOCOM, RT2573_4),
    RUM_DEV(ABOCOM, WUG2700),
    RUM_DEV(AMIT, CGWLUSB2GO),
    RUM_DEV(ASUS, RT2573_1),
    RUM_DEV(ASUS, RT2573_2),
    RUM_DEV(BELKIN, F5D7050A),
    RUM_DEV(BELKIN, F5D9050V3),
    RUM_DEV(CISCOLINKSYS, WUSB54GC),
    RUM_DEV(CISCOLINKSYS, WUSB54GR),
    RUM_DEV(CONCEPTRONIC2, C54RU2),
    RUM_DEV(COREGA, CGWLUSB2GL),
    RUM_DEV(COREGA, CGWLUSB2GPX),
    RUM_DEV(DICKSMITH, CWD854F),
    RUM_DEV(DICKSMITH, RT2573),
    RUM_DEV(EDIMAX, EW7318USG),
    RUM_DEV(DLINK2, DWLG122C1),
    RUM_DEV(DLINK2, WUA1340),
    RUM_DEV(DLINK2, DWA111),
    RUM_DEV(DLINK2, DWA110),
    RUM_DEV(GIGABYTE, GNWB01GS),
    RUM_DEV(GIGABYTE, GNWI05GS),
    RUM_DEV(GIGASET, RT2573),
    RUM_DEV(GOODWAY, RT2573),
    RUM_DEV(GUILLEMOT, HWGUSB254LB),
    RUM_DEV(GUILLEMOT, HWGUSB254V2AP),
    RUM_DEV(HUAWEI3COM, WUB320G),
    RUM_DEV(MELCO, G54HP),
    RUM_DEV(MELCO, SG54HP),
    RUM_DEV(MELCO, SG54HG),
    RUM_DEV(MELCO, WLIUCG),
    RUM_DEV(MELCO, WLRUCG),
    RUM_DEV(MELCO, WLRUCGAOSS),
    RUM_DEV(MSI, RT2573_1),
    RUM_DEV(MSI, RT2573_2),
    RUM_DEV(MSI, RT2573_3),
    RUM_DEV(MSI, RT2573_4),
    RUM_DEV(NOVATECH, RT2573),
    RUM_DEV(PLANEX2, GWUS54HP),
    RUM_DEV(PLANEX2, GWUS54MINI2),
    RUM_DEV(PLANEX2, GWUSMM),
    RUM_DEV(QCOM, RT2573),
    RUM_DEV(QCOM, RT2573_2),
    RUM_DEV(QCOM, RT2573_3),
    RUM_DEV(RALINK, RT2573),
    RUM_DEV(RALINK, RT2573_2),
    RUM_DEV(RALINK, RT2671),
    RUM_DEV(SITECOMEU, WL113R2),
    RUM_DEV(SITECOMEU, WL172),
    RUM_DEV(SPARKLAN, RT2573),
    RUM_DEV(SURECOM, RT2573),
#undef RUM_DEV
};

static device_probe_t rum_match;
static device_attach_t rum_attach;
static device_detach_t rum_detach;

static usb_callback_t rum_bulk_read_callback;
static usb_callback_t rum_bulk_write_callback;

static usb_error_t	rum_do_request(struct rum_softc *sc,
			    struct usb_device_request *req, void *data);
static usb_error_t	rum_do_mcu_request(struct rum_softc *sc, int);
static struct ieee80211vap *rum_vap_create(struct ieee80211com *,
			    const char [IFNAMSIZ], int, enum ieee80211_opmode,
			    int, const uint8_t [IEEE80211_ADDR_LEN],
			    const uint8_t [IEEE80211_ADDR_LEN]);
static void		rum_vap_delete(struct ieee80211vap *);
static void		rum_cmdq_cb(void *, int);
static int		rum_cmd_sleepable(struct rum_softc *, const void *,
			    size_t, uint8_t, CMD_FUNC_PROTO);
static void		rum_tx_free(struct rum_tx_data *, int);
static void		rum_setup_tx_list(struct rum_softc *);
static void		rum_reset_tx_list(struct rum_softc *,
			    struct ieee80211vap *);
static void		rum_unsetup_tx_list(struct rum_softc *);
static void		rum_beacon_miss(struct ieee80211vap *);
static void		rum_sta_recv_mgmt(struct ieee80211_node *,
			    struct mbuf *, int,
			    const struct ieee80211_rx_stats *, int, int);
static int		rum_set_power_state(struct rum_softc *, int);
static int		rum_newstate(struct ieee80211vap *,
			    enum ieee80211_state, int);
static uint8_t		rum_crypto_mode(struct rum_softc *, u_int, int);
static void		rum_setup_tx_desc(struct rum_softc *,
			    struct rum_tx_desc *, struct ieee80211_key *,
			    uint32_t, uint8_t, uint8_t, int, int, int);
static uint32_t		rum_tx_crypto_flags(struct rum_softc *,
			    struct ieee80211_node *,
			    const struct ieee80211_key *);
static int		rum_tx_mgt(struct rum_softc *, struct mbuf *,
			    struct ieee80211_node *);
static int		rum_tx_raw(struct rum_softc *, struct mbuf *,
			    struct ieee80211_node *, 
			    const struct ieee80211_bpf_params *);
static int		rum_tx_data(struct rum_softc *, struct mbuf *,
			    struct ieee80211_node *);
static int		rum_transmit(struct ieee80211com *, struct mbuf *);
static void		rum_start(struct rum_softc *);
static void		rum_parent(struct ieee80211com *);
static void		rum_eeprom_read(struct rum_softc *, uint16_t, void *,
			    int);
static uint32_t		rum_read(struct rum_softc *, uint16_t);
static void		rum_read_multi(struct rum_softc *, uint16_t, void *,
			    int);
static usb_error_t	rum_write(struct rum_softc *, uint16_t, uint32_t);
static usb_error_t	rum_write_multi(struct rum_softc *, uint16_t, void *,
			    size_t);
static usb_error_t	rum_setbits(struct rum_softc *, uint16_t, uint32_t);
static usb_error_t	rum_clrbits(struct rum_softc *, uint16_t, uint32_t);
static usb_error_t	rum_modbits(struct rum_softc *, uint16_t, uint32_t,
			    uint32_t);
static int		rum_bbp_busy(struct rum_softc *);
static void		rum_bbp_write(struct rum_softc *, uint8_t, uint8_t);
static uint8_t		rum_bbp_read(struct rum_softc *, uint8_t);
static void		rum_rf_write(struct rum_softc *, uint8_t, uint32_t);
static void		rum_select_antenna(struct rum_softc *);
static void		rum_enable_mrr(struct rum_softc *);
static void		rum_set_txpreamble(struct rum_softc *);
static void		rum_set_basicrates(struct rum_softc *);
static void		rum_select_band(struct rum_softc *,
			    struct ieee80211_channel *);
static void		rum_set_chan(struct rum_softc *,
			    struct ieee80211_channel *);
static void		rum_set_maxretry(struct rum_softc *,
			    struct ieee80211vap *);
static int		rum_enable_tsf_sync(struct rum_softc *);
static void		rum_enable_tsf(struct rum_softc *);
static void		rum_abort_tsf_sync(struct rum_softc *);
static void		rum_get_tsf(struct rum_softc *, uint64_t *);
static void		rum_update_slot_cb(struct rum_softc *,
			    union sec_param *, uint8_t);
static void		rum_update_slot(struct ieee80211com *);
static int		rum_wme_update(struct ieee80211com *);
static void		rum_set_bssid(struct rum_softc *, const uint8_t *);
static void		rum_set_macaddr(struct rum_softc *, const uint8_t *);
static void		rum_update_mcast(struct ieee80211com *);
static void		rum_update_promisc(struct ieee80211com *);
static void		rum_setpromisc(struct rum_softc *);
static const char	*rum_get_rf(int);
static void		rum_read_eeprom(struct rum_softc *);
static int		rum_bbp_wakeup(struct rum_softc *);
static int		rum_bbp_init(struct rum_softc *);
static void		rum_clr_shkey_regs(struct rum_softc *);
static int		rum_init(struct rum_softc *);
static void		rum_stop(struct rum_softc *);
static void		rum_load_microcode(struct rum_softc *, const uint8_t *,
			    size_t);
static int		rum_set_sleep_time(struct rum_softc *, uint16_t);
static int		rum_reset(struct ieee80211vap *, u_long);
static int		rum_set_beacon(struct rum_softc *,
			    struct ieee80211vap *);
static int		rum_alloc_beacon(struct rum_softc *,
			    struct ieee80211vap *);
static void		rum_update_beacon_cb(struct rum_softc *,
			    union sec_param *, uint8_t);
static void		rum_update_beacon(struct ieee80211vap *, int);
static int		rum_common_key_set(struct rum_softc *,
			    struct ieee80211_key *, uint16_t);
static void		rum_group_key_set_cb(struct rum_softc *,
			    union sec_param *, uint8_t);
static void		rum_group_key_del_cb(struct rum_softc *,
			    union sec_param *, uint8_t);
static void		rum_pair_key_set_cb(struct rum_softc *,
			    union sec_param *, uint8_t);
static void		rum_pair_key_del_cb(struct rum_softc *,
			    union sec_param *, uint8_t);
static int		rum_key_alloc(struct ieee80211vap *,
			    struct ieee80211_key *, ieee80211_keyix *,
			    ieee80211_keyix *);
static int		rum_key_set(struct ieee80211vap *,
			    const struct ieee80211_key *);
static int		rum_key_delete(struct ieee80211vap *,
			    const struct ieee80211_key *);
static int		rum_raw_xmit(struct ieee80211_node *, struct mbuf *,
			    const struct ieee80211_bpf_params *);
static void		rum_scan_start(struct ieee80211com *);
static void		rum_scan_end(struct ieee80211com *);
static void		rum_set_channel(struct ieee80211com *);
static void		rum_getradiocaps(struct ieee80211com *, int, int *,
			    struct ieee80211_channel[]);
static int		rum_get_rssi(struct rum_softc *, uint8_t);
static void		rum_ratectl_start(struct rum_softc *,
			    struct ieee80211_node *);
static void		rum_ratectl_timeout(void *);
static void		rum_ratectl_task(void *, int);
static int		rum_pause(struct rum_softc *, int);

static const struct {
	uint32_t	reg;
	uint32_t	val;
} rum_def_mac[] = {
	{ RT2573_TXRX_CSR0,  0x025fb032 },
	{ RT2573_TXRX_CSR1,  0x9eaa9eaf },
	{ RT2573_TXRX_CSR2,  0x8a8b8c8d }, 
	{ RT2573_TXRX_CSR3,  0x00858687 },
	{ RT2573_TXRX_CSR7,  0x2e31353b },
	{ RT2573_TXRX_CSR8,  0x2a2a2a2c },
	{ RT2573_TXRX_CSR15, 0x0000000f },
	{ RT2573_MAC_CSR6,   0x00000fff },
	{ RT2573_MAC_CSR8,   0x016c030a },
	{ RT2573_MAC_CSR10,  0x00000718 },
	{ RT2573_MAC_CSR12,  0x00000004 },
	{ RT2573_MAC_CSR13,  0x00007f00 },
	{ RT2573_SEC_CSR2,   0x00000000 },
	{ RT2573_SEC_CSR3,   0x00000000 },
	{ RT2573_SEC_CSR4,   0x00000000 },
	{ RT2573_PHY_CSR1,   0x000023b0 },
	{ RT2573_PHY_CSR5,   0x00040a06 },
	{ RT2573_PHY_CSR6,   0x00080606 },
	{ RT2573_PHY_CSR7,   0x00000408 },
	{ RT2573_AIFSN_CSR,  0x00002273 },
	{ RT2573_CWMIN_CSR,  0x00002344 },
	{ RT2573_CWMAX_CSR,  0x000034aa }
};

static const struct {
	uint8_t	reg;
	uint8_t	val;
} rum_def_bbp[] = {
	{   3, 0x80 },
	{  15, 0x30 },
	{  17, 0x20 },
	{  21, 0xc8 },
	{  22, 0x38 },
	{  23, 0x06 },
	{  24, 0xfe },
	{  25, 0x0a },
	{  26, 0x0d },
	{  32, 0x0b },
	{  34, 0x12 },
	{  37, 0x07 },
	{  39, 0xf8 },
	{  41, 0x60 },
	{  53, 0x10 },
	{  54, 0x18 },
	{  60, 0x10 },
	{  61, 0x04 },
	{  62, 0x04 },
	{  75, 0xfe },
	{  86, 0xfe },
	{  88, 0xfe },
	{  90, 0x0f },
	{  99, 0x00 },
	{ 102, 0x16 },
	{ 107, 0x04 }
};

static const uint8_t rum_chan_5ghz[] =
	{ 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
	  100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
	  149, 153, 157, 161, 165 };

static const struct rfprog {
	uint8_t		chan;
	uint32_t	r1, r2, r3, r4;
}  rum_rf5226[] = {
	{   1, 0x00b03, 0x001e1, 0x1a014, 0x30282 },
	{   2, 0x00b03, 0x001e1, 0x1a014, 0x30287 },
	{   3, 0x00b03, 0x001e2, 0x1a014, 0x30282 },
	{   4, 0x00b03, 0x001e2, 0x1a014, 0x30287 },
	{   5, 0x00b03, 0x001e3, 0x1a014, 0x30282 },
	{   6, 0x00b03, 0x001e3, 0x1a014, 0x30287 },
	{   7, 0x00b03, 0x001e4, 0x1a014, 0x30282 },
	{   8, 0x00b03, 0x001e4, 0x1a014, 0x30287 },
	{   9, 0x00b03, 0x001e5, 0x1a014, 0x30282 },
	{  10, 0x00b03, 0x001e5, 0x1a014, 0x30287 },
	{  11, 0x00b03, 0x001e6, 0x1a014, 0x30282 },
	{  12, 0x00b03, 0x001e6, 0x1a014, 0x30287 },
	{  13, 0x00b03, 0x001e7, 0x1a014, 0x30282 },
	{  14, 0x00b03, 0x001e8, 0x1a014, 0x30284 },

	{  34, 0x00b03, 0x20266, 0x36014, 0x30282 },
	{  38, 0x00b03, 0x20267, 0x36014, 0x30284 },
	{  42, 0x00b03, 0x20268, 0x36014, 0x30286 },
	{  46, 0x00b03, 0x20269, 0x36014, 0x30288 },

	{  36, 0x00b03, 0x00266, 0x26014, 0x30288 },
	{  40, 0x00b03, 0x00268, 0x26014, 0x30280 },
	{  44, 0x00b03, 0x00269, 0x26014, 0x30282 },
	{  48, 0x00b03, 0x0026a, 0x26014, 0x30284 },
	{  52, 0x00b03, 0x0026b, 0x26014, 0x30286 },
	{  56, 0x00b03, 0x0026c, 0x26014, 0x30288 },
	{  60, 0x00b03, 0x0026e, 0x26014, 0x30280 },
	{  64, 0x00b03, 0x0026f, 0x26014, 0x30282 },

	{ 100, 0x00b03, 0x0028a, 0x2e014, 0x30280 },
	{ 104, 0x00b03, 0x0028b, 0x2e014, 0x30282 },
	{ 108, 0x00b03, 0x0028c, 0x2e014, 0x30284 },
	{ 112, 0x00b03, 0x0028d, 0x2e014, 0x30286 },
	{ 116, 0x00b03, 0x0028e, 0x2e014, 0x30288 },
	{ 120, 0x00b03, 0x002a0, 0x2e014, 0x30280 },
	{ 124, 0x00b03, 0x002a1, 0x2e014, 0x30282 },
	{ 128, 0x00b03, 0x002a2, 0x2e014, 0x30284 },
	{ 132, 0x00b03, 0x002a3, 0x2e014, 0x30286 },
	{ 136, 0x00b03, 0x002a4, 0x2e014, 0x30288 },
	{ 140, 0x00b03, 0x002a6, 0x2e014, 0x30280 },

	{ 149, 0x00b03, 0x002a8, 0x2e014, 0x30287 },
	{ 153, 0x00b03, 0x002a9, 0x2e014, 0x30289 },
	{ 157, 0x00b03, 0x002ab, 0x2e014, 0x30281 },
	{ 161, 0x00b03, 0x002ac, 0x2e014, 0x30283 },
	{ 165, 0x00b03, 0x002ad, 0x2e014, 0x30285 }
}, rum_rf5225[] = {
	{   1, 0x00b33, 0x011e1, 0x1a014, 0x30282 },
	{   2, 0x00b33, 0x011e1, 0x1a014, 0x30287 },
	{   3, 0x00b33, 0x011e2, 0x1a014, 0x30282 },
	{   4, 0x00b33, 0x011e2, 0x1a014, 0x30287 },
	{   5, 0x00b33, 0x011e3, 0x1a014, 0x30282 },
	{   6, 0x00b33, 0x011e3, 0x1a014, 0x30287 },
	{   7, 0x00b33, 0x011e4, 0x1a014, 0x30282 },
	{   8, 0x00b33, 0x011e4, 0x1a014, 0x30287 },
	{   9, 0x00b33, 0x011e5, 0x1a014, 0x30282 },
	{  10, 0x00b33, 0x011e5, 0x1a014, 0x30287 },
	{  11, 0x00b33, 0x011e6, 0x1a014, 0x30282 },
	{  12, 0x00b33, 0x011e6, 0x1a014, 0x30287 },
	{  13, 0x00b33, 0x011e7, 0x1a014, 0x30282 },
	{  14, 0x00b33, 0x011e8, 0x1a014, 0x30284 },

	{  34, 0x00b33, 0x01266, 0x26014, 0x30282 },
	{  38, 0x00b33, 0x01267, 0x26014, 0x30284 },
	{  42, 0x00b33, 0x01268, 0x26014, 0x30286 },
	{  46, 0x00b33, 0x01269, 0x26014, 0x30288 },

	{  36, 0x00b33, 0x01266, 0x26014, 0x30288 },
	{  40, 0x00b33, 0x01268, 0x26014, 0x30280 },
	{  44, 0x00b33, 0x01269, 0x26014, 0x30282 },
	{  48, 0x00b33, 0x0126a, 0x26014, 0x30284 },
	{  52, 0x00b33, 0x0126b, 0x26014, 0x30286 },
	{  56, 0x00b33, 0x0126c, 0x26014, 0x30288 },
	{  60, 0x00b33, 0x0126e, 0x26014, 0x30280 },
	{  64, 0x00b33, 0x0126f, 0x26014, 0x30282 },

	{ 100, 0x00b33, 0x0128a, 0x2e014, 0x30280 },
	{ 104, 0x00b33, 0x0128b, 0x2e014, 0x30282 },
	{ 108, 0x00b33, 0x0128c, 0x2e014, 0x30284 },
	{ 112, 0x00b33, 0x0128d, 0x2e014, 0x30286 },
	{ 116, 0x00b33, 0x0128e, 0x2e014, 0x30288 },
	{ 120, 0x00b33, 0x012a0, 0x2e014, 0x30280 },
	{ 124, 0x00b33, 0x012a1, 0x2e014, 0x30282 },
	{ 128, 0x00b33, 0x012a2, 0x2e014, 0x30284 },
	{ 132, 0x00b33, 0x012a3, 0x2e014, 0x30286 },
	{ 136, 0x00b33, 0x012a4, 0x2e014, 0x30288 },
	{ 140, 0x00b33, 0x012a6, 0x2e014, 0x30280 },

	{ 149, 0x00b33, 0x012a8, 0x2e014, 0x30287 },
	{ 153, 0x00b33, 0x012a9, 0x2e014, 0x30289 },
	{ 157, 0x00b33, 0x012ab, 0x2e014, 0x30281 },
	{ 161, 0x00b33, 0x012ac, 0x2e014, 0x30283 },
	{ 165, 0x00b33, 0x012ad, 0x2e014, 0x30285 }
};

static const struct usb_config rum_config[RUM_N_TRANSFER] = {
	[RUM_BULK_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = (MCLBYTES + RT2573_TX_DESC_SIZE + 8),
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = rum_bulk_write_callback,
		.timeout = 5000,	/* ms */
	},
	[RUM_BULK_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = (MCLBYTES + RT2573_RX_DESC_SIZE),
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = rum_bulk_read_callback,
	},
};

static int
rum_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != 0)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != RT2573_IFACE_INDEX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(rum_devs, sizeof(rum_devs), uaa));
}

static int
rum_attach(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct rum_softc *sc = device_get_softc(self);
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	uint8_t iface_index;
	int error, ntries;

	device_set_usb_desc(self);
	sc->sc_udev = uaa->device;
	sc->sc_dev = self;

	RUM_LOCK_INIT(sc);
	RUM_CMDQ_LOCK_INIT(sc);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	iface_index = RT2573_IFACE_INDEX;
	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, rum_config, RUM_N_TRANSFER, sc, &sc->sc_mtx);
	if (error) {
		device_printf(self, "could not allocate USB transfers, "
		    "err=%s\n", usbd_errstr(error));
		goto detach;
	}

	RUM_LOCK(sc);
	/* retrieve RT2573 rev. no */
	for (ntries = 0; ntries < 100; ntries++) {
		if ((tmp = rum_read(sc, RT2573_MAC_CSR0)) != 0)
			break;
		if (rum_pause(sc, hz / 100))
			break;
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev, "timeout waiting for chip to settle\n");
		RUM_UNLOCK(sc);
		goto detach;
	}

	/* retrieve MAC address and various other things from EEPROM */
	rum_read_eeprom(sc);

	device_printf(sc->sc_dev, "MAC/BBP RT2573 (rev 0x%05x), RF %s\n",
	    tmp, rum_get_rf(sc->rf_rev));

	rum_load_microcode(sc, rt2573_ucode, sizeof(rt2573_ucode));
	RUM_UNLOCK(sc);

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(self);
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */

	/* set device capabilities */
	ic->ic_caps =
	      IEEE80211_C_STA		/* station mode supported */
	    | IEEE80211_C_IBSS		/* IBSS mode supported */
	    | IEEE80211_C_MONITOR	/* monitor mode supported */
	    | IEEE80211_C_HOSTAP	/* HostAp mode supported */
	    | IEEE80211_C_AHDEMO	/* adhoc demo mode */
	    | IEEE80211_C_TXPMGT	/* tx power management */
	    | IEEE80211_C_SHPREAMBLE	/* short preamble supported */
	    | IEEE80211_C_SHSLOT	/* short slot time supported */
	    | IEEE80211_C_BGSCAN	/* bg scanning supported */
	    | IEEE80211_C_WPA		/* 802.11i */
	    | IEEE80211_C_WME		/* 802.11e */
	    | IEEE80211_C_PMGT		/* Station-side power mgmt */
	    | IEEE80211_C_SWSLEEP	/* net80211 managed power mgmt */
	    ;

	ic->ic_cryptocaps =
	    IEEE80211_CRYPTO_WEP |
	    IEEE80211_CRYPTO_AES_CCM |
	    IEEE80211_CRYPTO_TKIPMIC |
	    IEEE80211_CRYPTO_TKIP;

	rum_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	ieee80211_ifattach(ic);
	ic->ic_update_promisc = rum_update_promisc;
	ic->ic_raw_xmit = rum_raw_xmit;
	ic->ic_scan_start = rum_scan_start;
	ic->ic_scan_end = rum_scan_end;
	ic->ic_set_channel = rum_set_channel;
	ic->ic_getradiocaps = rum_getradiocaps;
	ic->ic_transmit = rum_transmit;
	ic->ic_parent = rum_parent;
	ic->ic_vap_create = rum_vap_create;
	ic->ic_vap_delete = rum_vap_delete;
	ic->ic_updateslot = rum_update_slot;
	ic->ic_wme.wme_update = rum_wme_update;
	ic->ic_update_mcast = rum_update_mcast;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_txtap.wt_ihdr, sizeof(sc->sc_txtap),
		RT2573_TX_RADIOTAP_PRESENT,
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
		RT2573_RX_RADIOTAP_PRESENT);

	TASK_INIT(&sc->cmdq_task, 0, rum_cmdq_cb, sc);

	if (bootverbose)
		ieee80211_announce(ic);

	return (0);

detach:
	rum_detach(self);
	return (ENXIO);			/* failure */
}

static int
rum_detach(device_t self)
{
	struct rum_softc *sc = device_get_softc(self);
	struct ieee80211com *ic = &sc->sc_ic;

	/* Prevent further ioctls */
	RUM_LOCK(sc);
	sc->sc_detached = 1;
	RUM_UNLOCK(sc);

	/* stop all USB transfers */
	usbd_transfer_unsetup(sc->sc_xfer, RUM_N_TRANSFER);

	/* free TX list, if any */
	RUM_LOCK(sc);
	rum_unsetup_tx_list(sc);
	RUM_UNLOCK(sc);

	if (ic->ic_softc == sc) {
		ieee80211_draintask(ic, &sc->cmdq_task);
		ieee80211_ifdetach(ic);
	}

	mbufq_drain(&sc->sc_snd);
	RUM_CMDQ_LOCK_DESTROY(sc);
	RUM_LOCK_DESTROY(sc);

	return (0);
}

static usb_error_t
rum_do_request(struct rum_softc *sc,
    struct usb_device_request *req, void *data)
{
	usb_error_t err;
	int ntries = 10;

	while (ntries--) {
		err = usbd_do_request_flags(sc->sc_udev, &sc->sc_mtx,
		    req, data, 0, NULL, 250 /* ms */);
		if (err == 0)
			break;

		DPRINTFN(1, "Control request failed, %s (retrying)\n",
		    usbd_errstr(err));
		if (rum_pause(sc, hz / 100))
			break;
	}
	return (err);
}

static usb_error_t
rum_do_mcu_request(struct rum_softc *sc, int request)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2573_MCU_CNTL;
	USETW(req.wValue, request);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	return (rum_do_request(sc, &req, NULL));
}

static struct ieee80211vap *
rum_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct rum_softc *sc = ic->ic_softc;
	struct rum_vap *rvp;
	struct ieee80211vap *vap;

	if (!TAILQ_EMPTY(&ic->ic_vaps))		/* only one at a time */
		return NULL;
	rvp = malloc(sizeof(struct rum_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &rvp->vap;
	/* enable s/w bmiss handling for sta mode */

	if (ieee80211_vap_setup(ic, vap, name, unit, opmode,
	    flags | IEEE80211_CLONE_NOBEACONS, bssid) != 0) {
		/* out of memory */
		free(rvp, M_80211_VAP);
		return (NULL);
	}

	/* override state transition machine */
	rvp->newstate = vap->iv_newstate;
	vap->iv_newstate = rum_newstate;
	vap->iv_key_alloc = rum_key_alloc;
	vap->iv_key_set = rum_key_set;
	vap->iv_key_delete = rum_key_delete;
	vap->iv_update_beacon = rum_update_beacon;
	vap->iv_reset = rum_reset;
	vap->iv_max_aid = RT2573_ADDR_MAX;

	if (opmode == IEEE80211_M_STA) {
		/*
		 * Move device to the sleep state when
		 * beacon is received and there is no data for us.
		 *
		 * Used only for IEEE80211_S_SLEEP state.
		 */
		rvp->recv_mgmt = vap->iv_recv_mgmt;
		vap->iv_recv_mgmt = rum_sta_recv_mgmt;

		/* Ignored while sleeping. */
		rvp->bmiss = vap->iv_bmiss;
		vap->iv_bmiss = rum_beacon_miss;
	}

	usb_callout_init_mtx(&rvp->ratectl_ch, &sc->sc_mtx, 0);
	TASK_INIT(&rvp->ratectl_task, 0, rum_ratectl_task, rvp);
	ieee80211_ratectl_init(vap);
	ieee80211_ratectl_setinterval(vap, 1000 /* 1 sec */);
	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change,
	    ieee80211_media_status, mac);
	ic->ic_opmode = opmode;
	return vap;
}

static void
rum_vap_delete(struct ieee80211vap *vap)
{
	struct rum_vap *rvp = RUM_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct rum_softc *sc = ic->ic_softc;

	/* Put vap into INIT state. */
	ieee80211_new_state(vap, IEEE80211_S_INIT, -1);
	ieee80211_draintask(ic, &vap->iv_nstate_task);

	RUM_LOCK(sc);
	/* Cancel any unfinished Tx. */
	rum_reset_tx_list(sc, vap);
	RUM_UNLOCK(sc);

	usb_callout_drain(&rvp->ratectl_ch);
	ieee80211_draintask(ic, &rvp->ratectl_task);
	ieee80211_ratectl_deinit(vap);
	ieee80211_vap_detach(vap);
	m_freem(rvp->bcn_mbuf);
	free(rvp, M_80211_VAP);
}

static void
rum_cmdq_cb(void *arg, int pending)
{
	struct rum_softc *sc = arg;
	struct rum_cmdq *rc;

	RUM_CMDQ_LOCK(sc);
	while (sc->cmdq[sc->cmdq_first].func != NULL) {
		rc = &sc->cmdq[sc->cmdq_first];
		RUM_CMDQ_UNLOCK(sc);

		RUM_LOCK(sc);
		rc->func(sc, &rc->data, rc->rvp_id);
		RUM_UNLOCK(sc);

		RUM_CMDQ_LOCK(sc);
		memset(rc, 0, sizeof (*rc));
		sc->cmdq_first = (sc->cmdq_first + 1) % RUM_CMDQ_SIZE;
	}
	RUM_CMDQ_UNLOCK(sc);
}

static int
rum_cmd_sleepable(struct rum_softc *sc, const void *ptr, size_t len,
    uint8_t rvp_id, CMD_FUNC_PROTO)
{
	struct ieee80211com *ic = &sc->sc_ic;

	KASSERT(len <= sizeof(union sec_param), ("buffer overflow"));

	RUM_CMDQ_LOCK(sc);
	if (sc->cmdq[sc->cmdq_last].func != NULL) {
		device_printf(sc->sc_dev, "%s: cmdq overflow\n", __func__);
		RUM_CMDQ_UNLOCK(sc);

		return EAGAIN;
	}

	if (ptr != NULL)
		memcpy(&sc->cmdq[sc->cmdq_last].data, ptr, len);
	sc->cmdq[sc->cmdq_last].rvp_id = rvp_id;
	sc->cmdq[sc->cmdq_last].func = func;
	sc->cmdq_last = (sc->cmdq_last + 1) % RUM_CMDQ_SIZE;
	RUM_CMDQ_UNLOCK(sc);

	ieee80211_runtask(ic, &sc->cmdq_task);

	return 0;
}

static void
rum_tx_free(struct rum_tx_data *data, int txerr)
{
	struct rum_softc *sc = data->sc;

	if (data->m != NULL) {
		ieee80211_tx_complete(data->ni, data->m, txerr);
		data->m = NULL;
		data->ni = NULL;
	}
	STAILQ_INSERT_TAIL(&sc->tx_free, data, next);
	sc->tx_nfree++;
}

static void
rum_setup_tx_list(struct rum_softc *sc)
{
	struct rum_tx_data *data;
	int i;

	sc->tx_nfree = 0;
	STAILQ_INIT(&sc->tx_q);
	STAILQ_INIT(&sc->tx_free);

	for (i = 0; i < RUM_TX_LIST_COUNT; i++) {
		data = &sc->tx_data[i];

		data->sc = sc;
		STAILQ_INSERT_TAIL(&sc->tx_free, data, next);
		sc->tx_nfree++;
	}
}

static void
rum_reset_tx_list(struct rum_softc *sc, struct ieee80211vap *vap)
{
	struct rum_tx_data *data, *tmp;

	KASSERT(vap != NULL, ("%s: vap is NULL\n", __func__));

	STAILQ_FOREACH_SAFE(data, &sc->tx_q, next, tmp) {
		if (data->ni != NULL && data->ni->ni_vap == vap) {
			ieee80211_free_node(data->ni);
			data->ni = NULL;

			KASSERT(data->m != NULL, ("%s: m is NULL\n",
			    __func__));
			m_freem(data->m);
			data->m = NULL;

			STAILQ_REMOVE(&sc->tx_q, data, rum_tx_data, next);
			STAILQ_INSERT_TAIL(&sc->tx_free, data, next);
			sc->tx_nfree++;
		}
	}
}

static void
rum_unsetup_tx_list(struct rum_softc *sc)
{
	struct rum_tx_data *data;
	int i;

	/* make sure any subsequent use of the queues will fail */
	sc->tx_nfree = 0;
	STAILQ_INIT(&sc->tx_q);
	STAILQ_INIT(&sc->tx_free);

	/* free up all node references and mbufs */
	for (i = 0; i < RUM_TX_LIST_COUNT; i++) {
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

static void
rum_beacon_miss(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct rum_softc *sc = ic->ic_softc;
	struct rum_vap *rvp = RUM_VAP(vap);
	int sleep;

	RUM_LOCK(sc);
	if (sc->sc_sleeping && sc->sc_sleep_end < ticks) {
		DPRINTFN(12, "dropping 'sleeping' bit, "
		    "device must be awake now\n");

		sc->sc_sleeping = 0;
	}

	sleep = sc->sc_sleeping;
	RUM_UNLOCK(sc);

	if (!sleep)
		rvp->bmiss(vap);
#ifdef USB_DEBUG
	else
		DPRINTFN(13, "bmiss event is ignored whilst sleeping\n");
#endif
}

static void
rum_sta_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m, int subtype,
    const struct ieee80211_rx_stats *rxs,
    int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct rum_softc *sc = vap->iv_ic->ic_softc;
	struct rum_vap *rvp = RUM_VAP(vap);

	if (vap->iv_state == IEEE80211_S_SLEEP &&
	    subtype == IEEE80211_FC0_SUBTYPE_BEACON) {
		RUM_LOCK(sc);
		DPRINTFN(12, "beacon, mybss %d (flags %02X)\n",
		    !!(sc->last_rx_flags & RT2573_RX_MYBSS),
		    sc->last_rx_flags);

		if ((sc->last_rx_flags & (RT2573_RX_MYBSS | RT2573_RX_BC)) ==
		    (RT2573_RX_MYBSS | RT2573_RX_BC)) {
			/*
			 * Put it to sleep here; in case if there is a data
			 * for us, iv_recv_mgmt() will wakeup the device via
			 * SLEEP -> RUN state transition.
			 */
			rum_set_power_state(sc, 1);
		}
		RUM_UNLOCK(sc);
	}

	rvp->recv_mgmt(ni, m, subtype, rxs, rssi, nf);
}

static int
rum_set_power_state(struct rum_softc *sc, int sleep)
{
	usb_error_t uerror;

	RUM_LOCK_ASSERT(sc);

	DPRINTFN(12, "moving to %s state (sleep time %u)\n",
	    sleep ? "sleep" : "awake", sc->sc_sleep_time);

	uerror = rum_do_mcu_request(sc,
	    sleep ? RT2573_MCU_SLEEP : RT2573_MCU_WAKEUP);
	if (uerror != USB_ERR_NORMAL_COMPLETION) {
		device_printf(sc->sc_dev,
		    "%s: could not change power state: %s\n",
		    __func__, usbd_errstr(uerror));
		return (EIO);
	}

	sc->sc_sleeping = !!sleep;
	sc->sc_sleep_end = sleep ? ticks + sc->sc_sleep_time : 0;

	return (0);
}

static int
rum_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct rum_vap *rvp = RUM_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct rum_softc *sc = ic->ic_softc;
	const struct ieee80211_txparam *tp;
	enum ieee80211_state ostate;
	struct ieee80211_node *ni;
	usb_error_t uerror;
	int ret = 0;

	ostate = vap->iv_state;
	DPRINTF("%s -> %s\n",
		ieee80211_state_name[ostate],
		ieee80211_state_name[nstate]);

	IEEE80211_UNLOCK(ic);
	RUM_LOCK(sc);
	usb_callout_stop(&rvp->ratectl_ch);

	if (ostate == IEEE80211_S_SLEEP && vap->iv_opmode == IEEE80211_M_STA) {
		rum_clrbits(sc, RT2573_TXRX_CSR4, RT2573_ACKCTS_PWRMGT);
		rum_clrbits(sc, RT2573_MAC_CSR11, RT2573_AUTO_WAKEUP);

		/*
		 * Ignore any errors;
		 * any subsequent TX will wakeup it anyway
		 */
		(void) rum_set_power_state(sc, 0);
	}

	switch (nstate) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN)
			rum_abort_tsf_sync(sc);

		break;

	case IEEE80211_S_RUN:
		if (ostate == IEEE80211_S_SLEEP)
			break;		/* already handled */

		ni = ieee80211_ref_node(vap->iv_bss);

		if (vap->iv_opmode != IEEE80211_M_MONITOR) {
			if (ic->ic_bsschan == IEEE80211_CHAN_ANYC ||
			    ni->ni_chan == IEEE80211_CHAN_ANYC) {
				ret = EINVAL;
				goto run_fail;
			}
			rum_update_slot_cb(sc, NULL, 0);
			rum_enable_mrr(sc);
			rum_set_txpreamble(sc);
			rum_set_basicrates(sc);
			rum_set_maxretry(sc, vap);
			IEEE80211_ADDR_COPY(sc->sc_bssid, ni->ni_bssid);
			rum_set_bssid(sc, sc->sc_bssid);
		}

		if (vap->iv_opmode == IEEE80211_M_HOSTAP ||
		    vap->iv_opmode == IEEE80211_M_IBSS) {
			if ((ret = rum_alloc_beacon(sc, vap)) != 0)
				goto run_fail;
		}

		if (vap->iv_opmode != IEEE80211_M_MONITOR &&
		    vap->iv_opmode != IEEE80211_M_AHDEMO) {
			if ((ret = rum_enable_tsf_sync(sc)) != 0)
				goto run_fail;
		} else
			rum_enable_tsf(sc);

		/* enable automatic rate adaptation */
		tp = &vap->iv_txparms[ieee80211_chan2mode(ic->ic_curchan)];
		if (tp->ucastrate == IEEE80211_FIXED_RATE_NONE)
			rum_ratectl_start(sc, ni);
run_fail:
		ieee80211_free_node(ni);
		break;
	case IEEE80211_S_SLEEP:
		/* Implemented for STA mode only. */
		if (vap->iv_opmode != IEEE80211_M_STA)
			break;

		uerror = rum_setbits(sc, RT2573_MAC_CSR11, RT2573_AUTO_WAKEUP);
		if (uerror != USB_ERR_NORMAL_COMPLETION) {
			ret = EIO;
			break;
		}

		uerror = rum_setbits(sc, RT2573_TXRX_CSR4, RT2573_ACKCTS_PWRMGT);
		if (uerror != USB_ERR_NORMAL_COMPLETION) {
			ret = EIO;
			break;
		}

		ret = rum_set_power_state(sc, 1);
		if (ret != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not move to the SLEEP state: %s\n",
			    __func__, usbd_errstr(uerror));
		}
		break;
	default:
		break;
	}
	RUM_UNLOCK(sc);
	IEEE80211_LOCK(ic);
	return (ret == 0 ? rvp->newstate(vap, nstate, arg) : ret);
}

static void
rum_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct rum_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211vap *vap;
	struct rum_tx_data *data;
	struct mbuf *m;
	struct usb_page_cache *pc;
	unsigned int len;
	int actlen, sumlen;

	usbd_xfer_status(xfer, &actlen, &sumlen, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer complete, %d bytes\n", actlen);

		/* free resources */
		data = usbd_xfer_get_priv(xfer);
		rum_tx_free(data, 0);
		usbd_xfer_set_priv(xfer, NULL);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		data = STAILQ_FIRST(&sc->tx_q);
		if (data) {
			STAILQ_REMOVE_HEAD(&sc->tx_q, next);
			m = data->m;

			if (m->m_pkthdr.len > (int)(MCLBYTES + RT2573_TX_DESC_SIZE)) {
				DPRINTFN(0, "data overflow, %u bytes\n",
				    m->m_pkthdr.len);
				m->m_pkthdr.len = (MCLBYTES + RT2573_TX_DESC_SIZE);
			}
			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_in(pc, 0, &data->desc, RT2573_TX_DESC_SIZE);
			usbd_m_copy_in(pc, RT2573_TX_DESC_SIZE, m, 0,
			    m->m_pkthdr.len);

			vap = data->ni->ni_vap;
			if (ieee80211_radiotap_active_vap(vap)) {
				struct rum_tx_radiotap_header *tap = &sc->sc_txtap;

				tap->wt_flags = 0;
				tap->wt_rate = data->rate;
				tap->wt_antenna = sc->tx_ant;

				ieee80211_radiotap_tx(vap, m);
			}

			/* align end on a 4-bytes boundary */
			len = (RT2573_TX_DESC_SIZE + m->m_pkthdr.len + 3) & ~3;
			if ((len % 64) == 0)
				len += 4;

			DPRINTFN(11, "sending frame len=%u xferlen=%u\n",
			    m->m_pkthdr.len, len);

			usbd_xfer_set_frame_len(xfer, 0, len);
			usbd_xfer_set_priv(xfer, data);

			usbd_transfer_submit(xfer);
		}
		rum_start(sc);
		break;

	default:			/* Error */
		DPRINTFN(11, "transfer error, %s\n",
		    usbd_errstr(error));

		counter_u64_add(sc->sc_ic.ic_oerrors, 1);
		data = usbd_xfer_get_priv(xfer);
		if (data != NULL) {
			rum_tx_free(data, error);
			usbd_xfer_set_priv(xfer, NULL);
		}

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

static void
rum_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct rum_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame_min *wh;
	struct ieee80211_node *ni;
	struct mbuf *m = NULL;
	struct usb_page_cache *pc;
	uint32_t flags;
	uint8_t rssi = 0;
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		DPRINTFN(15, "rx done, actlen=%d\n", len);

		if (len < RT2573_RX_DESC_SIZE) {
			DPRINTF("%s: xfer too short %d\n",
			    device_get_nameunit(sc->sc_dev), len);
			counter_u64_add(ic->ic_ierrors, 1);
			goto tr_setup;
		}

		len -= RT2573_RX_DESC_SIZE;
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, &sc->sc_rx_desc, RT2573_RX_DESC_SIZE);

		rssi = rum_get_rssi(sc, sc->sc_rx_desc.rssi);
		flags = le32toh(sc->sc_rx_desc.flags);
		sc->last_rx_flags = flags;
		if (len < ((flags >> 16) & 0xfff)) {
			DPRINTFN(5, "%s: frame is truncated from %d to %d "
			    "bytes\n", device_get_nameunit(sc->sc_dev),
			    (flags >> 16) & 0xfff, len);
			counter_u64_add(ic->ic_ierrors, 1);
			goto tr_setup;
		}
		len = (flags >> 16) & 0xfff;
		if (len < sizeof(struct ieee80211_frame_ack)) {
			DPRINTFN(5, "%s: frame too short %d\n",
			    device_get_nameunit(sc->sc_dev), len);
			counter_u64_add(ic->ic_ierrors, 1);
			goto tr_setup;
		}
		if (flags & RT2573_RX_CRC_ERROR) {
			/*
		         * This should not happen since we did not
		         * request to receive those frames when we
		         * filled RUM_TXRX_CSR2:
		         */
			DPRINTFN(5, "PHY or CRC error\n");
			counter_u64_add(ic->ic_ierrors, 1);
			goto tr_setup;
		}
		if ((flags & RT2573_RX_DEC_MASK) != RT2573_RX_DEC_OK) {
			switch (flags & RT2573_RX_DEC_MASK) {
			case RT2573_RX_IV_ERROR:
				DPRINTFN(5, "IV/EIV error\n");
				break;
			case RT2573_RX_MIC_ERROR:
				DPRINTFN(5, "MIC error\n");
				break;
			case RT2573_RX_KEY_ERROR:
				DPRINTFN(5, "Key error\n");
				break;
			}
			counter_u64_add(ic->ic_ierrors, 1);
			goto tr_setup;
		}

		m = m_get2(len, M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL) {
			DPRINTF("could not allocate mbuf\n");
			counter_u64_add(ic->ic_ierrors, 1);
			goto tr_setup;
		}
		usbd_copy_out(pc, RT2573_RX_DESC_SIZE,
		    mtod(m, uint8_t *), len);

		wh = mtod(m, struct ieee80211_frame_min *);

		if ((wh->i_fc[1] & IEEE80211_FC1_PROTECTED) &&
		    (flags & RT2573_RX_CIP_MASK) !=
		     RT2573_RX_CIP_MODE(RT2573_MODE_NOSEC)) {
			wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
			m->m_flags |= M_WEP;
		}

		/* finalize mbuf */
		m->m_pkthdr.len = m->m_len = len;

		if (ieee80211_radiotap_active(ic)) {
			struct rum_rx_radiotap_header *tap = &sc->sc_rxtap;

			tap->wr_flags = 0;
			tap->wr_rate = ieee80211_plcp2rate(sc->sc_rx_desc.rate,
			    (flags & RT2573_RX_OFDM) ?
			    IEEE80211_T_OFDM : IEEE80211_T_CCK);
			rum_get_tsf(sc, &tap->wr_tsf);
			tap->wr_antsignal = RT2573_NOISE_FLOOR + rssi;
			tap->wr_antnoise = RT2573_NOISE_FLOOR;
			tap->wr_antenna = sc->rx_ant;
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
		RUM_UNLOCK(sc);
		if (m) {
			if (m->m_len >= sizeof(struct ieee80211_frame_min))
				ni = ieee80211_find_rxnode(ic, wh);
			else
				ni = NULL;

			if (ni != NULL) {
				(void) ieee80211_input(ni, m, rssi,
				    RT2573_NOISE_FLOOR);
				ieee80211_free_node(ni);
			} else
				(void) ieee80211_input_all(ic, m, rssi,
				    RT2573_NOISE_FLOOR);
		}
		RUM_LOCK(sc);
		rum_start(sc);
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static uint8_t
rum_plcp_signal(int rate)
{
	switch (rate) {
	/* OFDM rates (cf IEEE Std 802.11a-1999, pp. 14 Table 80) */
	case 12:	return 0xb;
	case 18:	return 0xf;
	case 24:	return 0xa;
	case 36:	return 0xe;
	case 48:	return 0x9;
	case 72:	return 0xd;
	case 96:	return 0x8;
	case 108:	return 0xc;

	/* CCK rates (NB: not IEEE std, device-specific) */
	case 2:		return 0x0;
	case 4:		return 0x1;
	case 11:	return 0x2;
	case 22:	return 0x3;
	}
	return 0xff;		/* XXX unsupported/unknown rate */
}

/*
 * Map net80211 cipher to RT2573 security mode.
 */
static uint8_t
rum_crypto_mode(struct rum_softc *sc, u_int cipher, int keylen)
{
	switch (cipher) {
	case IEEE80211_CIPHER_WEP:
		return (keylen < 8 ? RT2573_MODE_WEP40 : RT2573_MODE_WEP104);
	case IEEE80211_CIPHER_TKIP:
		return RT2573_MODE_TKIP;
	case IEEE80211_CIPHER_AES_CCM:
		return RT2573_MODE_AES_CCMP;
	default:
		device_printf(sc->sc_dev, "unknown cipher %d\n", cipher);
		return 0;
	}
}

static void
rum_setup_tx_desc(struct rum_softc *sc, struct rum_tx_desc *desc,
    struct ieee80211_key *k, uint32_t flags, uint8_t xflags, uint8_t qid,
    int hdrlen, int len, int rate)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct wmeParams *wmep = &sc->wme_params[qid];
	uint16_t plcp_length;
	int remainder;

	flags |= RT2573_TX_VALID;
	flags |= len << 16;

	if (k != NULL && !(k->wk_flags & IEEE80211_KEY_SWCRYPT)) {
		const struct ieee80211_cipher *cip = k->wk_cipher;

		len += cip->ic_header + cip->ic_trailer + cip->ic_miclen;

		desc->eiv = 0;		/* for WEP */
		cip->ic_setiv(k, (uint8_t *)&desc->iv);
	}

	/* setup PLCP fields */
	desc->plcp_signal  = rum_plcp_signal(rate);
	desc->plcp_service = 4;

	len += IEEE80211_CRC_LEN;
	if (ieee80211_rate2phytype(ic->ic_rt, rate) == IEEE80211_T_OFDM) {
		flags |= RT2573_TX_OFDM;

		plcp_length = len & 0xfff;
		desc->plcp_length_hi = plcp_length >> 6;
		desc->plcp_length_lo = plcp_length & 0x3f;
	} else {
		if (rate == 0)
			rate = 2;	/* avoid division by zero */
		plcp_length = howmany(16 * len, rate);
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

	desc->flags = htole32(flags);
	desc->hdrlen = hdrlen;
	desc->xflags = xflags;

	desc->wme = htole16(RT2573_QID(qid) |
	    RT2573_AIFSN(wmep->wmep_aifsn) |
	    RT2573_LOGCWMIN(wmep->wmep_logcwmin) |
	    RT2573_LOGCWMAX(wmep->wmep_logcwmax));
}

static int
rum_sendprot(struct rum_softc *sc,
    const struct mbuf *m, struct ieee80211_node *ni, int prot, int rate)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct rum_tx_data *data;
	struct mbuf *mprot;
	int protrate, flags;

	RUM_LOCK_ASSERT(sc);

	mprot = ieee80211_alloc_prot(ni, m, rate, prot);
	if (mprot == NULL) {
		if_inc_counter(ni->ni_vap->iv_ifp, IFCOUNTER_OERRORS, 1);
		device_printf(sc->sc_dev,
		    "could not allocate mbuf for protection mode %d\n", prot);
		return (ENOBUFS);
	}

	protrate = ieee80211_ctl_rate(ic->ic_rt, rate);
	flags = 0;
	if (prot == IEEE80211_PROT_RTSCTS)
		flags |= RT2573_TX_NEED_ACK;

	data = STAILQ_FIRST(&sc->tx_free);
	STAILQ_REMOVE_HEAD(&sc->tx_free, next);
	sc->tx_nfree--;

	data->m = mprot;
	data->ni = ieee80211_ref_node(ni);
	data->rate = protrate;
	rum_setup_tx_desc(sc, &data->desc, NULL, flags, 0, 0, 0,
	    mprot->m_pkthdr.len, protrate);

	STAILQ_INSERT_TAIL(&sc->tx_q, data, next);
	usbd_transfer_start(sc->sc_xfer[RUM_BULK_WR]);

	return 0;
}

static uint32_t
rum_tx_crypto_flags(struct rum_softc *sc, struct ieee80211_node *ni, 
    const struct ieee80211_key *k)
{
	struct ieee80211vap *vap = ni->ni_vap;
	u_int cipher;
	uint32_t flags = 0;
	uint8_t mode, pos;

	if (!(k->wk_flags & IEEE80211_KEY_SWCRYPT)) {
		cipher = k->wk_cipher->ic_cipher;
		pos = k->wk_keyix;
		mode = rum_crypto_mode(sc, cipher, k->wk_keylen);
		if (mode == 0)
			return 0;

		flags |= RT2573_TX_CIP_MODE(mode);

		/* Do not trust GROUP flag */
		if (!(k >= &vap->iv_nw_keys[0] &&
		      k < &vap->iv_nw_keys[IEEE80211_WEP_NKID]))
			flags |= RT2573_TX_KEY_PAIR;
		else
			pos += 0 * RT2573_SKEY_MAX;	/* vap id */

		flags |= RT2573_TX_KEY_ID(pos);

		if (cipher == IEEE80211_CIPHER_TKIP)
			flags |= RT2573_TX_TKIPMIC;
	}

	return flags;
}

static int
rum_tx_mgt(struct rum_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct ieee80211com *ic = &sc->sc_ic;
	struct rum_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	uint32_t flags = 0;
	uint16_t dur;
	uint8_t ac, type, xflags = 0;
	int hdrlen;

	RUM_LOCK_ASSERT(sc);

	data = STAILQ_FIRST(&sc->tx_free);
	STAILQ_REMOVE_HEAD(&sc->tx_free, next);
	sc->tx_nfree--;

	wh = mtod(m0, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	hdrlen = ieee80211_anyhdrsize(wh);
	ac = M_WME_GETAC(m0);

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_get_txkey(ni, m0);
		if (k == NULL)
			return (ENOENT);

		if ((k->wk_flags & IEEE80211_KEY_SWCRYPT) &&
		    !k->wk_cipher->ic_encap(k, m0))
			return (ENOBUFS);

		wh = mtod(m0, struct ieee80211_frame *);
	}

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= RT2573_TX_NEED_ACK;

		dur = ieee80211_ack_duration(ic->ic_rt, tp->mgmtrate, 
		    ic->ic_flags & IEEE80211_F_SHPREAMBLE);
		USETW(wh->i_dur, dur);

		/* tell hardware to add timestamp for probe responses */
		if (type == IEEE80211_FC0_TYPE_MGT &&
		    (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
		    IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			flags |= RT2573_TX_TIMESTAMP;
	}

	if (type != IEEE80211_FC0_TYPE_CTL && !IEEE80211_QOS_HAS_SEQ(wh))
		xflags |= RT2573_TX_HWSEQ;

	if (k != NULL)
		flags |= rum_tx_crypto_flags(sc, ni, k);

	data->m = m0;
	data->ni = ni;
	data->rate = tp->mgmtrate;

	rum_setup_tx_desc(sc, &data->desc, k, flags, xflags, ac, hdrlen,
	    m0->m_pkthdr.len, tp->mgmtrate);

	DPRINTFN(10, "sending mgt frame len=%d rate=%d\n",
	    m0->m_pkthdr.len + (int)RT2573_TX_DESC_SIZE, tp->mgmtrate);

	STAILQ_INSERT_TAIL(&sc->tx_q, data, next);
	usbd_transfer_start(sc->sc_xfer[RUM_BULK_WR]);

	return (0);
}

static int
rum_tx_raw(struct rum_softc *sc, struct mbuf *m0, struct ieee80211_node *ni,
    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_frame *wh;
	struct rum_tx_data *data;
	uint32_t flags;
	uint8_t ac, type, xflags = 0;
	int rate, error;

	RUM_LOCK_ASSERT(sc);

	wh = mtod(m0, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	ac = params->ibp_pri & 3;

	rate = params->ibp_rate0;
	if (!ieee80211_isratevalid(ic->ic_rt, rate))
		return (EINVAL);

	flags = 0;
	if ((params->ibp_flags & IEEE80211_BPF_NOACK) == 0)
		flags |= RT2573_TX_NEED_ACK;
	if (params->ibp_flags & (IEEE80211_BPF_RTS|IEEE80211_BPF_CTS)) {
		error = rum_sendprot(sc, m0, ni,
		    params->ibp_flags & IEEE80211_BPF_RTS ?
			 IEEE80211_PROT_RTSCTS : IEEE80211_PROT_CTSONLY,
		    rate);
		if (error || sc->tx_nfree == 0)
			return (ENOBUFS);

		flags |= RT2573_TX_LONG_RETRY | RT2573_TX_IFS_SIFS;
	}

	if (type != IEEE80211_FC0_TYPE_CTL && !IEEE80211_QOS_HAS_SEQ(wh))
		xflags |= RT2573_TX_HWSEQ;

	data = STAILQ_FIRST(&sc->tx_free);
	STAILQ_REMOVE_HEAD(&sc->tx_free, next);
	sc->tx_nfree--;

	data->m = m0;
	data->ni = ni;
	data->rate = rate;

	/* XXX need to setup descriptor ourself */
	rum_setup_tx_desc(sc, &data->desc, NULL, flags, xflags, ac, 0,
	    m0->m_pkthdr.len, rate);

	DPRINTFN(10, "sending raw frame len=%u rate=%u\n",
	    m0->m_pkthdr.len, rate);

	STAILQ_INSERT_TAIL(&sc->tx_q, data, next);
	usbd_transfer_start(sc->sc_xfer[RUM_BULK_WR]);

	return 0;
}

static int
rum_tx_data(struct rum_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = &sc->sc_ic;
	struct rum_tx_data *data;
	struct ieee80211_frame *wh;
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct ieee80211_key *k = NULL;
	uint32_t flags = 0;
	uint16_t dur;
	uint8_t ac, type, qos, xflags = 0;
	int error, hdrlen, rate;

	RUM_LOCK_ASSERT(sc);

	wh = mtod(m0, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	hdrlen = ieee80211_anyhdrsize(wh);

	if (IEEE80211_QOS_HAS_SEQ(wh))
		qos = ((const struct ieee80211_qosframe *)wh)->i_qos[0];
	else
		qos = 0;
	ac = M_WME_GETAC(m0);

	if (m0->m_flags & M_EAPOL)
		rate = tp->mgmtrate;
	else if (IEEE80211_IS_MULTICAST(wh->i_addr1))
		rate = tp->mcastrate;
	else if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE)
		rate = tp->ucastrate;
	else {
		(void) ieee80211_ratectl_rate(ni, NULL, 0);
		rate = ni->ni_txrate;
	}

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_get_txkey(ni, m0);
		if (k == NULL) {
			m_freem(m0);
			return (ENOENT);
		}
		if ((k->wk_flags & IEEE80211_KEY_SWCRYPT) &&
		    !k->wk_cipher->ic_encap(k, m0)) {
			m_freem(m0);
			return (ENOBUFS);
		}

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	if (type != IEEE80211_FC0_TYPE_CTL && !IEEE80211_QOS_HAS_SEQ(wh))
		xflags |= RT2573_TX_HWSEQ;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		int prot = IEEE80211_PROT_NONE;
		if (m0->m_pkthdr.len + IEEE80211_CRC_LEN > vap->iv_rtsthreshold)
			prot = IEEE80211_PROT_RTSCTS;
		else if ((ic->ic_flags & IEEE80211_F_USEPROT) &&
		    ieee80211_rate2phytype(ic->ic_rt, rate) == IEEE80211_T_OFDM)
			prot = ic->ic_protmode;
		if (prot != IEEE80211_PROT_NONE) {
			error = rum_sendprot(sc, m0, ni, prot, rate);
			if (error || sc->tx_nfree == 0) {
				m_freem(m0);
				return ENOBUFS;
			}
			flags |= RT2573_TX_LONG_RETRY | RT2573_TX_IFS_SIFS;
		}
	}

	if (k != NULL)
		flags |= rum_tx_crypto_flags(sc, ni, k);

	data = STAILQ_FIRST(&sc->tx_free);
	STAILQ_REMOVE_HEAD(&sc->tx_free, next);
	sc->tx_nfree--;

	data->m = m0;
	data->ni = ni;
	data->rate = rate;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/* Unicast frame, check if an ACK is expected. */
		if (!qos || (qos & IEEE80211_QOS_ACKPOLICY) !=
		    IEEE80211_QOS_ACKPOLICY_NOACK)
			flags |= RT2573_TX_NEED_ACK;

		dur = ieee80211_ack_duration(ic->ic_rt, rate,
		    ic->ic_flags & IEEE80211_F_SHPREAMBLE);
		USETW(wh->i_dur, dur);
	}

	rum_setup_tx_desc(sc, &data->desc, k, flags, xflags, ac, hdrlen,
	    m0->m_pkthdr.len, rate);

	DPRINTFN(10, "sending frame len=%d rate=%d\n",
	    m0->m_pkthdr.len + (int)RT2573_TX_DESC_SIZE, rate);

	STAILQ_INSERT_TAIL(&sc->tx_q, data, next);
	usbd_transfer_start(sc->sc_xfer[RUM_BULK_WR]);

	return 0;
}

static int
rum_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct rum_softc *sc = ic->ic_softc;
	int error;

	RUM_LOCK(sc);
	if (!sc->sc_running) {
		RUM_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		RUM_UNLOCK(sc);
		return (error);
	}
	rum_start(sc);
	RUM_UNLOCK(sc);

	return (0);
}

static void
rum_start(struct rum_softc *sc)
{
	struct ieee80211_node *ni;
	struct mbuf *m;

	RUM_LOCK_ASSERT(sc);

	if (!sc->sc_running)
		return;

	while (sc->tx_nfree >= RUM_TX_MINFREE &&
	    (m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *) m->m_pkthdr.rcvif;
		if (rum_tx_data(sc, m, ni) != 0) {
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			ieee80211_free_node(ni);
			break;
		}
	}
}

static void
rum_parent(struct ieee80211com *ic)
{
	struct rum_softc *sc = ic->ic_softc;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	RUM_LOCK(sc);
	if (sc->sc_detached) {
		RUM_UNLOCK(sc);
		return;
	}
	RUM_UNLOCK(sc);

	if (ic->ic_nrunning > 0) {
		if (rum_init(sc) == 0)
			ieee80211_start_all(ic);
		else
			ieee80211_stop(vap);
	} else
		rum_stop(sc);
}

static void
rum_eeprom_read(struct rum_softc *sc, uint16_t addr, void *buf, int len)
{
	struct usb_device_request req;
	usb_error_t error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2573_READ_EEPROM;
	USETW(req.wValue, 0);
	USETW(req.wIndex, addr);
	USETW(req.wLength, len);

	error = rum_do_request(sc, &req, buf);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not read EEPROM: %s\n",
		    usbd_errstr(error));
	}
}

static uint32_t
rum_read(struct rum_softc *sc, uint16_t reg)
{
	uint32_t val;

	rum_read_multi(sc, reg, &val, sizeof val);

	return le32toh(val);
}

static void
rum_read_multi(struct rum_softc *sc, uint16_t reg, void *buf, int len)
{
	struct usb_device_request req;
	usb_error_t error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2573_READ_MULTI_MAC;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);

	error = rum_do_request(sc, &req, buf);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not multi read MAC register: %s\n",
		    usbd_errstr(error));
	}
}

static usb_error_t
rum_write(struct rum_softc *sc, uint16_t reg, uint32_t val)
{
	uint32_t tmp = htole32(val);

	return (rum_write_multi(sc, reg, &tmp, sizeof tmp));
}

static usb_error_t
rum_write_multi(struct rum_softc *sc, uint16_t reg, void *buf, size_t len)
{
	struct usb_device_request req;
	usb_error_t error;
	size_t offset;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2573_WRITE_MULTI_MAC;
	USETW(req.wValue, 0);

	/* write at most 64 bytes at a time */
	for (offset = 0; offset < len; offset += 64) {
		USETW(req.wIndex, reg + offset);
		USETW(req.wLength, MIN(len - offset, 64));

		error = rum_do_request(sc, &req, (char *)buf + offset);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not multi write MAC register: %s\n",
			    usbd_errstr(error));
			return (error);
		}
	}

	return (USB_ERR_NORMAL_COMPLETION);
}

static usb_error_t
rum_setbits(struct rum_softc *sc, uint16_t reg, uint32_t mask)
{
	return (rum_write(sc, reg, rum_read(sc, reg) | mask));
}

static usb_error_t
rum_clrbits(struct rum_softc *sc, uint16_t reg, uint32_t mask)
{
	return (rum_write(sc, reg, rum_read(sc, reg) & ~mask));
}

static usb_error_t
rum_modbits(struct rum_softc *sc, uint16_t reg, uint32_t set, uint32_t unset)
{
	return (rum_write(sc, reg, (rum_read(sc, reg) & ~unset) | set));
}

static int
rum_bbp_busy(struct rum_softc *sc)
{
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR3) & RT2573_BBP_BUSY))
			break;
		if (rum_pause(sc, hz / 100))
			break;
	}
	if (ntries == 100)
		return (ETIMEDOUT);

	return (0);
}

static void
rum_bbp_write(struct rum_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;

	DPRINTFN(2, "reg=0x%08x\n", reg);

	if (rum_bbp_busy(sc) != 0) {
		device_printf(sc->sc_dev, "could not write to BBP\n");
		return;
	}

	tmp = RT2573_BBP_BUSY | (reg & 0x7f) << 8 | val;
	rum_write(sc, RT2573_PHY_CSR3, tmp);
}

static uint8_t
rum_bbp_read(struct rum_softc *sc, uint8_t reg)
{
	uint32_t val;
	int ntries;

	DPRINTFN(2, "reg=0x%08x\n", reg);

	if (rum_bbp_busy(sc) != 0) {
		device_printf(sc->sc_dev, "could not read BBP\n");
		return 0;
	}

	val = RT2573_BBP_BUSY | RT2573_BBP_READ | reg << 8;
	rum_write(sc, RT2573_PHY_CSR3, val);

	for (ntries = 0; ntries < 100; ntries++) {
		val = rum_read(sc, RT2573_PHY_CSR3);
		if (!(val & RT2573_BBP_BUSY))
			return val & 0xff;
		if (rum_pause(sc, hz / 100))
			break;
	}

	device_printf(sc->sc_dev, "could not read BBP\n");
	return 0;
}

static void
rum_rf_write(struct rum_softc *sc, uint8_t reg, uint32_t val)
{
	uint32_t tmp;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(rum_read(sc, RT2573_PHY_CSR4) & RT2573_RF_BUSY))
			break;
		if (rum_pause(sc, hz / 100))
			break;
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev, "could not write to RF\n");
		return;
	}

	tmp = RT2573_RF_BUSY | RT2573_RF_20BIT | (val & 0xfffff) << 2 |
	    (reg & 3);
	rum_write(sc, RT2573_PHY_CSR4, tmp);

	/* remember last written value in sc */
	sc->rf_regs[reg] = val;

	DPRINTFN(15, "RF R[%u] <- 0x%05x\n", reg & 3, val & 0xfffff);
}

static void
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
static void
rum_enable_mrr(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if (!IEEE80211_IS_CHAN_5GHZ(ic->ic_bsschan)) {
		rum_setbits(sc, RT2573_TXRX_CSR4,
		    RT2573_MRR_ENABLED | RT2573_MRR_CCK_FALLBACK);
	} else {
		rum_modbits(sc, RT2573_TXRX_CSR4,
		    RT2573_MRR_ENABLED, RT2573_MRR_CCK_FALLBACK);
	}
}

static void
rum_set_txpreamble(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		rum_setbits(sc, RT2573_TXRX_CSR4, RT2573_SHORT_PREAMBLE);
	else
		rum_clrbits(sc, RT2573_TXRX_CSR4, RT2573_SHORT_PREAMBLE);
}

static void
rum_set_basicrates(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* update basic rate set */
	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		/* 11b basic rates: 1, 2Mbps */
		rum_write(sc, RT2573_TXRX_CSR5, 0x3);
	} else if (IEEE80211_IS_CHAN_5GHZ(ic->ic_bsschan)) {
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
static void
rum_select_band(struct rum_softc *sc, struct ieee80211_channel *c)
{
	uint8_t bbp17, bbp35, bbp96, bbp97, bbp98, bbp104;

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

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		rum_modbits(sc, RT2573_PHY_CSR0, RT2573_PA_PE_2GHZ,
		    RT2573_PA_PE_5GHZ);
	} else {
		rum_modbits(sc, RT2573_PHY_CSR0, RT2573_PA_PE_5GHZ,
		    RT2573_PA_PE_2GHZ);
	}
}

static void
rum_set_chan(struct rum_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct rfprog *rfprog;
	uint8_t bbp3, bbp94 = RT2573_BBPR94_DEFAULT;
	int8_t power;
	int i, chan;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return;

	/* select the appropriate RF settings based on what EEPROM says */
	rfprog = (sc->rf_rev == RT2573_RF_5225 ||
		  sc->rf_rev == RT2573_RF_2527) ? rum_rf5225 : rum_rf5226;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rfprog[i].chan != chan; i++);

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
	if (c->ic_flags != ic->ic_curchan->ic_flags) {
		rum_select_band(sc, c);
		rum_select_antenna(sc);
	}
	ic->ic_curchan = c;

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

	rum_pause(sc, hz / 100);

	/* enable smart mode for MIMO-capable RFs */
	bbp3 = rum_bbp_read(sc, 3);

	bbp3 &= ~RT2573_SMART_MODE;
	if (sc->rf_rev == RT2573_RF_5225 || sc->rf_rev == RT2573_RF_2527)
		bbp3 |= RT2573_SMART_MODE;

	rum_bbp_write(sc, 3, bbp3);

	if (bbp94 != RT2573_BBPR94_DEFAULT)
		rum_bbp_write(sc, 94, bbp94);

	/* give the chip some extra time to do the switchover */
	rum_pause(sc, hz / 100);
}

static void
rum_set_maxretry(struct rum_softc *sc, struct ieee80211vap *vap)
{
	struct ieee80211_node *ni = vap->iv_bss;
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct rum_vap *rvp = RUM_VAP(vap);

	rvp->maxretry = MIN(tp->maxretry, 0xf);

	rum_modbits(sc, RT2573_TXRX_CSR4, RT2573_SHORT_RETRY(rvp->maxretry) |
	    RT2573_LONG_RETRY(rvp->maxretry),
	    RT2573_SHORT_RETRY_MASK | RT2573_LONG_RETRY_MASK);
}

/*
 * Enable TSF synchronization and tell h/w to start sending beacons for IBSS
 * and HostAP operating modes.
 */
static int
rum_enable_tsf_sync(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t tmp;
	uint16_t bintval;

	if (vap->iv_opmode != IEEE80211_M_STA) {
		/*
		 * Change default 16ms TBTT adjustment to 8ms.
		 * Must be done before enabling beacon generation.
		 */
		if (rum_write(sc, RT2573_TXRX_CSR10, 1 << 12 | 8) != 0)
			return EIO;
	}

	tmp = rum_read(sc, RT2573_TXRX_CSR9) & 0xff000000;

	/* set beacon interval (in 1/16ms unit) */
	bintval = vap->iv_bss->ni_intval;
	tmp |= bintval * 16;
	tmp |= RT2573_TSF_TIMER_EN | RT2573_TBTT_TIMER_EN;

	switch (vap->iv_opmode) {
	case IEEE80211_M_STA:
		/*
		 * Local TSF is always updated with remote TSF on beacon
		 * reception.
		 */
		tmp |= RT2573_TSF_SYNC_MODE(RT2573_TSF_SYNC_MODE_STA);
		break;
	case IEEE80211_M_IBSS:
		/*
		 * Local TSF is updated with remote TSF on beacon reception
		 * only if the remote TSF is greater than local TSF.
		 */
		tmp |= RT2573_TSF_SYNC_MODE(RT2573_TSF_SYNC_MODE_IBSS);
		tmp |= RT2573_BCN_TX_EN;
		break;
	case IEEE80211_M_HOSTAP:
		/* SYNC with nobody */
		tmp |= RT2573_TSF_SYNC_MODE(RT2573_TSF_SYNC_MODE_HOSTAP);
		tmp |= RT2573_BCN_TX_EN;
		break;
	default:
		device_printf(sc->sc_dev,
		    "Enabling TSF failed. undefined opmode %d\n",
		    vap->iv_opmode);
		return EINVAL;
	}

	if (rum_write(sc, RT2573_TXRX_CSR9, tmp) != 0)
		return EIO;

	/* refresh current sleep time */
	return (rum_set_sleep_time(sc, bintval));
}

static void
rum_enable_tsf(struct rum_softc *sc)
{
	rum_modbits(sc, RT2573_TXRX_CSR9, RT2573_TSF_TIMER_EN |
	    RT2573_TSF_SYNC_MODE(RT2573_TSF_SYNC_MODE_DIS), 0x00ffffff);
}

static void
rum_abort_tsf_sync(struct rum_softc *sc)
{
	rum_clrbits(sc, RT2573_TXRX_CSR9, 0x00ffffff);
}

static void
rum_get_tsf(struct rum_softc *sc, uint64_t *buf)
{
	rum_read_multi(sc, RT2573_TXRX_CSR12, buf, sizeof (*buf));
}

static void
rum_update_slot_cb(struct rum_softc *sc, union sec_param *data, uint8_t rvp_id)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t slottime;

	slottime = IEEE80211_GET_SLOTTIME(ic);

	rum_modbits(sc, RT2573_MAC_CSR9, slottime, 0xff);

	DPRINTF("setting slot time to %uus\n", slottime);
}

static void
rum_update_slot(struct ieee80211com *ic)
{
	rum_cmd_sleepable(ic->ic_softc, NULL, 0, 0, rum_update_slot_cb);
}

static int
rum_wme_update(struct ieee80211com *ic)
{
	struct chanAccParams chp;
	const struct wmeParams *chanp;
	struct rum_softc *sc = ic->ic_softc;
	int error = 0;

	ieee80211_wme_ic_getparams(ic, &chp);
	chanp = chp.cap_wmeParams;

	RUM_LOCK(sc);
	error = rum_write(sc, RT2573_AIFSN_CSR,
	    chanp[WME_AC_VO].wmep_aifsn  << 12 |
	    chanp[WME_AC_VI].wmep_aifsn  <<  8 |
	    chanp[WME_AC_BK].wmep_aifsn  <<  4 |
	    chanp[WME_AC_BE].wmep_aifsn);
	if (error)
		goto print_err;
	error = rum_write(sc, RT2573_CWMIN_CSR,
	    chanp[WME_AC_VO].wmep_logcwmin << 12 |
	    chanp[WME_AC_VI].wmep_logcwmin <<  8 |
	    chanp[WME_AC_BK].wmep_logcwmin <<  4 |
	    chanp[WME_AC_BE].wmep_logcwmin);
	if (error)
		goto print_err;
	error = rum_write(sc, RT2573_CWMAX_CSR,
	    chanp[WME_AC_VO].wmep_logcwmax << 12 |
	    chanp[WME_AC_VI].wmep_logcwmax <<  8 |
	    chanp[WME_AC_BK].wmep_logcwmax <<  4 |
	    chanp[WME_AC_BE].wmep_logcwmax);
	if (error)
		goto print_err;
	error = rum_write(sc, RT2573_TXOP01_CSR,
	    chanp[WME_AC_BK].wmep_txopLimit << 16 |
	    chanp[WME_AC_BE].wmep_txopLimit);
	if (error)
		goto print_err;
	error = rum_write(sc, RT2573_TXOP23_CSR,
	    chanp[WME_AC_VO].wmep_txopLimit << 16 |
	    chanp[WME_AC_VI].wmep_txopLimit);
	if (error)
		goto print_err;

	memcpy(sc->wme_params, chanp, sizeof(*chanp) * WME_NUM_AC);

print_err:
	RUM_UNLOCK(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "%s: WME update failed, error %d\n",
		    __func__, error);
	}

	return (error);
}

static void
rum_set_bssid(struct rum_softc *sc, const uint8_t *bssid)
{

	rum_write(sc, RT2573_MAC_CSR4,
	    bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24);
	rum_write(sc, RT2573_MAC_CSR5,
	    bssid[4] | bssid[5] << 8 | RT2573_NUM_BSSID_MSK(1));
}

static void
rum_set_macaddr(struct rum_softc *sc, const uint8_t *addr)
{

	rum_write(sc, RT2573_MAC_CSR2,
	    addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	rum_write(sc, RT2573_MAC_CSR3,
	    addr[4] | addr[5] << 8 | 0xff << 16);
}

static void
rum_setpromisc(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_promisc == 0)
		rum_setbits(sc, RT2573_TXRX_CSR0, RT2573_DROP_NOT_TO_ME);
	else
		rum_clrbits(sc, RT2573_TXRX_CSR0, RT2573_DROP_NOT_TO_ME);

	DPRINTF("%s promiscuous mode\n", ic->ic_promisc > 0 ?
	    "entering" : "leaving");
}

static void
rum_update_promisc(struct ieee80211com *ic)
{
	struct rum_softc *sc = ic->ic_softc;

	RUM_LOCK(sc);
	if (sc->sc_running)
		rum_setpromisc(sc);
	RUM_UNLOCK(sc);
}

static void
rum_update_mcast(struct ieee80211com *ic)
{
	/* Ignore. */
}

static const char *
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

static void
rum_read_eeprom(struct rum_softc *sc)
{
	uint16_t val;
#ifdef RUM_DEBUG
	int i;
#endif

	/* read MAC address */
	rum_eeprom_read(sc, RT2573_EEPROM_ADDRESS, sc->sc_ic.ic_macaddr, 6);

	rum_eeprom_read(sc, RT2573_EEPROM_ANTENNA, &val, 2);
	val = le16toh(val);
	sc->rf_rev =   (val >> 11) & 0x1f;
	sc->hw_radio = (val >> 10) & 0x1;
	sc->rx_ant =   (val >> 4)  & 0x3;
	sc->tx_ant =   (val >> 2)  & 0x3;
	sc->nb_ant =   val & 0x3;

	DPRINTF("RF revision=%d\n", sc->rf_rev);

	rum_eeprom_read(sc, RT2573_EEPROM_CONFIG2, &val, 2);
	val = le16toh(val);
	sc->ext_5ghz_lna = (val >> 6) & 0x1;
	sc->ext_2ghz_lna = (val >> 4) & 0x1;

	DPRINTF("External 2GHz LNA=%d\nExternal 5GHz LNA=%d\n",
	    sc->ext_2ghz_lna, sc->ext_5ghz_lna);

	rum_eeprom_read(sc, RT2573_EEPROM_RSSI_2GHZ_OFFSET, &val, 2);
	val = le16toh(val);
	if ((val & 0xff) != 0xff)
		sc->rssi_2ghz_corr = (int8_t)(val & 0xff);	/* signed */

	/* Only [-10, 10] is valid */
	if (sc->rssi_2ghz_corr < -10 || sc->rssi_2ghz_corr > 10)
		sc->rssi_2ghz_corr = 0;

	rum_eeprom_read(sc, RT2573_EEPROM_RSSI_5GHZ_OFFSET, &val, 2);
	val = le16toh(val);
	if ((val & 0xff) != 0xff)
		sc->rssi_5ghz_corr = (int8_t)(val & 0xff);	/* signed */

	/* Only [-10, 10] is valid */
	if (sc->rssi_5ghz_corr < -10 || sc->rssi_5ghz_corr > 10)
		sc->rssi_5ghz_corr = 0;

	if (sc->ext_2ghz_lna)
		sc->rssi_2ghz_corr -= 14;
	if (sc->ext_5ghz_lna)
		sc->rssi_5ghz_corr -= 14;

	DPRINTF("RSSI 2GHz corr=%d\nRSSI 5GHz corr=%d\n",
	    sc->rssi_2ghz_corr, sc->rssi_5ghz_corr);

	rum_eeprom_read(sc, RT2573_EEPROM_FREQ_OFFSET, &val, 2);
	val = le16toh(val);
	if ((val & 0xff) != 0xff)
		sc->rffreq = val & 0xff;

	DPRINTF("RF freq=%d\n", sc->rffreq);

	/* read Tx power for all a/b/g channels */
	rum_eeprom_read(sc, RT2573_EEPROM_TXPOWER, sc->txpow, 14);
	/* XXX default Tx power for 802.11a channels */
	memset(sc->txpow + 14, 24, sizeof (sc->txpow) - 14);
#ifdef RUM_DEBUG
	for (i = 0; i < 14; i++)
		DPRINTF("Channel=%d Tx power=%d\n", i + 1,  sc->txpow[i]);
#endif

	/* read default values for BBP registers */
	rum_eeprom_read(sc, RT2573_EEPROM_BBP_BASE, sc->bbp_prom, 2 * 16);
#ifdef RUM_DEBUG
	for (i = 0; i < 14; i++) {
		if (sc->bbp_prom[i].reg == 0 || sc->bbp_prom[i].reg == 0xff)
			continue;
		DPRINTF("BBP R%d=%02x\n", sc->bbp_prom[i].reg,
		    sc->bbp_prom[i].val);
	}
#endif
}

static int
rum_bbp_wakeup(struct rum_softc *sc)
{
	unsigned int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (rum_read(sc, RT2573_MAC_CSR12) & 8)
			break;
		rum_write(sc, RT2573_MAC_CSR12, 4);	/* force wakeup */
		if (rum_pause(sc, hz / 100))
			break;
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev,
		    "timeout waiting for BBP/RF to wakeup\n");
		return (ETIMEDOUT);
	}

	return (0);
}

static int
rum_bbp_init(struct rum_softc *sc)
{
	int i, ntries;

	/* wait for BBP to be ready */
	for (ntries = 0; ntries < 100; ntries++) {
		const uint8_t val = rum_bbp_read(sc, 0);
		if (val != 0 && val != 0xff)
			break;
		if (rum_pause(sc, hz / 100))
			break;
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev, "timeout waiting for BBP\n");
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

static void
rum_clr_shkey_regs(struct rum_softc *sc)
{
	rum_write(sc, RT2573_SEC_CSR0, 0);
	rum_write(sc, RT2573_SEC_CSR1, 0);
	rum_write(sc, RT2573_SEC_CSR5, 0);
}

static int
rum_init(struct rum_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t tmp;
	int i, ret;

	RUM_LOCK(sc);
	if (sc->sc_running) {
		ret = 0;
		goto end;
	}

	/* initialize MAC registers to default values */
	for (i = 0; i < nitems(rum_def_mac); i++)
		rum_write(sc, rum_def_mac[i].reg, rum_def_mac[i].val);

	/* reset some WME parameters to default values */
	sc->wme_params[0].wmep_aifsn = 2;
	sc->wme_params[0].wmep_logcwmin = 4;
	sc->wme_params[0].wmep_logcwmax = 10;

	/* set host ready */
	rum_write(sc, RT2573_MAC_CSR1, RT2573_RESET_ASIC | RT2573_RESET_BBP);
	rum_write(sc, RT2573_MAC_CSR1, 0);

	/* wait for BBP/RF to wakeup */
	if ((ret = rum_bbp_wakeup(sc)) != 0)
		goto end;

	if ((ret = rum_bbp_init(sc)) != 0)
		goto end;

	/* select default channel */
	rum_select_band(sc, ic->ic_curchan);
	rum_select_antenna(sc);
	rum_set_chan(sc, ic->ic_curchan);

	/* clear STA registers */
	rum_read_multi(sc, RT2573_STA_CSR0, sc->sta, sizeof sc->sta);

	/* clear security registers (if required) */
	if (sc->sc_clr_shkeys == 0) {
		rum_clr_shkey_regs(sc);
		sc->sc_clr_shkeys = 1;
	}

	rum_set_macaddr(sc, vap ? vap->iv_myaddr : ic->ic_macaddr);

	/* initialize ASIC */
	rum_write(sc, RT2573_MAC_CSR1, RT2573_HOST_READY);

	/*
	 * Allocate Tx and Rx xfer queues.
	 */
	rum_setup_tx_list(sc);

	/* update Rx filter */
	tmp = rum_read(sc, RT2573_TXRX_CSR0) & 0xffff;

	tmp |= RT2573_DROP_PHY_ERROR | RT2573_DROP_CRC_ERROR;
	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RT2573_DROP_CTL | RT2573_DROP_VER_ERROR |
		       RT2573_DROP_ACKCTS;
		if (ic->ic_opmode != IEEE80211_M_HOSTAP)
			tmp |= RT2573_DROP_TODS;
		if (ic->ic_promisc == 0)
			tmp |= RT2573_DROP_NOT_TO_ME;
	}
	rum_write(sc, RT2573_TXRX_CSR0, tmp);

	sc->sc_running = 1;
	usbd_xfer_set_stall(sc->sc_xfer[RUM_BULK_WR]);
	usbd_transfer_start(sc->sc_xfer[RUM_BULK_RD]);

end:	RUM_UNLOCK(sc);

	if (ret != 0)
		rum_stop(sc);

	return ret;
}

static void
rum_stop(struct rum_softc *sc)
{

	RUM_LOCK(sc);
	if (!sc->sc_running) {
		RUM_UNLOCK(sc);
		return;
	}
	sc->sc_running = 0;
	RUM_UNLOCK(sc);

	/*
	 * Drain the USB transfers, if not already drained:
	 */
	usbd_transfer_drain(sc->sc_xfer[RUM_BULK_WR]);
	usbd_transfer_drain(sc->sc_xfer[RUM_BULK_RD]);

	RUM_LOCK(sc);
	rum_unsetup_tx_list(sc);

	/* disable Rx */
	rum_setbits(sc, RT2573_TXRX_CSR0, RT2573_DISABLE_RX);

	/* reset ASIC */
	rum_write(sc, RT2573_MAC_CSR1, RT2573_RESET_ASIC | RT2573_RESET_BBP);
	rum_write(sc, RT2573_MAC_CSR1, 0);
	RUM_UNLOCK(sc);
}

static void
rum_load_microcode(struct rum_softc *sc, const uint8_t *ucode, size_t size)
{
	uint16_t reg = RT2573_MCU_CODE_BASE;
	usb_error_t err;

	/* copy firmware image into NIC */
	for (; size >= 4; reg += 4, ucode += 4, size -= 4) {
		err = rum_write(sc, reg, UGETDW(ucode));
		if (err) {
			/* firmware already loaded ? */
			device_printf(sc->sc_dev, "Firmware load "
			    "failure! (ignored)\n");
			break;
		}
	}

	err = rum_do_mcu_request(sc, RT2573_MCU_RUN);
	if (err != USB_ERR_NORMAL_COMPLETION) {
		device_printf(sc->sc_dev, "could not run firmware: %s\n",
		    usbd_errstr(err));
	}

	/* give the chip some time to boot */
	rum_pause(sc, hz / 8);
}

static int
rum_set_sleep_time(struct rum_softc *sc, uint16_t bintval)
{
	struct ieee80211com *ic = &sc->sc_ic;
	usb_error_t uerror;
	int exp, delay;

	RUM_LOCK_ASSERT(sc);

	exp = ic->ic_lintval / bintval;
	delay = ic->ic_lintval % bintval;

	if (exp > RT2573_TBCN_EXP_MAX)
		exp = RT2573_TBCN_EXP_MAX;
	if (delay > RT2573_TBCN_DELAY_MAX)
		delay = RT2573_TBCN_DELAY_MAX;

	uerror = rum_modbits(sc, RT2573_MAC_CSR11,
	    RT2573_TBCN_EXP(exp) |
	    RT2573_TBCN_DELAY(delay),
	    RT2573_TBCN_EXP(RT2573_TBCN_EXP_MAX) |
	    RT2573_TBCN_DELAY(RT2573_TBCN_DELAY_MAX));

	if (uerror != USB_ERR_NORMAL_COMPLETION)
		return (EIO);

	sc->sc_sleep_time = IEEE80211_TU_TO_TICKS(exp * bintval + delay);

	return (0);
}

static int
rum_reset(struct ieee80211vap *vap, u_long cmd)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni;
	struct rum_softc *sc = ic->ic_softc;
	int error;

	switch (cmd) {
	case IEEE80211_IOC_POWERSAVE:
	case IEEE80211_IOC_PROTMODE:
	case IEEE80211_IOC_RTSTHRESHOLD:
		error = 0;
		break;
	case IEEE80211_IOC_POWERSAVESLEEP:
		ni = ieee80211_ref_node(vap->iv_bss);

		RUM_LOCK(sc);
		error = rum_set_sleep_time(sc, ni->ni_intval);
		if (vap->iv_state == IEEE80211_S_SLEEP) {
			/* Use new values for wakeup timer. */
			rum_clrbits(sc, RT2573_MAC_CSR11, RT2573_AUTO_WAKEUP);
			rum_setbits(sc, RT2573_MAC_CSR11, RT2573_AUTO_WAKEUP);
		}
		/* XXX send reassoc */
		RUM_UNLOCK(sc);

		ieee80211_free_node(ni);
		break;
	default:
		error = ENETRESET;
		break;
	}

	return (error);
}

static int
rum_set_beacon(struct rum_softc *sc, struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct rum_vap *rvp = RUM_VAP(vap);
	struct mbuf *m = rvp->bcn_mbuf;
	const struct ieee80211_txparam *tp;
	struct rum_tx_desc desc;

	RUM_LOCK_ASSERT(sc);

	if (m == NULL)
		return EINVAL;
	if (ic->ic_bsschan == IEEE80211_CHAN_ANYC)
		return EINVAL;

	tp = &vap->iv_txparms[ieee80211_chan2mode(ic->ic_bsschan)];
	rum_setup_tx_desc(sc, &desc, NULL, RT2573_TX_TIMESTAMP,
	    RT2573_TX_HWSEQ, 0, 0, m->m_pkthdr.len, tp->mgmtrate);

	/* copy the Tx descriptor into NIC memory */
	if (rum_write_multi(sc, RT2573_HW_BCN_BASE(0), (uint8_t *)&desc,
	    RT2573_TX_DESC_SIZE) != 0)
		return EIO;

	/* copy beacon header and payload into NIC memory */
	if (rum_write_multi(sc, RT2573_HW_BCN_BASE(0) + RT2573_TX_DESC_SIZE,
	    mtod(m, uint8_t *), m->m_pkthdr.len) != 0)
		return EIO;

	return 0;
}

static int
rum_alloc_beacon(struct rum_softc *sc, struct ieee80211vap *vap)
{
	struct rum_vap *rvp = RUM_VAP(vap);
	struct ieee80211_node *ni = vap->iv_bss;
	struct mbuf *m;

	if (ni->ni_chan == IEEE80211_CHAN_ANYC)
		return EINVAL;

	m = ieee80211_beacon_alloc(ni);
	if (m == NULL)
		return ENOMEM;

	if (rvp->bcn_mbuf != NULL)
		m_freem(rvp->bcn_mbuf);

	rvp->bcn_mbuf = m;

	return (rum_set_beacon(sc, vap));
}

static void
rum_update_beacon_cb(struct rum_softc *sc, union sec_param *data,
    uint8_t rvp_id)
{
	struct ieee80211vap *vap = data->vap;

	rum_set_beacon(sc, vap);
}

static void
rum_update_beacon(struct ieee80211vap *vap, int item)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct rum_softc *sc = ic->ic_softc;
	struct rum_vap *rvp = RUM_VAP(vap);
	struct ieee80211_beacon_offsets *bo = &vap->iv_bcn_off;
	struct ieee80211_node *ni = vap->iv_bss;
	struct mbuf *m = rvp->bcn_mbuf;
	int mcast = 0;

	RUM_LOCK(sc);
	if (m == NULL) {
		m = ieee80211_beacon_alloc(ni);
		if (m == NULL) {
			device_printf(sc->sc_dev,
			    "%s: could not allocate beacon frame\n", __func__);
			RUM_UNLOCK(sc);
			return;
		}
		rvp->bcn_mbuf = m;
	}

	switch (item) {
	case IEEE80211_BEACON_ERP:
		rum_update_slot(ic);
		break;
	case IEEE80211_BEACON_TIM:
		mcast = 1;	/*TODO*/
		break;
	default:
		break;
	}
	RUM_UNLOCK(sc);

	setbit(bo->bo_flags, item);
	ieee80211_beacon_update(ni, m, mcast);

	rum_cmd_sleepable(sc, &vap, sizeof(vap), 0, rum_update_beacon_cb);
}

static int
rum_common_key_set(struct rum_softc *sc, struct ieee80211_key *k,
    uint16_t base)
{

	if (rum_write_multi(sc, base, k->wk_key, k->wk_keylen))
		return EIO;

	if (k->wk_cipher->ic_cipher == IEEE80211_CIPHER_TKIP) {
		if (rum_write_multi(sc, base + IEEE80211_KEYBUF_SIZE,
		    k->wk_txmic, 8))
			return EIO;
		if (rum_write_multi(sc, base + IEEE80211_KEYBUF_SIZE + 8,
		    k->wk_rxmic, 8))
			return EIO;
	}

	return 0;
}

static void
rum_group_key_set_cb(struct rum_softc *sc, union sec_param *data,
    uint8_t rvp_id) 
{
	struct ieee80211_key *k = &data->key;
	uint8_t mode;

	if (sc->sc_clr_shkeys == 0) {
		rum_clr_shkey_regs(sc);
		sc->sc_clr_shkeys = 1;
	}

	mode = rum_crypto_mode(sc, k->wk_cipher->ic_cipher, k->wk_keylen);
	if (mode == 0)
		goto print_err;

	DPRINTFN(1, "setting group key %d for vap %d, mode %d "
	    "(tx %s, rx %s)\n", k->wk_keyix, rvp_id, mode,
	    (k->wk_flags & IEEE80211_KEY_XMIT) ? "on" : "off",
	    (k->wk_flags & IEEE80211_KEY_RECV) ? "on" : "off");

	/* Install the key. */
	if (rum_common_key_set(sc, k, RT2573_SKEY(rvp_id, k->wk_keyix)) != 0)
		goto print_err;

	/* Set cipher mode. */
	if (rum_modbits(sc, rvp_id < 2 ? RT2573_SEC_CSR1 : RT2573_SEC_CSR5,
	      mode << (rvp_id % 2 + k->wk_keyix) * RT2573_SKEY_MAX,
	      RT2573_MODE_MASK << (rvp_id % 2 + k->wk_keyix) * RT2573_SKEY_MAX)
	    != 0)
		goto print_err;

	/* Mark this key as valid. */
	if (rum_setbits(sc, RT2573_SEC_CSR0,
	      1 << (rvp_id * RT2573_SKEY_MAX + k->wk_keyix)) != 0)
		goto print_err;

	return;

print_err:
	device_printf(sc->sc_dev, "%s: cannot set group key %d for vap %d\n",
	    __func__, k->wk_keyix, rvp_id);
}

static void
rum_group_key_del_cb(struct rum_softc *sc, union sec_param *data,
    uint8_t rvp_id)
{
	struct ieee80211_key *k = &data->key;

	DPRINTF("%s: removing group key %d for vap %d\n", __func__,
	    k->wk_keyix, rvp_id);
	rum_clrbits(sc,
	    rvp_id < 2 ? RT2573_SEC_CSR1 : RT2573_SEC_CSR5,
	    RT2573_MODE_MASK << (rvp_id % 2 + k->wk_keyix) * RT2573_SKEY_MAX);
	rum_clrbits(sc, RT2573_SEC_CSR0,
	    rvp_id * RT2573_SKEY_MAX + k->wk_keyix);
}

static void
rum_pair_key_set_cb(struct rum_softc *sc, union sec_param *data,
    uint8_t rvp_id)
{
	struct ieee80211_key *k = &data->key;
	uint8_t buf[IEEE80211_ADDR_LEN + 1];
	uint8_t mode;

	mode = rum_crypto_mode(sc, k->wk_cipher->ic_cipher, k->wk_keylen);
	if (mode == 0)
		goto print_err;

	DPRINTFN(1, "setting pairwise key %d for vap %d, mode %d "
	    "(tx %s, rx %s)\n", k->wk_keyix, rvp_id, mode,
	    (k->wk_flags & IEEE80211_KEY_XMIT) ? "on" : "off",
	    (k->wk_flags & IEEE80211_KEY_RECV) ? "on" : "off");

	/* Install the key. */
	if (rum_common_key_set(sc, k, RT2573_PKEY(k->wk_keyix)) != 0)
		goto print_err;

	IEEE80211_ADDR_COPY(buf, k->wk_macaddr);
	buf[IEEE80211_ADDR_LEN] = mode;

	/* Set transmitter address and cipher mode. */
	if (rum_write_multi(sc, RT2573_ADDR_ENTRY(k->wk_keyix),
	      buf, sizeof buf) != 0)
		goto print_err;

	/* Enable key table lookup for this vap. */
	if (sc->vap_key_count[rvp_id]++ == 0)
		if (rum_setbits(sc, RT2573_SEC_CSR4, 1 << rvp_id) != 0)
			goto print_err;

	/* Mark this key as valid. */
	if (rum_setbits(sc,
	      k->wk_keyix < 32 ? RT2573_SEC_CSR2 : RT2573_SEC_CSR3,
	      1 << (k->wk_keyix % 32)) != 0)
		goto print_err;

	return;

print_err:
	device_printf(sc->sc_dev,
	    "%s: cannot set pairwise key %d, vap %d\n", __func__, k->wk_keyix,
	    rvp_id);
}

static void
rum_pair_key_del_cb(struct rum_softc *sc, union sec_param *data,
    uint8_t rvp_id)
{
	struct ieee80211_key *k = &data->key;

	DPRINTF("%s: removing key %d\n", __func__, k->wk_keyix);
	rum_clrbits(sc, (k->wk_keyix < 32) ? RT2573_SEC_CSR2 : RT2573_SEC_CSR3,
	    1 << (k->wk_keyix % 32));
	sc->keys_bmap &= ~(1ULL << k->wk_keyix);
	if (--sc->vap_key_count[rvp_id] == 0)
		rum_clrbits(sc, RT2573_SEC_CSR4, 1 << rvp_id);
}

static int
rum_key_alloc(struct ieee80211vap *vap, struct ieee80211_key *k,
    ieee80211_keyix *keyix, ieee80211_keyix *rxkeyix)
{
	struct rum_softc *sc = vap->iv_ic->ic_softc;
	uint8_t i;

	if (!(&vap->iv_nw_keys[0] <= k &&
	     k < &vap->iv_nw_keys[IEEE80211_WEP_NKID])) {
		if (!(k->wk_flags & IEEE80211_KEY_SWCRYPT)) {
			RUM_LOCK(sc);
			for (i = 0; i < RT2573_ADDR_MAX; i++) {
				if ((sc->keys_bmap & (1ULL << i)) == 0) {
					sc->keys_bmap |= (1ULL << i);
					*keyix = i;
					break;
				}
			}
			RUM_UNLOCK(sc);
			if (i == RT2573_ADDR_MAX) {
				device_printf(sc->sc_dev,
				    "%s: no free space in the key table\n",
				    __func__);
				return 0;
			}
		} else
			*keyix = 0;
	} else {
		*keyix = ieee80211_crypto_get_key_wepidx(vap, k);
	}
	*rxkeyix = *keyix;
	return 1;
}

static int
rum_key_set(struct ieee80211vap *vap, const struct ieee80211_key *k)
{
	struct rum_softc *sc = vap->iv_ic->ic_softc;
	int group;

	if (k->wk_flags & IEEE80211_KEY_SWCRYPT) {
		/* Not for us. */
		return 1;
	}

	group = k >= &vap->iv_nw_keys[0] && k < &vap->iv_nw_keys[IEEE80211_WEP_NKID];

	return !rum_cmd_sleepable(sc, k, sizeof(*k), 0,
		   group ? rum_group_key_set_cb : rum_pair_key_set_cb);
}

static int
rum_key_delete(struct ieee80211vap *vap, const struct ieee80211_key *k)
{
	struct rum_softc *sc = vap->iv_ic->ic_softc;
	int group;

	if (k->wk_flags & IEEE80211_KEY_SWCRYPT) {
		/* Not for us. */
		return 1;
	}

	group = k >= &vap->iv_nw_keys[0] && k < &vap->iv_nw_keys[IEEE80211_WEP_NKID];

	return !rum_cmd_sleepable(sc, k, sizeof(*k), 0,
		   group ? rum_group_key_del_cb : rum_pair_key_del_cb);
}

static int
rum_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_bpf_params *params)
{
	struct rum_softc *sc = ni->ni_ic->ic_softc;
	int ret;

	RUM_LOCK(sc);
	/* prevent management frames from being sent if we're not ready */
	if (!sc->sc_running) {
		ret = ENETDOWN;
		goto bad;
	}
	if (sc->tx_nfree < RUM_TX_MINFREE) {
		ret = EIO;
		goto bad;
	}

	if (params == NULL) {
		/*
		 * Legacy path; interpret frame contents to decide
		 * precisely how to send the frame.
		 */
		if ((ret = rum_tx_mgt(sc, m, ni)) != 0)
			goto bad;
	} else {
		/*
		 * Caller supplied explicit parameters to use in
		 * sending the frame.
		 */
		if ((ret = rum_tx_raw(sc, m, ni, params)) != 0)
			goto bad;
	}
	RUM_UNLOCK(sc);

	return 0;
bad:
	RUM_UNLOCK(sc);
	m_freem(m);
	return ret;
}

static void
rum_ratectl_start(struct rum_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct rum_vap *rvp = RUM_VAP(vap);

	/* clear statistic registers (STA_CSR0 to STA_CSR5) */
	rum_read_multi(sc, RT2573_STA_CSR0, sc->sta, sizeof sc->sta);

	usb_callout_reset(&rvp->ratectl_ch, hz, rum_ratectl_timeout, rvp);
}

static void
rum_ratectl_timeout(void *arg)
{
	struct rum_vap *rvp = arg;
	struct ieee80211vap *vap = &rvp->vap;
	struct ieee80211com *ic = vap->iv_ic;

	ieee80211_runtask(ic, &rvp->ratectl_task);
}

static void
rum_ratectl_task(void *arg, int pending)
{
	struct rum_vap *rvp = arg;
	struct ieee80211vap *vap = &rvp->vap;
	struct rum_softc *sc = vap->iv_ic->ic_softc;
	struct ieee80211_ratectl_tx_stats *txs = &sc->sc_txs;
	int ok[3], fail;

	RUM_LOCK(sc);
	/* read and clear statistic registers (STA_CSR0 to STA_CSR5) */
	rum_read_multi(sc, RT2573_STA_CSR0, sc->sta, sizeof(sc->sta));

	ok[0] = (le32toh(sc->sta[4]) & 0xffff);	/* TX ok w/o retry */
	ok[1] = (le32toh(sc->sta[4]) >> 16);	/* TX ok w/ one retry */
	ok[2] = (le32toh(sc->sta[5]) & 0xffff);	/* TX ok w/ multiple retries */
	fail =  (le32toh(sc->sta[5]) >> 16);	/* TX retry-fail count */

	txs->flags = IEEE80211_RATECTL_TX_STATS_RETRIES;
	txs->nframes = ok[0] + ok[1] + ok[2] + fail;
	txs->nsuccess = txs->nframes - fail;
	/* XXX at least */
	txs->nretries = ok[1] + ok[2] * 2 + fail * (rvp->maxretry + 1);

	if (txs->nframes != 0)
		ieee80211_ratectl_tx_update(vap, txs);

	/* count TX retry-fail as Tx errors */
	if_inc_counter(vap->iv_ifp, IFCOUNTER_OERRORS, fail);

	usb_callout_reset(&rvp->ratectl_ch, hz, rum_ratectl_timeout, rvp);
	RUM_UNLOCK(sc);
}

static void
rum_scan_start(struct ieee80211com *ic)
{
	struct rum_softc *sc = ic->ic_softc;

	RUM_LOCK(sc);
	rum_abort_tsf_sync(sc);
	rum_set_bssid(sc, ieee80211broadcastaddr);
	RUM_UNLOCK(sc);

}

static void
rum_scan_end(struct ieee80211com *ic)
{
	struct rum_softc *sc = ic->ic_softc;

	if (ic->ic_flags_ext & IEEE80211_FEXT_BGSCAN) {
		RUM_LOCK(sc);
		if (ic->ic_opmode != IEEE80211_M_AHDEMO)
			rum_enable_tsf_sync(sc);
		else
			rum_enable_tsf(sc);
		rum_set_bssid(sc, sc->sc_bssid);
		RUM_UNLOCK(sc);
	}
}

static void
rum_set_channel(struct ieee80211com *ic)
{
	struct rum_softc *sc = ic->ic_softc;

	RUM_LOCK(sc);
	rum_set_chan(sc, ic->ic_curchan);
	RUM_UNLOCK(sc);
}

static void
rum_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct rum_softc *sc = ic->ic_softc;
	uint8_t bands[IEEE80211_MODE_BYTES];

	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	ieee80211_add_channels_default_2ghz(chans, maxchans, nchans, bands, 0);

	if (sc->rf_rev == RT2573_RF_5225 || sc->rf_rev == RT2573_RF_5226) {
		setbit(bands, IEEE80211_MODE_11A);
		ieee80211_add_channel_list_5ghz(chans, maxchans, nchans,
		    rum_chan_5ghz, nitems(rum_chan_5ghz), bands, 0);
	}
}

static int
rum_get_rssi(struct rum_softc *sc, uint8_t raw)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int lna, agc, rssi;

	lna = (raw >> 5) & 0x3;
	agc = raw & 0x1f;

	if (lna == 0) {
		/*
		 * No RSSI mapping
		 *
		 * NB: Since RSSI is relative to noise floor, -1 is
		 *     adequate for caller to know error happened.
		 */
		return -1;
	}

	rssi = (2 * agc) - RT2573_NOISE_FLOOR;

	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
		rssi += sc->rssi_2ghz_corr;

		if (lna == 1)
			rssi -= 64;
		else if (lna == 2)
			rssi -= 74;
		else if (lna == 3)
			rssi -= 90;
	} else {
		rssi += sc->rssi_5ghz_corr;

		if (!sc->ext_5ghz_lna && lna != 1)
			rssi += 4;

		if (lna == 1)
			rssi -= 64;
		else if (lna == 2)
			rssi -= 86;
		else if (lna == 3)
			rssi -= 100;
	}
	return rssi;
}

static int
rum_pause(struct rum_softc *sc, int timeout)
{

	usb_pause_mtx(&sc->sc_mtx, timeout);
	return (0);
}

static device_method_t rum_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rum_match),
	DEVMETHOD(device_attach,	rum_attach),
	DEVMETHOD(device_detach,	rum_detach),
	DEVMETHOD_END
};

static driver_t rum_driver = {
	.name = "rum",
	.methods = rum_methods,
	.size = sizeof(struct rum_softc),
};

static devclass_t rum_devclass;

DRIVER_MODULE(rum, uhub, rum_driver, rum_devclass, NULL, 0);
MODULE_DEPEND(rum, wlan, 1, 1, 1);
MODULE_DEPEND(rum, usb, 1, 1, 1);
MODULE_VERSION(rum, 1);
USB_PNP_HOST_INFO(rum_devs);

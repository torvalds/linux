/*	$OpenBSD: if_run.c,v 1.140 2024/05/23 03:21:08 jsg Exp $	*/

/*-
 * Copyright (c) 2008-2010 Damien Bergamini <damien.bergamini@free.fr>
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

/*-
 * Ralink Technology RT2700U/RT2800U/RT3000U/RT3900E chipset driver.
 * http://www.ralinktech.com/
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

#include <dev/ic/rt2860reg.h>		/* shared with ral(4) */
#include <dev/usb/if_runvar.h>

#ifdef RUN_DEBUG
#define DPRINTF(x)	do { if (run_debug) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (run_debug >= (n)) printf x; } while (0)
int run_debug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

#define USB_ID(v, p)	{ USB_VENDOR_##v, USB_PRODUCT_##v##_##p }
static const struct usb_devno run_devs[] = {
	USB_ID(ABOCOM,		RT2770),
	USB_ID(ABOCOM,		RT2870),
	USB_ID(ABOCOM,		RT3070),
	USB_ID(ABOCOM,		RT3071),
	USB_ID(ABOCOM,		RT3072),
	USB_ID(ABOCOM2,		RT2870_1),
	USB_ID(ACCTON,		RT2770),
	USB_ID(ACCTON,		RT2870_1),
	USB_ID(ACCTON,		RT2870_2),
	USB_ID(ACCTON,		RT2870_3),
	USB_ID(ACCTON,		RT2870_4),
	USB_ID(ACCTON,		RT2870_5),
	USB_ID(ACCTON,		RT3070),
	USB_ID(ACCTON,		RT3070_1),
	USB_ID(ACCTON,		RT3070_2),
	USB_ID(ACCTON,		RT3070_3),
	USB_ID(ACCTON,		RT3070_4),
	USB_ID(ACCTON,		RT3070_5),
	USB_ID(ACCTON,		RT3070_6),
	USB_ID(AIRTIES,		RT3070),
	USB_ID(AIRTIES,		RT3070_2),
	USB_ID(ALLWIN,		RT2070),
	USB_ID(ALLWIN,		RT2770),
	USB_ID(ALLWIN,		RT2870),
	USB_ID(ALLWIN,		RT3070),
	USB_ID(ALLWIN,		RT3071),
	USB_ID(ALLWIN,		RT3072),
	USB_ID(ALLWIN,		RT3572),
	USB_ID(AMIGO,		RT2870_1),
	USB_ID(AMIGO,		RT2870_2),
	USB_ID(AMIT,		CGWLUSB2GNR),
	USB_ID(AMIT,		RT2870_1),
	USB_ID(AMIT2,		RT2870),
	USB_ID(ASUS,		RT2870_1),
	USB_ID(ASUS,		RT2870_2),
	USB_ID(ASUS,		RT2870_3),
	USB_ID(ASUS,		RT2870_4),
	USB_ID(ASUS,		RT2870_5),
	USB_ID(ASUS,		RT3070_1),
	USB_ID(ASUS,		USBN13),
	USB_ID(ASUS,		USBN53),
	USB_ID(ASUS,		USBN66),
	USB_ID(ASUS2,		USBN11),
	USB_ID(AZUREWAVE,	RT2870_1),
	USB_ID(AZUREWAVE,	RT2870_2),
	USB_ID(AZUREWAVE,	RT3070_1),
	USB_ID(AZUREWAVE,	RT3070_2),
	USB_ID(AZUREWAVE,	RT3070_3),
	USB_ID(AZUREWAVE,	RT3070_4),
	USB_ID(AZUREWAVE,	RT3070_5),
	USB_ID(BELKIN,		F9L1103),
	USB_ID(BELKIN,		F5D8053V3),
	USB_ID(BELKIN,		F5D8055),
	USB_ID(BELKIN,		F5D8055V2),
	USB_ID(BELKIN,		F6D4050V1),
	USB_ID(BELKIN,		F6D4050V2),
	USB_ID(BELKIN,		F7D1101V2),
	USB_ID(BELKIN,		RT2870_1),
	USB_ID(BELKIN,		RT2870_2),
	USB_ID(BEWAN,		RT3070),
	USB_ID(CISCOLINKSYS,	AE1000),
	USB_ID(CISCOLINKSYS,	AM10),
	USB_ID(CISCOLINKSYS2,	RT3070),
	USB_ID(CISCOLINKSYS3,	RT3070),
	USB_ID(CONCEPTRONIC2,	RT2870_1),
	USB_ID(CONCEPTRONIC2,	RT2870_2),
	USB_ID(CONCEPTRONIC2,	RT2870_3),
	USB_ID(CONCEPTRONIC2,	RT2870_5),
	USB_ID(CONCEPTRONIC2,	RT2870_6),
	USB_ID(CONCEPTRONIC2,	RT2870_7),
	USB_ID(CONCEPTRONIC2,	RT2870_8),
	USB_ID(CONCEPTRONIC2,	RT3070_1),
	USB_ID(CONCEPTRONIC2,	RT3070_2),
	USB_ID(CONCEPTRONIC2,	RT3070_3),
	USB_ID(CONCEPTRONIC2,	VIGORN61),
	USB_ID(COREGA,		CGWLUSB300GNM),
	USB_ID(COREGA,		RT2870_1),
	USB_ID(COREGA,		RT2870_2),
	USB_ID(COREGA,		RT2870_3),
	USB_ID(COREGA,		RT3070),
	USB_ID(CYBERTAN,	RT2870),
	USB_ID(DLINK,		DWA125B2),
	USB_ID(DLINK,		DWA127),
	USB_ID(DLINK,		DWA130F1),
	USB_ID(DLINK,		DWA137A1),
	USB_ID(DLINK,		DWA140B3),
	USB_ID(DLINK,		DWA140D1),
	USB_ID(DLINK,		DWA160B2),
	USB_ID(DLINK,	 	DWA162),
	USB_ID(DLINK,		RT2870),
	USB_ID(DLINK,		RT3072),
	USB_ID(DLINK2,		DWA130),
	USB_ID(DLINK2,		RT2870_1),
	USB_ID(DLINK2,		RT2870_2),
	USB_ID(DLINK2,		RT3070_1),
	USB_ID(DLINK2,		RT3070_2),
	USB_ID(DLINK2,		RT3070_3),
	USB_ID(DLINK2,		RT3070_4),
	USB_ID(DLINK2,		RT3070_5),
	USB_ID(DLINK2,		RT3072),
	USB_ID(DLINK2,		RT3072_1),
	USB_ID(DVICO,		RT3070),
	USB_ID(EDIMAX,		EW7717),
	USB_ID(EDIMAX,		EW7718),
	USB_ID(EDIMAX,		EW7722UTN),
	USB_ID(EDIMAX,		RT2870_1),
	USB_ID(ENCORE,		RT3070_1),
	USB_ID(ENCORE,		RT3070_2),
	USB_ID(ENCORE,		RT3070_3),
	USB_ID(GIGABYTE,	GNWB31N),
	USB_ID(GIGABYTE,	GNWB32L),
	USB_ID(GIGABYTE,	RT2870_1),
	USB_ID(GIGASET,		RT3070_1),
	USB_ID(GIGASET,		RT3070_2),
	USB_ID(GUILLEMOT,	HWNU300),
	USB_ID(HAWKING,		HWDN2),
	USB_ID(HAWKING,		HWUN2),
	USB_ID(HAWKING,		RT2870_1),
	USB_ID(HAWKING,		RT2870_2),
	USB_ID(HAWKING,		RT2870_3),
	USB_ID(HAWKING,		RT2870_4),
	USB_ID(HAWKING,		RT2870_5),
	USB_ID(IODATA,		RT3072_1),
	USB_ID(IODATA,		RT3072_2),
	USB_ID(IODATA,		RT3072_3),
	USB_ID(IODATA,		RT3072_4),
	USB_ID(LINKSYS4,	RT3070),
	USB_ID(LINKSYS4,	WUSB100),
	USB_ID(LINKSYS4,	WUSB54GCV3),
	USB_ID(LINKSYS4,	WUSB600N),
	USB_ID(LINKSYS4,	WUSB600NV2),
	USB_ID(LOGITEC,		LANW150NU2),
	USB_ID(LOGITEC,		LANW300NU2),
	USB_ID(LOGITEC,		LANW300NU2S),
	USB_ID(LOGITEC,		RT2870_1),
	USB_ID(LOGITEC,		RT2870_2),
	USB_ID(LOGITEC,		RT2870_3),
	USB_ID(MELCO,		RT2870_1),
	USB_ID(MELCO,		RT2870_2),
	USB_ID(MELCO,		WLIUCAG300N),
	USB_ID(MELCO,		WLIUCG300N),
	USB_ID(MELCO,		WLIUCG301N),
	USB_ID(MELCO,		WLIUCGN),
	USB_ID(MELCO,		WLIUCGNHP),
	USB_ID(MELCO,		WLIUCGNM),
	USB_ID(MELCO,		WLIUCGNM2),
	USB_ID(MOTOROLA4,	RT2770),
	USB_ID(MOTOROLA4,	RT3070),
	USB_ID(MSI,		RT3070_1),
	USB_ID(MSI,		RT3070_2),
	USB_ID(MSI,		RT3070_3),
	USB_ID(MSI,		RT3070_4),
	USB_ID(MSI,		RT3070_5),
	USB_ID(MSI,		RT3070_6),
	USB_ID(MSI,		RT3070_7),
	USB_ID(MSI,		RT3070_8),
	USB_ID(MSI,		RT3070_9),
	USB_ID(MSI,		RT3070_10),
	USB_ID(MSI,		RT3070_11),
	USB_ID(MSI,		RT3070_12),
	USB_ID(MSI,		RT3070_13),
	USB_ID(MSI,		RT3070_14),
	USB_ID(MSI,		RT3070_15),
	USB_ID(OVISLINK,	RT3071),
	USB_ID(OVISLINK,	RT3072),
	USB_ID(PARA,		RT3070),
	USB_ID(PEGATRON,	RT2870),
	USB_ID(PEGATRON,	RT3070),
	USB_ID(PEGATRON,	RT3070_2),
	USB_ID(PEGATRON,	RT3070_3),
	USB_ID(PEGATRON,	RT3072),
	USB_ID(PHILIPS,		RT2870),
	USB_ID(PLANEX2,		GWUS300MINIS),
	USB_ID(PLANEX2,		GWUSMICRO300),
	USB_ID(PLANEX2,		GWUSMICRON),
	USB_ID(PLANEX2,		RT2870),
	USB_ID(PLANEX2,		RT3070),
	USB_ID(QCOM,		RT2870),
	USB_ID(QUANTA,		RT3070),
	USB_ID(RALINK,		RT2070),
	USB_ID(RALINK,		RT2770),
	USB_ID(RALINK,		RT2870),
	USB_ID(RALINK,		RT3070),
	USB_ID(RALINK,		RT3071),
	USB_ID(RALINK,		RT3072),
	USB_ID(RALINK,		RT3370),
	USB_ID(RALINK,		RT3572),
	USB_ID(RALINK,		RT3573),
	USB_ID(RALINK,		RT5370),
	USB_ID(RALINK,		RT5372),
	USB_ID(RALINK,		RT5572),
	USB_ID(RALINK,		RT8070),
	USB_ID(SAMSUNG,		WIS09ABGN),
	USB_ID(SAMSUNG2,	RT2870_1),
	USB_ID(SENAO,		RT2870_1),
	USB_ID(SENAO,		RT2870_2),
	USB_ID(SENAO,		RT2870_3),
	USB_ID(SENAO,		RT2870_4),
	USB_ID(SENAO,		RT3070),
	USB_ID(SENAO,		RT3071),
	USB_ID(SENAO,		RT3072_1),
	USB_ID(SENAO,		RT3072_2),
	USB_ID(SENAO,		RT3072_3),
	USB_ID(SENAO,		RT3072_4),
	USB_ID(SENAO,		RT3072_5),
	USB_ID(SITECOMEU,	WL302),
	USB_ID(SITECOMEU,	WL315),
	USB_ID(SITECOMEU,	WL321),
	USB_ID(SITECOMEU,	RT3070_3),
	USB_ID(SITECOMEU,	WL302),
	USB_ID(SITECOMEU,	WL344),
	USB_ID(SITECOMEU,	WL329),
	USB_ID(SITECOMEU,	WL345),
	USB_ID(SITECOMEU,	RT2870_1),
	USB_ID(SITECOMEU,	RT2870_2),
	USB_ID(SITECOMEU,	RT2870_3),
	USB_ID(SITECOMEU,	RT3070_1),
	USB_ID(SITECOMEU,	RT3072_3),
	USB_ID(SITECOMEU,	RT3072_4),
	USB_ID(SITECOMEU,	RT3072_5),
	USB_ID(SITECOMEU,	RT3072_6),
	USB_ID(SITECOMEU,	WL302),
	USB_ID(SITECOMEU,	WL315),
	USB_ID(SITECOMEU,	WL321),
	USB_ID(SITECOMEU,	WL324),
	USB_ID(SITECOMEU,	WL329),
	USB_ID(SITECOMEU,	WL343),
	USB_ID(SITECOMEU,	WL344),
	USB_ID(SITECOMEU,	WL345),
	USB_ID(SITECOMEU,	WL349V4),
	USB_ID(SITECOMEU,	WL608),
	USB_ID(SITECOMEU,	WLA4000),
	USB_ID(SITECOMEU,	WLA5000),
	USB_ID(SPARKLAN,	RT2870_1),
	USB_ID(SPARKLAN,	RT2870_2),
	USB_ID(SPARKLAN,	RT3070),
	USB_ID(SWEEX2,		LW153),
	USB_ID(SWEEX2,		LW303),
	USB_ID(SWEEX2,		LW313),
	USB_ID(TOSHIBA,		RT3070),
	USB_ID(UMEDIA,		RT2870_1),
	USB_ID(UMEDIA,		TEW645UB),
	USB_ID(ZCOM,		RT2870_1),
	USB_ID(ZCOM,		RT2870_2),
	USB_ID(ZINWELL,		RT2870_1),
	USB_ID(ZINWELL,		RT2870_2),
	USB_ID(ZINWELL,		RT3070),
	USB_ID(ZINWELL,		RT3072_1),
	USB_ID(ZINWELL,		RT3072_2),
	USB_ID(ZYXEL,		NWD2105),
	USB_ID(ZYXEL,		NWD211AN),
	USB_ID(ZYXEL,		RT2870_1),
	USB_ID(ZYXEL,		RT2870_2),
	USB_ID(ZYXEL,		RT3070)
};

int		run_match(struct device *, void *, void *);
void		run_attach(struct device *, struct device *, void *);
int		run_detach(struct device *, int);
int		run_alloc_rx_ring(struct run_softc *);
void		run_free_rx_ring(struct run_softc *);
int		run_alloc_tx_ring(struct run_softc *, int);
void		run_free_tx_ring(struct run_softc *, int);
int		run_load_microcode(struct run_softc *);
int		run_reset(struct run_softc *);
int		run_read(struct run_softc *, uint16_t, uint32_t *);
int		run_read_region_1(struct run_softc *, uint16_t, uint8_t *,
		    int);
int		run_write_2(struct run_softc *, uint16_t, uint16_t);
int		run_write(struct run_softc *, uint16_t, uint32_t);
int		run_write_region_1(struct run_softc *, uint16_t,
		    const uint8_t *, int);
int		run_set_region_4(struct run_softc *, uint16_t, uint32_t, int);
int		run_efuse_read(struct run_softc *, uint16_t, uint16_t *);
int		run_efuse_read_2(struct run_softc *, uint16_t, uint16_t *);
int		run_eeprom_read_2(struct run_softc *, uint16_t, uint16_t *);
int		run_rt2870_rf_write(struct run_softc *, uint8_t, uint32_t);
int		run_rt3070_rf_read(struct run_softc *, uint8_t, uint8_t *);
int		run_rt3070_rf_write(struct run_softc *, uint8_t, uint8_t);
int		run_bbp_read(struct run_softc *, uint8_t, uint8_t *);
int		run_bbp_write(struct run_softc *, uint8_t, uint8_t);
int		run_mcu_cmd(struct run_softc *, uint8_t, uint16_t);
const char *	run_get_rf(int);
void		run_get_txpower(struct run_softc *);
void		run_rt3593_get_txpower(struct run_softc *);
int		run_read_eeprom(struct run_softc *);
struct		ieee80211_node *run_node_alloc(struct ieee80211com *);
int		run_media_change(struct ifnet *);
void		run_next_scan(void *);
void		run_task(void *);
void		run_do_async(struct run_softc *, void (*)(struct run_softc *,
		    void *), void *, int);
int		run_newstate(struct ieee80211com *, enum ieee80211_state, int);
void		run_newstate_cb(struct run_softc *, void *);
void		run_updateedca(struct ieee80211com *);
void		run_updateedca_cb(struct run_softc *, void *);
int		run_set_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		run_set_key_cb(struct run_softc *, void *);
void		run_delete_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		run_delete_key_cb(struct run_softc *, void *);
void		run_calibrate_to(void *);
void		run_calibrate_cb(struct run_softc *, void *);
void		run_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);
void		run_rx_frame(struct run_softc *, uint8_t *, int,
		    struct mbuf_list *);
void		run_rxeof(struct usbd_xfer *, void *, usbd_status);
void		run_txeof(struct usbd_xfer *, void *, usbd_status);
int		run_tx(struct run_softc *, struct mbuf *,
		    struct ieee80211_node *);
void		run_start(struct ifnet *);
void		run_watchdog(struct ifnet *);
int		run_ioctl(struct ifnet *, u_long, caddr_t);
void		run_iq_calib(struct run_softc *, u_int);
void		run_select_chan_group(struct run_softc *, int);
void		run_set_agc(struct run_softc *, uint8_t);
void		run_set_rx_antenna(struct run_softc *, int);
void		run_rt2870_set_chan(struct run_softc *, u_int);
void		run_rt3070_set_chan(struct run_softc *, u_int);
void		run_rt3572_set_chan(struct run_softc *, u_int);
void		run_rt3593_set_chan(struct run_softc *, u_int);
void		run_rt5390_set_chan(struct run_softc *, u_int);
void		run_rt5592_set_chan(struct run_softc *, u_int);
int		run_set_chan(struct run_softc *, struct ieee80211_channel *);
void		run_enable_tsf_sync(struct run_softc *);
void		run_enable_mrr(struct run_softc *);
void		run_set_txpreamble(struct run_softc *);
void		run_set_basicrates(struct run_softc *);
void		run_set_leds(struct run_softc *, uint16_t);
void		run_set_bssid(struct run_softc *, const uint8_t *);
void		run_set_macaddr(struct run_softc *, const uint8_t *);
void		run_updateslot(struct ieee80211com *);
void		run_updateslot_cb(struct run_softc *, void *);
#if NBPFILTER > 0
int8_t		run_rssi2dbm(struct run_softc *, uint8_t, uint8_t);
#endif
void		run_rt5390_bbp_init(struct run_softc *);
int		run_bbp_init(struct run_softc *);
int		run_rt3070_rf_init(struct run_softc *);
void		run_rt3593_rf_init(struct run_softc *);
void		run_rt5390_rf_init(struct run_softc *);
int		run_rt3070_filter_calib(struct run_softc *, uint8_t, uint8_t,
		    uint8_t *);
void		run_rt3070_rf_setup(struct run_softc *);
void		run_rt3593_rf_setup(struct run_softc *);
void		run_rt5390_rf_setup(struct run_softc *);
int		run_txrx_enable(struct run_softc *);
void		run_adjust_freq_offset(struct run_softc *);
int		run_init(struct ifnet *);
void		run_stop(struct ifnet *, int);

struct cfdriver run_cd = {
	NULL, "run", DV_IFNET
};

const struct cfattach run_ca = {
	sizeof (struct run_softc), run_match, run_attach, run_detach
};

static const struct {
	uint32_t	reg;
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

int
run_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return UMATCH_NONE;

	return (usb_lookup(run_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE;
}

void
run_attach(struct device *parent, struct device *self, void *aux)
{
	struct run_softc *sc = (struct run_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i, nrx, ntx, ntries;
	uint32_t ver;

	sc->sc_udev = uaa->device;
	sc->sc_iface = uaa->iface;

	/*
	 * Find all bulk endpoints.  There are 7 bulk endpoints: 1 for RX
	 * and 6 for TX (4 EDCAs + HCCA + Prio).
	 * Update 03-14-2009:  some devices like the Planex GW-US300MiniS
	 * seem to have only 4 TX bulk endpoints (Fukaumi Naoki).
	 */
	nrx = ntx = 0;
	id = usbd_get_interface_descriptor(sc->sc_iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL || UE_GET_XFERTYPE(ed->bmAttributes) != UE_BULK)
			continue;

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN) {
			sc->rxq.pipe_no = ed->bEndpointAddress;
			nrx++;
		} else if (ntx < 4) {
			sc->txq[ntx].pipe_no = ed->bEndpointAddress;
			ntx++;
		}
	}
	/* make sure we've got them all */
	if (nrx < 1 || ntx < 4) {
		printf("%s: missing endpoint\n", sc->sc_dev.dv_xname);
		return;
	}

	usb_init_task(&sc->sc_task, run_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->scan_to, run_next_scan, sc);
	timeout_set(&sc->calib_to, run_calibrate_to, sc);

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 10;

	/* wait for the chip to settle */
	for (ntries = 0; ntries < 100; ntries++) {
		if (run_read(sc, RT2860_ASIC_VER_ID, &ver) != 0)
			return;
		if (ver != 0 && ver != 0xffffffff)
			break;
		DELAY(10);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for NIC to initialize\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	sc->mac_ver = ver >> 16;
	sc->mac_rev = ver & 0xffff;

	/* retrieve RF rev. no and various other things from EEPROM */
	run_read_eeprom(sc);

	printf("%s: MAC/BBP RT%04X (rev 0x%04X), RF %s (MIMO %dT%dR), "
	    "address %s\n", sc->sc_dev.dv_xname, sc->mac_ver,
	    sc->mac_rev, run_get_rf(sc->rf_rev), sc->ntxchains,
	    sc->nrxchains, ether_sprintf(ic->ic_myaddr));

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_WEP |		/* WEP */
	    IEEE80211_C_RSN;		/* WPA/RSN */

	if (sc->rf_rev == RT2860_RF_2750 ||
	    sc->rf_rev == RT2860_RF_2850 ||
	    sc->rf_rev == RT3070_RF_3052 ||
	    sc->rf_rev == RT3070_RF_3053 ||
	    sc->rf_rev == RT5592_RF_5592) {
		/* set supported .11a rates */
		ic->ic_sup_rates[IEEE80211_MODE_11A] =
		    ieee80211_std_rateset_11a;

		/* set supported .11a channels */
		for (i = 14; i < nitems(rt2860_rf2850); i++) {
			uint8_t chan = rt2860_rf2850[i].chan;
			ic->ic_channels[chan].ic_freq =
			    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[chan].ic_flags = IEEE80211_CHAN_A;
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
	ifp->if_ioctl = run_ioctl;
	ifp->if_start = run_start;
	ifp->if_watchdog = run_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_node_alloc = run_node_alloc;
	ic->ic_newassoc = run_newassoc;
	ic->ic_updateslot = run_updateslot;
	ic->ic_updateedca = run_updateedca;
	ic->ic_set_key = run_set_key;
	ic->ic_delete_key = run_delete_key;
	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = run_newstate;
	ieee80211_media_init(ifp, run_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(RUN_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(RUN_TX_RADIOTAP_PRESENT);
#endif
}

int
run_detach(struct device *self, int flags)
{
	struct run_softc *sc = (struct run_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int qid, s;

	s = splusb();

	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
	if (timeout_initialized(&sc->calib_to))
		timeout_del(&sc->calib_to);

	/* wait for all queued asynchronous commands to complete */
	usb_rem_wait_task(sc->sc_udev, &sc->sc_task);

	usbd_ref_wait(sc->sc_udev);

	if (ifp->if_softc != NULL) {
		ifp->if_flags &= ~IFF_RUNNING;
		ifq_clr_oactive(&ifp->if_snd);
		ieee80211_ifdetach(ifp);
		if_detach(ifp);
	}

	for (qid = 0; qid < 4; qid++)
		run_free_tx_ring(sc, qid);
	run_free_rx_ring(sc);

	splx(s);

	return 0;
}

int
run_alloc_rx_ring(struct run_softc *sc)
{
	struct run_rx_ring *rxq = &sc->rxq;
	int i, error;

	error = usbd_open_pipe(sc->sc_iface, rxq->pipe_no, 0, &rxq->pipeh);
	if (error != 0)
		goto fail;

	for (i = 0; i < RUN_RX_RING_COUNT; i++) {
		struct run_rx_data *data = &rxq->data[i];

		data->sc = sc;	/* backpointer for callbacks */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			error = ENOMEM;
			goto fail;
		}
		data->buf = usbd_alloc_buffer(data->xfer, RUN_MAX_RXSZ);
		if (data->buf == NULL) {
			error = ENOMEM;
			goto fail;
		}
	}
	if (error != 0)
fail:		run_free_rx_ring(sc);
	return error;
}

void
run_free_rx_ring(struct run_softc *sc)
{
	struct run_rx_ring *rxq = &sc->rxq;
	int i;

	if (rxq->pipeh != NULL) {
		usbd_close_pipe(rxq->pipeh);
		rxq->pipeh = NULL;
	}
	for (i = 0; i < RUN_RX_RING_COUNT; i++) {
		if (rxq->data[i].xfer != NULL)
			usbd_free_xfer(rxq->data[i].xfer);
		rxq->data[i].xfer = NULL;
	}
}

int
run_alloc_tx_ring(struct run_softc *sc, int qid)
{
	struct run_tx_ring *txq = &sc->txq[qid];
	int i, error;
	uint16_t txwisize;

	txwisize = sizeof(struct rt2860_txwi);
	if (sc->mac_ver == 0x5592)
		txwisize += sizeof(uint32_t);

	txq->cur = txq->queued = 0;

	error = usbd_open_pipe(sc->sc_iface, txq->pipe_no, 0, &txq->pipeh);
	if (error != 0)
		goto fail;

	for (i = 0; i < RUN_TX_RING_COUNT; i++) {
		struct run_tx_data *data = &txq->data[i];

		data->sc = sc;	/* backpointer for callbacks */
		data->qid = qid;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			error = ENOMEM;
			goto fail;
		}
		data->buf = usbd_alloc_buffer(data->xfer, RUN_MAX_TXSZ);
		if (data->buf == NULL) {
			error = ENOMEM;
			goto fail;
		}
		/* zeroize the TXD + TXWI part */
		memset(data->buf, 0, sizeof(struct rt2870_txd) + txwisize);
	}
	if (error != 0)
fail:		run_free_tx_ring(sc, qid);
	return error;
}

void
run_free_tx_ring(struct run_softc *sc, int qid)
{
	struct run_tx_ring *txq = &sc->txq[qid];
	int i;

	if (txq->pipeh != NULL) {
		usbd_close_pipe(txq->pipeh);
		txq->pipeh = NULL;
	}
	for (i = 0; i < RUN_TX_RING_COUNT; i++) {
		if (txq->data[i].xfer != NULL)
			usbd_free_xfer(txq->data[i].xfer);
		txq->data[i].xfer = NULL;
	}
}

int
run_load_microcode(struct run_softc *sc)
{
	usb_device_request_t req;
	const char *fwname;
	u_char *ucode;
	size_t size;
	uint32_t tmp;
	int ntries, error;

	/* RT3071/RT3072 use a different firmware */
	if (sc->mac_ver != 0x2860 &&
	    sc->mac_ver != 0x2872 &&
	    sc->mac_ver != 0x3070)
		fwname = "run-rt3071";
	else
		fwname = "run-rt2870";

	if ((error = loadfirmware(fwname, &ucode, &size)) != 0) {
		printf("%s: failed loadfirmware of file %s (error %d)\n",
		    sc->sc_dev.dv_xname, fwname, error);
		return error;
	}
	if (size != 4096) {
		printf("%s: invalid firmware size (should be 4KB)\n",
		    sc->sc_dev.dv_xname);
		free(ucode, M_DEVBUF, size);
		return EINVAL;
	}

	/* write microcode image */
	run_write_region_1(sc, RT2870_FW_BASE, ucode, size);
	free(ucode, M_DEVBUF, size);
	run_write(sc, RT2860_H2M_MAILBOX_CID, 0xffffffff);
	run_write(sc, RT2860_H2M_MAILBOX_STATUS, 0xffffffff);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2870_RESET;
	USETW(req.wValue, 8);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	if ((error = usbd_do_request(sc->sc_udev, &req, NULL)) != 0)
		return error;

	usbd_delay_ms(sc->sc_udev, 10);
	run_write(sc, RT2860_H2M_BBPAGENT, 0);
	run_write(sc, RT2860_H2M_MAILBOX, 0);
	run_write(sc, RT2860_H2M_INTSRC, 0);
	if ((error = run_mcu_cmd(sc, RT2860_MCU_CMD_RFRESET, 0)) != 0)
		return error;

	/* wait until microcontroller is ready */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((error = run_read(sc, RT2860_SYS_CTRL, &tmp)) != 0)
			return error;
		if (tmp & RT2860_MCU_READY)
			break;
		DELAY(1000);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for MCU to initialize\n",
		    sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}
	DPRINTF(("microcode successfully loaded after %d tries\n", ntries));
	return 0;
}

int
run_reset(struct run_softc *sc)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2870_RESET;
	USETW(req.wValue, 1);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return usbd_do_request(sc->sc_udev, &req, NULL);
}

int
run_read(struct run_softc *sc, uint16_t reg, uint32_t *val)
{
	uint32_t tmp;
	int error;

	error = run_read_region_1(sc, reg, (uint8_t *)&tmp, sizeof tmp);
	if (error == 0)
		*val = letoh32(tmp);
	else
		*val = 0xffffffff;
	return error;
}

int
run_read_region_1(struct run_softc *sc, uint16_t reg, uint8_t *buf, int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = RT2870_READ_REGION_1;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);
	return usbd_do_request(sc->sc_udev, &req, buf);
}

int
run_write_2(struct run_softc *sc, uint16_t reg, uint16_t val)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2870_WRITE_2;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);
	return usbd_do_request(sc->sc_udev, &req, NULL);
}

int
run_write(struct run_softc *sc, uint16_t reg, uint32_t val)
{
	int error;

	if ((error = run_write_2(sc, reg, val & 0xffff)) == 0)
		error = run_write_2(sc, reg + 2, val >> 16);
	return error;
}

int
run_write_region_1(struct run_softc *sc, uint16_t reg, const uint8_t *buf,
    int len)
{
#if 1
	int i, error = 0;
	/*
	 * NB: the WRITE_REGION_1 command is not stable on RT2860.
	 * We thus issue multiple WRITE_2 commands instead.
	 */
	KASSERT((len & 1) == 0);
	for (i = 0; i < len && error == 0; i += 2)
		error = run_write_2(sc, reg + i, buf[i] | buf[i + 1] << 8);
	return error;
#else
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = RT2870_WRITE_REGION_1;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, len);
	return usbd_do_request(sc->sc_udev, &req, buf);
#endif
}

int
run_set_region_4(struct run_softc *sc, uint16_t reg, uint32_t val, int count)
{
	int error = 0;

	for (; count > 0 && error == 0; count--, reg += 4)
		error = run_write(sc, reg, val);
	return error;
}

/* Read 16-bit from eFUSE ROM. */
int
run_efuse_read(struct run_softc *sc, uint16_t addr, uint16_t *val)
{
	uint32_t tmp;
	uint16_t reg;
	int error, ntries;

	if ((error = run_read(sc, RT3070_EFUSE_CTRL, &tmp)) != 0)
		return error;

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
			return error;
		if (!(tmp & RT3070_EFSROM_KICK))
			break;
		DELAY(2);
	}
	if (ntries == 100)
		return ETIMEDOUT;

	if ((tmp & RT3070_EFUSE_AOUT_MASK) == RT3070_EFUSE_AOUT_MASK) {
		*val = 0xffff;	/* address not found */
		return 0;
	}
	/* determine to which 32-bit register our 16-bit word belongs */
	reg = RT3070_EFUSE_DATA3 - (addr & 0xc);
	if ((error = run_read(sc, reg, &tmp)) != 0)
		return error;

	tmp >>= (8 * (addr & 0x3));
	*val = (addr & 1) ? tmp >> 16 : tmp & 0xffff;
	return 0;
}

/* Read 16-bit from eFUSE ROM for RT3xxx. */
int
run_efuse_read_2(struct run_softc *sc, uint16_t addr, uint16_t *val)
{
	uint32_t tmp;
	uint16_t reg;
	int error, ntries;

	if ((error = run_read(sc, RT3070_EFUSE_CTRL, &tmp)) != 0)
		return error;

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
			return error;
		if (!(tmp & RT3070_EFSROM_KICK))
			break;
		DELAY(2);
	}
	if (ntries == 100)
		return ETIMEDOUT;

	if ((tmp & RT3070_EFUSE_AOUT_MASK) == RT3070_EFUSE_AOUT_MASK) {
		*val = 0xffff;	/* address not found */
		return 0;
	}
	/* determine to which 32-bit register our 16-bit word belongs */
	reg = RT3070_EFUSE_DATA3 - (addr & 0xc);
	if ((error = run_read(sc, reg, &tmp)) != 0)
		return error;

	*val = (addr & 2) ? tmp >> 16 : tmp & 0xffff;
	return 0;
}

int
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
	USETW(req.wLength, sizeof tmp);
	error = usbd_do_request(sc->sc_udev, &req, &tmp);
	if (error == 0)
		*val = letoh16(tmp);
	else
		*val = 0xffff;
	return error;
}

static __inline int
run_srom_read(struct run_softc *sc, uint16_t addr, uint16_t *val)
{
	/* either eFUSE ROM or EEPROM */
	return sc->sc_srom_read(sc, addr, val);
}

int
run_rt2870_rf_write(struct run_softc *sc, uint8_t reg, uint32_t val)
{
	uint32_t tmp;
	int error, ntries;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = run_read(sc, RT2860_RF_CSR_CFG0, &tmp)) != 0)
			return error;
		if (!(tmp & RT2860_RF_REG_CTRL))
			break;
	}
	if (ntries == 10)
		return ETIMEDOUT;

	/* RF registers are 24-bit on the RT2860 */
	tmp = RT2860_RF_REG_CTRL | 24 << RT2860_RF_REG_WIDTH_SHIFT |
	    (val & 0x3fffff) << 2 | (reg & 3);
	return run_write(sc, RT2860_RF_CSR_CFG0, tmp);
}

int
run_rt3070_rf_read(struct run_softc *sc, uint8_t reg, uint8_t *val)
{
	uint32_t tmp;
	int error, ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = run_read(sc, RT3070_RF_CSR_CFG, &tmp)) != 0)
			return error;
		if (!(tmp & RT3070_RF_KICK))
			break;
	}
	if (ntries == 100)
		return ETIMEDOUT;

	tmp = RT3070_RF_KICK | reg << 8;
	if ((error = run_write(sc, RT3070_RF_CSR_CFG, tmp)) != 0)
		return error;

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = run_read(sc, RT3070_RF_CSR_CFG, &tmp)) != 0)
			return error;
		if (!(tmp & RT3070_RF_KICK))
			break;
	}
	if (ntries == 100)
		return ETIMEDOUT;

	*val = tmp & 0xff;
	return 0;
}

int
run_rt3070_rf_write(struct run_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int error, ntries;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = run_read(sc, RT3070_RF_CSR_CFG, &tmp)) != 0)
			return error;
		if (!(tmp & RT3070_RF_KICK))
			break;
	}
	if (ntries == 10)
		return ETIMEDOUT;

	tmp = RT3070_RF_WRITE | RT3070_RF_KICK | reg << 8 | val;
	return run_write(sc, RT3070_RF_CSR_CFG, tmp);
}

int
run_bbp_read(struct run_softc *sc, uint8_t reg, uint8_t *val)
{
	uint32_t tmp;
	int ntries, error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = run_read(sc, RT2860_BBP_CSR_CFG, &tmp)) != 0)
			return error;
		if (!(tmp & RT2860_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return ETIMEDOUT;

	tmp = RT2860_BBP_CSR_READ | RT2860_BBP_CSR_KICK | reg << 8;
	if ((error = run_write(sc, RT2860_BBP_CSR_CFG, tmp)) != 0)
		return error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = run_read(sc, RT2860_BBP_CSR_CFG, &tmp)) != 0)
			return error;
		if (!(tmp & RT2860_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return ETIMEDOUT;

	*val = tmp & 0xff;
	return 0;
}

int
run_bbp_write(struct run_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int ntries, error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = run_read(sc, RT2860_BBP_CSR_CFG, &tmp)) != 0)
			return error;
		if (!(tmp & RT2860_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return ETIMEDOUT;

	tmp = RT2860_BBP_CSR_KICK | reg << 8 | val;
	return run_write(sc, RT2860_BBP_CSR_CFG, tmp);
}

/*
 * Send a command to the 8051 microcontroller unit.
 */
int
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
	return error;
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
	return b32;
}

const char *
run_get_rf(int rev)
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
	case RT3070_RF_3053:	return "RT3053";
	case RT5592_RF_5592:	return "RT5592";
	case RT5390_RF_5370:	return "RT5370";
	case RT5390_RF_5372:	return "RT5372";
	}
	return "unknown";
}

void
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

void
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
			if (sc->txpow1[i] < 0 || sc->txpow1[i] > 27)
				sc->txpow1[i] = 5;
		} else {
			if (sc->txpow1[i] < 0 || sc->txpow1[i] > 31)
				sc->txpow1[i] = 5;
		}
		if (sc->mac_ver > 0x5390) {
			if (sc->txpow2[i] < 0 || sc->txpow2[i] > 27)
				sc->txpow2[i] = 5;
		} else if (sc->mac_ver < 0x5390) {
			if (sc->txpow2[i] < 0 || sc->txpow2[i] > 31)
				sc->txpow2[i] = 5;
		}
		DPRINTF(("chan %d: power1=%d, power2=%d\n",
		    rt2860_rf2850[i].chan, sc->txpow1[i], sc->txpow2[i]));
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
		DPRINTF(("chan %d: power1=%d, power2=%d\n",
		    rt2860_rf2850[14 + i].chan, sc->txpow1[14 + i],
		    sc->txpow2[14 + i]));
	}
}

int
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
		DPRINTF(("EFUSE_CTRL=0x%08x\n", tmp));
		if (tmp & RT3070_SEL_EFUSE || sc->mac_ver == 0x3593)
			sc->sc_srom_read = run_efuse_read_2;
	}

	/* read ROM version */
	run_srom_read(sc, RT2860_EEPROM_VERSION, &val);
	DPRINTF(("EEPROM rev=%d, FAE=%d\n", val & 0xff, val >> 8));

	/* read MAC address */
	run_srom_read(sc, RT2860_EEPROM_MAC01, &val);
	ic->ic_myaddr[0] = val & 0xff;
	ic->ic_myaddr[1] = val >> 8;
	run_srom_read(sc, RT2860_EEPROM_MAC23, &val);
	ic->ic_myaddr[2] = val & 0xff;
	ic->ic_myaddr[3] = val >> 8;
	run_srom_read(sc, RT2860_EEPROM_MAC45, &val);
	ic->ic_myaddr[4] = val & 0xff;
	ic->ic_myaddr[5] = val >> 8;

	if (sc->mac_ver < 0x3593) {
		/* read vendor BBP settings */
		for (i = 0; i < 10; i++) {
			run_srom_read(sc, RT2860_EEPROM_BBP_BASE + i, &val);
			sc->bbp[i].val = val & 0xff;
			sc->bbp[i].reg = val >> 8;
			DPRINTF(("BBP%d=0x%02x\n", sc->bbp[i].reg,
			    sc->bbp[i].val));
		}
		if (sc->mac_ver >= 0x3071) {
			/* read vendor RF settings */
			for (i = 0; i < 10; i++) {
				run_srom_read(sc, RT3071_EEPROM_RF_BASE + i,
				    &val);
				sc->rf[i].val = val & 0xff;
				sc->rf[i].reg = val >> 8;
				DPRINTF(("RF%d=0x%02x\n", sc->rf[i].reg,
				    sc->rf[i].val));
			}
		}
	}

	/* read RF frequency offset from EEPROM */
	run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_FREQ_LEDS :
	    RT3593_EEPROM_FREQ, &val);
	sc->freq = ((val & 0xff) != 0xff) ? val & 0xff : 0;
	DPRINTF(("EEPROM freq offset %d\n", sc->freq & 0xff));
	run_srom_read(sc, (sc->mac_ver != 0x3593) ? RT2860_EEPROM_FREQ_LEDS :
	    RT3593_EEPROM_FREQ_LEDS, &val);
	if ((val >> 8) != 0xff) {
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
	DPRINTF(("EEPROM LED mode=0x%02x, LEDs=0x%04x/0x%04x/0x%04x\n",
	    sc->leds, sc->led[0], sc->led[1], sc->led[2]));

	/* read RF information */
	if (sc->mac_ver == 0x5390 || sc->mac_ver == 0x5392)
		run_srom_read(sc, 0x00, &val);
	else
		run_srom_read(sc, RT2860_EEPROM_ANTENNA, &val);
	if (val == 0xffff) {
		DPRINTF(("invalid EEPROM antenna info, using default\n"));
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
		if (sc->mac_ver == 0x5390 || sc->mac_ver == 0x5392) {
			sc->rf_rev = val;
			run_srom_read(sc, RT2860_EEPROM_ANTENNA, &val);
		} else
			sc->rf_rev = (val >> 8) & 0xf;
		sc->ntxchains = (val >> 4) & 0xf;
		sc->nrxchains = val & 0xf;
	}
	DPRINTF(("EEPROM RF rev=0x%02x chains=%dT%dR\n",
	    sc->rf_rev, sc->ntxchains, sc->nrxchains));

	/* check if RF supports automatic Tx access gain control */
	run_srom_read(sc, RT2860_EEPROM_CONFIG, &val);
	DPRINTF(("EEPROM CFG 0x%04x\n", val));
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
	DPRINTF(("power compensation=%d (2GHz), %d (5GHz)\n",
	    delta_2ghz, delta_5ghz));

	for (ridx = 0; ridx < 5; ridx++) {
		uint32_t reg;

		run_srom_read(sc, RT2860_EEPROM_RPWR + ridx * 2, &val);
		reg = val;
		run_srom_read(sc, RT2860_EEPROM_RPWR + ridx * 2 + 1, &val);
		reg |= (uint32_t)val << 16;

		sc->txpow20mhz[ridx] = reg;
		sc->txpow40mhz_2ghz[ridx] = b4inc(reg, delta_2ghz);
		sc->txpow40mhz_5ghz[ridx] = b4inc(reg, delta_5ghz);

		DPRINTF(("ridx %d: power 20MHz=0x%08x, 40MHz/2GHz=0x%08x, "
		    "40MHz/5GHz=0x%08x\n", ridx, sc->txpow20mhz[ridx],
		    sc->txpow40mhz_2ghz[ridx], sc->txpow40mhz_5ghz[ridx]));
	}

	/* read RSSI offsets and LNA gains from EEPROM */
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
		DPRINTF(("tx mixer gain=%u (2GHz)\n", sc->txmixgain_2ghz));
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
		DPRINTF(("tx mixer gain=%u (5GHz)\n", sc->txmixgain_5ghz));
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
		DPRINTF(("invalid LNA for channel group %d\n", 2));
		sc->lna[2] = sc->lna[1];
	}
	if (sc->lna[3] == 0 || sc->lna[3] == 0xff) {
		DPRINTF(("invalid LNA for channel group %d\n", 3));
		sc->lna[3] = sc->lna[1];
	}

	/* fix broken RSSI offset entries */
	for (ant = 0; ant < 3; ant++) {
		if (sc->rssi_2ghz[ant] < -10 || sc->rssi_2ghz[ant] > 10) {
			DPRINTF(("invalid RSSI%d offset: %d (2GHz)\n",
			    ant + 1, sc->rssi_2ghz[ant]));
			sc->rssi_2ghz[ant] = 0;
		}
		if (sc->rssi_5ghz[ant] < -10 || sc->rssi_5ghz[ant] > 10) {
			DPRINTF(("invalid RSSI%d offset: %d (5GHz)\n",
			    ant + 1, sc->rssi_5ghz[ant]));
			sc->rssi_5ghz[ant] = 0;
		}
	}
	return 0;
}

struct ieee80211_node *
run_node_alloc(struct ieee80211com *ic)
{
	return malloc(sizeof (struct run_node), M_USBDEV, M_NOWAIT | M_ZERO);
}

int
run_media_change(struct ifnet *ifp)
{
	struct run_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rate, ridx;
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if (ic->ic_fixed_rate != -1) {
		rate = ic->ic_sup_rates[ic->ic_curmode].
		    rs_rates[ic->ic_fixed_rate] & IEEE80211_RATE_VAL;
		for (ridx = 0; ridx <= RT2860_RIDX_MAX; ridx++)
			if (rt2860_rates[ridx].rate == rate)
				break;
		sc->fixed_ridx = ridx;
	}

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING)) {
		run_stop(ifp, 0);
		error = run_init(ifp);
	}

	return error;
}

void
run_next_scan(void *arg)
{
	struct run_softc *sc = arg;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);

	s = splnet();
	if (sc->sc_ic.ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(&sc->sc_ic.ic_if);
	splx(s);

	usbd_ref_decr(sc->sc_udev);
}

void
run_task(void *arg)
{
	struct run_softc *sc = arg;
	struct run_host_cmd_ring *ring = &sc->cmdq;
	struct run_host_cmd *cmd;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	/* process host commands */
	s = splusb();
	while (ring->next != ring->cur) {
		cmd = &ring->cmd[ring->next];
		splx(s);
		/* callback */
		cmd->cb(sc, cmd->data);
		s = splusb();
		ring->queued--;
		ring->next = (ring->next + 1) % RUN_HOST_CMD_RING_COUNT;
	}
	splx(s);
}

void
run_do_async(struct run_softc *sc, void (*cb)(struct run_softc *, void *),
    void *arg, int len)
{
	struct run_host_cmd_ring *ring = &sc->cmdq;
	struct run_host_cmd *cmd;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	s = splusb();
	cmd = &ring->cmd[ring->cur];
	cmd->cb = cb;
	KASSERT(len <= sizeof (cmd->data));
	memcpy(cmd->data, arg, len);
	ring->cur = (ring->cur + 1) % RUN_HOST_CMD_RING_COUNT;

	/* if there is no pending command already, schedule a task */
	if (++ring->queued == 1)
		usb_add_task(sc->sc_udev, &sc->sc_task);
	splx(s);
}

int
run_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct run_softc *sc = ic->ic_softc;
	struct run_cmd_newstate cmd;

	/* do it in a process context */
	cmd.state = nstate;
	cmd.arg = arg;
	run_do_async(sc, run_newstate_cb, &cmd, sizeof cmd);
	return 0;
}

void
run_newstate_cb(struct run_softc *sc, void *arg)
{
	struct run_cmd_newstate *cmd = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state ostate;
	struct ieee80211_node *ni;
	uint32_t tmp, sta[3];
	uint8_t wcid;
	int s;

	s = splnet();
	ostate = ic->ic_state;

	if (ostate == IEEE80211_S_RUN) {
		/* turn link LED off */
		run_set_leds(sc, RT2860_LED_RADIO);
	}

	switch (cmd->state) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* abort TSF synchronization */
			run_read(sc, RT2860_BCN_TIME_CFG, &tmp);
			run_write(sc, RT2860_BCN_TIME_CFG,
			    tmp & ~(RT2860_BCN_TX_EN | RT2860_TSF_TIMER_EN |
			    RT2860_TBTT_TIMER_EN));
		}
		break;

	case IEEE80211_S_SCAN:
		run_set_chan(sc, ic->ic_bss->ni_chan);
		if (!usbd_is_dying(sc->sc_udev))
			timeout_add_msec(&sc->scan_to, 200);
		break;

	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		run_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_RUN:
		run_set_chan(sc, ic->ic_bss->ni_chan);

		ni = ic->ic_bss;

		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			run_updateslot(ic);
			run_enable_mrr(sc);
			run_set_txpreamble(sc);
			run_set_basicrates(sc);
			run_set_bssid(sc, ni->ni_bssid);
		}
		if (ic->ic_opmode == IEEE80211_M_STA) {
			/* add BSS entry to the WCID table */
			wcid = RUN_AID2WCID(ni->ni_associd);
			run_write_region_1(sc, RT2860_WCID_ENTRY(wcid),
			    ni->ni_macaddr, IEEE80211_ADDR_LEN);

			/* fake a join to init the tx rate */
			run_newassoc(ic, ni, 1);
		}
		if (ic->ic_opmode != IEEE80211_M_MONITOR) {
			run_enable_tsf_sync(sc);

			/* clear statistic registers used by AMRR */
			run_read_region_1(sc, RT2860_TX_STA_CNT0,
			    (uint8_t *)sta, sizeof sta);
			/* start calibration timer */
			if (!usbd_is_dying(sc->sc_udev))
				timeout_add_sec(&sc->calib_to, 1);
		}

		/* turn link LED on */
		run_set_leds(sc, RT2860_LED_RADIO |
		    (IEEE80211_IS_CHAN_2GHZ(ic->ic_bss->ni_chan) ?
		     RT2860_LED_LINK_2GHZ : RT2860_LED_LINK_5GHZ));
		break;
	}
	(void)sc->sc_newstate(ic, cmd->state, cmd->arg);
	splx(s);
}

void
run_updateedca(struct ieee80211com *ic)
{
	/* do it in a process context */
	run_do_async(ic->ic_softc, run_updateedca_cb, NULL, 0);
}

void
run_updateedca_cb(struct run_softc *sc, void *arg)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int s, aci;

	s = splnet();
	/* update MAC TX configuration registers */
	for (aci = 0; aci < EDCA_NUM_AC; aci++) {
		run_write(sc, RT2860_EDCA_AC_CFG(aci),
		    ic->ic_edca_ac[aci].ac_ecwmax << 16 |
		    ic->ic_edca_ac[aci].ac_ecwmin << 12 |
		    ic->ic_edca_ac[aci].ac_aifsn  <<  8 |
		    ic->ic_edca_ac[aci].ac_txoplimit);
	}

	/* update SCH/DMA registers too */
	run_write(sc, RT2860_WMM_AIFSN_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_aifsn  << 12 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_aifsn  <<  8 |
	    ic->ic_edca_ac[EDCA_AC_BK].ac_aifsn  <<  4 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_aifsn);
	run_write(sc, RT2860_WMM_CWMIN_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_ecwmin << 12 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_ecwmin <<  8 |
	    ic->ic_edca_ac[EDCA_AC_BK].ac_ecwmin <<  4 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_ecwmin);
	run_write(sc, RT2860_WMM_CWMAX_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_ecwmax << 12 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_ecwmax <<  8 |
	    ic->ic_edca_ac[EDCA_AC_BK].ac_ecwmax <<  4 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_ecwmax);
	run_write(sc, RT2860_WMM_TXOP0_CFG,
	    ic->ic_edca_ac[EDCA_AC_BK].ac_txoplimit << 16 |
	    ic->ic_edca_ac[EDCA_AC_BE].ac_txoplimit);
	run_write(sc, RT2860_WMM_TXOP1_CFG,
	    ic->ic_edca_ac[EDCA_AC_VO].ac_txoplimit << 16 |
	    ic->ic_edca_ac[EDCA_AC_VI].ac_txoplimit);
	splx(s);
}

int
run_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct run_softc *sc = ic->ic_softc;
	struct run_cmd_key cmd;

	/* defer setting of WEP keys until interface is brought up */
	if ((ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) !=
	    (IFF_UP | IFF_RUNNING))
		return 0;

	/* do it in a process context */
	cmd.key = *k;
	cmd.ni = ni;
	run_do_async(sc, run_set_key_cb, &cmd, sizeof cmd);
	sc->sc_key_tasks++;

	return EBUSY;
}

void
run_set_key_cb(struct run_softc *sc, void *arg)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct run_cmd_key *cmd = arg;
	struct ieee80211_key *k = &cmd->key;
	uint32_t attr;
	uint16_t base;
	uint8_t mode, wcid, iv[8];

	sc->sc_key_tasks--;

	/* map net80211 cipher to RT2860 security mode */
	switch (k->k_cipher) {
	case IEEE80211_CIPHER_WEP40:
		mode = RT2860_MODE_WEP40;
		break;
	case IEEE80211_CIPHER_WEP104:
		mode = RT2860_MODE_WEP104;
		break;
	case IEEE80211_CIPHER_TKIP:
		mode = RT2860_MODE_TKIP;
		break;
	case IEEE80211_CIPHER_CCMP:
		mode = RT2860_MODE_AES_CCMP;
		break;
	default:
		IEEE80211_SEND_MGMT(ic, cmd->ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_AUTH_LEAVE);
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		return;
	}

	if (k->k_flags & IEEE80211_KEY_GROUP) {
		wcid = 0;	/* NB: update WCID0 for group keys */
		base = RT2860_SKEY(0, k->k_id);
	} else {
		wcid = (cmd->ni != NULL) ? RUN_AID2WCID(cmd->ni->ni_associd) : 0;
		base = RT2860_PKEY(wcid);
	}

	if (k->k_cipher == IEEE80211_CIPHER_TKIP) {
		run_write_region_1(sc, base, k->k_key, 16);
		run_write_region_1(sc, base + 16, &k->k_key[24], 8);
		run_write_region_1(sc, base + 24, &k->k_key[16], 8);
	} else {
		/* roundup len to 16-bit: XXX fix write_region_1() instead */
		run_write_region_1(sc, base, k->k_key, (k->k_len + 1) & ~1);
	}

	if (!(k->k_flags & IEEE80211_KEY_GROUP) ||
	    (k->k_flags & IEEE80211_KEY_TX)) {
		/* set initial packet number in IV+EIV */
		if (k->k_cipher == IEEE80211_CIPHER_WEP40 ||
		    k->k_cipher == IEEE80211_CIPHER_WEP104) {
			memset(iv, 0, sizeof iv);
			iv[3] = sc->sc_ic.ic_def_txkey << 6;
		} else {
			if (k->k_cipher == IEEE80211_CIPHER_TKIP) {
				iv[0] = k->k_tsc >> 8;
				iv[1] = (iv[0] | 0x20) & 0x7f;
				iv[2] = k->k_tsc;
			} else /* CCMP */ {
				iv[0] = k->k_tsc;
				iv[1] = k->k_tsc >> 8;
				iv[2] = 0;
			}
			iv[3] = k->k_id << 6 | IEEE80211_WEP_EXTIV;
			iv[4] = k->k_tsc >> 16;
			iv[5] = k->k_tsc >> 24;
			iv[6] = k->k_tsc >> 32;
			iv[7] = k->k_tsc >> 40;
		}
		run_write_region_1(sc, RT2860_IVEIV(wcid), iv, 8);
	}

	if (k->k_flags & IEEE80211_KEY_GROUP) {
		/* install group key */
		run_read(sc, RT2860_SKEY_MODE_0_7, &attr);
		attr &= ~(0xf << (k->k_id * 4));
		attr |= mode << (k->k_id * 4);
		run_write(sc, RT2860_SKEY_MODE_0_7, attr);
	} else {
		/* install pairwise key */
		run_read(sc, RT2860_WCID_ATTR(wcid), &attr);
		attr = (attr & ~0xf) | (mode << 1) | RT2860_RX_PKEY_EN;
		run_write(sc, RT2860_WCID_ATTR(wcid), attr);
	}

	if (sc->sc_key_tasks == 0) {
		if (cmd->ni != NULL)
			cmd->ni->ni_port_valid = 1;
		ieee80211_set_link_state(ic, LINK_STATE_UP);
	}
}

void
run_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct run_softc *sc = ic->ic_softc;
	struct run_cmd_key cmd;

	if (!(ic->ic_if.if_flags & IFF_RUNNING) ||
	    ic->ic_state != IEEE80211_S_RUN)
		return;	/* nothing to do */

	/* do it in a process context */
	cmd.key = *k;
	cmd.ni = ni;
	run_do_async(sc, run_delete_key_cb, &cmd, sizeof cmd);
}

void
run_delete_key_cb(struct run_softc *sc, void *arg)
{
	struct run_cmd_key *cmd = arg;
	struct ieee80211_key *k = &cmd->key;
	uint32_t attr;
	uint8_t wcid;

	if (k->k_flags & IEEE80211_KEY_GROUP) {
		/* remove group key */
		run_read(sc, RT2860_SKEY_MODE_0_7, &attr);
		attr &= ~(0xf << (k->k_id * 4));
		run_write(sc, RT2860_SKEY_MODE_0_7, attr);

	} else {
		/* remove pairwise key */
		wcid = (cmd->ni != NULL) ? RUN_AID2WCID(cmd->ni->ni_associd) : 0;
		run_read(sc, RT2860_WCID_ATTR(wcid), &attr);
		attr &= ~0xf;
		run_write(sc, RT2860_WCID_ATTR(wcid), attr);
	}
}

void
run_calibrate_to(void *arg)
{
	/* do it in a process context */
	run_do_async(arg, run_calibrate_cb, NULL, 0);
	/* next timeout will be rescheduled in the calibration task */
}

void
run_calibrate_cb(struct run_softc *sc, void *arg)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t sta[3];
	int s, error;

	/* read statistic counters (clear on read) and update AMRR state */
	error = run_read_region_1(sc, RT2860_TX_STA_CNT0, (uint8_t *)sta,
	    sizeof sta);
	if (error != 0)
		goto skip;

	DPRINTF(("retrycnt=%d txcnt=%d failcnt=%d\n",
	    letoh32(sta[1]) >> 16, letoh32(sta[1]) & 0xffff,
	    letoh32(sta[0]) & 0xffff));

	s = splnet();
	/* count failed TX as errors */
	ifp->if_oerrors += letoh32(sta[0]) & 0xffff;

	sc->amn.amn_retrycnt =
	    (letoh32(sta[0]) & 0xffff) +	/* failed TX count */
	    (letoh32(sta[1]) >> 16);		/* TX retransmission count */

	sc->amn.amn_txcnt =
	    sc->amn.amn_retrycnt +
	    (letoh32(sta[1]) & 0xffff);		/* successful TX count */

	ieee80211_amrr_choose(&sc->amrr, sc->sc_ic.ic_bss, &sc->amn);
	splx(s);

skip:
	if (!usbd_is_dying(sc->sc_udev))
		timeout_add_sec(&sc->calib_to, 1);
}

void
run_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct run_softc *sc = ic->ic_softc;
	struct run_node *rn = (void *)ni;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	uint8_t rate;
	int ridx, i, j;

	DPRINTF(("new assoc isnew=%d addr=%s\n",
	    isnew, ether_sprintf(ni->ni_macaddr)));

	ieee80211_amrr_node_init(&sc->amrr, &sc->amn);
	/* start at lowest available bit-rate, AMRR will raise */
	ni->ni_txrate = 0;

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
		DPRINTF(("rate=0x%02x ridx=%d ctl_ridx=%d\n",
		    rs->rs_rates[i], rn->ridx[i], rn->ctl_ridx[i]));
	}
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
	return rxchain;
}

void
run_rx_frame(struct run_softc *sc, uint8_t *buf, int dmalen,
    struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct rt2870_rxd *rxd;
	struct rt2860_rxwi *rxwi;
	struct mbuf *m;
	uint32_t flags;
	uint16_t len;
#if NBPFILTER > 0
	uint16_t phy;
#endif
	uint16_t rxwisize;
	uint8_t ant, rssi;
	int s;

	rxwi = (struct rt2860_rxwi *)buf;
	rxwisize = sizeof(struct rt2860_rxwi);
	if (sc->mac_ver == 0x5592)
		rxwisize += sizeof(uint64_t);
	else if (sc->mac_ver == 0x3593)
		rxwisize += sizeof(uint32_t);
	len = letoh16(rxwi->len) & 0xfff;
	if (__predict_false(len > dmalen)) {
		DPRINTF(("bad RXWI length %u > %u\n", len, dmalen));
		return;
	}
	if (len > MCLBYTES) {
		DPRINTF(("frame too large (length=%d)\n", len));
		ifp->if_ierrors++;
		return;
	}
	/* Rx descriptor is located at the end */
	rxd = (struct rt2870_rxd *)(buf + dmalen);
	flags = letoh32(rxd->flags);

	if (__predict_false(flags & (RT2860_RX_CRCERR | RT2860_RX_ICVERR))) {
		ifp->if_ierrors++;
		return;
	}

	if (__predict_false((flags & RT2860_RX_MICERR))) {
		/* report MIC failures to net80211 for TKIP */
		ic->ic_stats.is_rx_locmicfail++;
		ieee80211_michael_mic_failure(ic, 0/* XXX */);
		ifp->if_ierrors++;
		return;
	}

	wh = (struct ieee80211_frame *)(buf + rxwisize);
	memset(&rxi, 0, sizeof(rxi));
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
		rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
	}

	if (flags & RT2860_RX_L2PAD) {
		u_int hdrlen = ieee80211_get_hdrlen(wh);
		memmove((caddr_t)wh + 2, wh, hdrlen);
		wh = (struct ieee80211_frame *)((caddr_t)wh + 2);
	}

	/* could use m_devget but net80211 wants contig mgmt frames */
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (__predict_false(m == NULL)) {
		ifp->if_ierrors++;
		return;
	}
	if (len > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if (__predict_false(!(m->m_flags & M_EXT))) {
			ifp->if_ierrors++;
			m_freem(m);
			return;
		}
	}
	/* finalize mbuf */
	memcpy(mtod(m, caddr_t), wh, len);
	m->m_pkthdr.len = m->m_len = len;

	ant = run_maxrssi_chain(sc, rxwi);
	rssi = rxwi->rssi[ant];

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct run_rx_radiotap_header *tap = &sc->sc_rxtap;
		struct mbuf mb;

		tap->wr_flags = 0;
		tap->wr_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);
		tap->wr_antsignal = rssi;
		tap->wr_antenna = ant;
		tap->wr_dbm_antsignal = run_rssi2dbm(sc, rssi, ant);
		tap->wr_rate = 2;	/* in case it can't be found below */
		phy = letoh16(rxwi->phy);
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
	rxi.rxi_rssi = rssi;
	ieee80211_inputm(ifp, m, ni, &rxi, ml);

	/* node is no longer needed */
	ieee80211_release_node(ic, ni);
	splx(s);
}

void
run_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct run_rx_data *data = priv;
	struct run_softc *sc = data->sc;
	uint8_t *buf;
	uint32_t dmalen;
	int xferlen;
	uint16_t rxwisize;

	rxwisize = sizeof(struct rt2860_rxwi);
	if (sc->mac_ver == 0x5592)
		rxwisize += sizeof(uint64_t);
	else if (sc->mac_ver == 0x3593)
		rxwisize += sizeof(uint32_t);

	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("RX status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->rxq.pipeh);
		if (status != USBD_CANCELLED)
			goto skip;
		return;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &xferlen, NULL);

	if (__predict_false(xferlen < sizeof (uint32_t) + rxwisize +
	    sizeof(struct rt2870_rxd))) {
		DPRINTF(("xfer too short %d\n", xferlen));
		goto skip;
	}

	/* HW can aggregate multiple 802.11 frames in a single USB xfer */
	buf = data->buf;
	while (xferlen > 8) {
		dmalen = letoh32(*(uint32_t *)buf) & 0xffff;

		if (__predict_false(dmalen == 0 || (dmalen & 3) != 0)) {
			DPRINTF(("bad DMA length %u\n", dmalen));
			break;
		}
		if (__predict_false(dmalen + 8 > xferlen)) {
			DPRINTF(("bad DMA length %u > %d\n",
			    dmalen + 8, xferlen));
			break;
		}
		run_rx_frame(sc, buf + sizeof (uint32_t), dmalen, &ml);
		buf += dmalen + 8;
		xferlen -= dmalen + 8;
	}
	if_input(&sc->sc_ic.ic_if, &ml);

skip:	/* setup a new transfer */
	usbd_setup_xfer(xfer, sc->rxq.pipeh, data, data->buf, RUN_MAX_RXSZ,
	    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT, run_rxeof);
	(void)usbd_transfer(data->xfer);
}

void
run_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct run_tx_data *data = priv;
	struct run_softc *sc = data->sc;
	struct run_tx_ring *txq = &sc->txq[data->qid];
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s;

	s = splnet();
	txq->queued--;
	sc->qfullmsk &= ~(1 << data->qid);

	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("TX status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(txq->pipeh);
		ifp->if_oerrors++;
		splx(s);
		return;
	}

	sc->sc_tx_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);
	run_start(ifp);
	splx(s);
}

int
run_tx(struct run_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct run_node *rn = (void *)ni;
	struct ieee80211_frame *wh;
	struct run_tx_ring *ring;
	struct run_tx_data *data;
	struct rt2870_txd *txd;
	struct rt2860_txwi *txwi;
	uint16_t qos, dur;
	uint16_t txwisize;
	uint8_t type, mcs, tid, qid;
	int error, hasqos, ridx, ctl_ridx, xferlen;

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	if ((hasqos = ieee80211_has_qos(wh))) {
		qos = ieee80211_get_qos(wh);
		tid = qos & IEEE80211_QOS_TID;
		qid = ieee80211_up_to_ac(ic, tid);
	} else {
		qos = 0;
		tid = 0;
		qid = EDCA_AC_BE;
	}
	ring = &sc->txq[qid];
	data = &ring->data[ring->cur];

	/* pickup a rate index */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA) {
		ridx = (ic->ic_curmode == IEEE80211_MODE_11A) ?
		    RT2860_RIDX_OFDM6 : RT2860_RIDX_CCK1;
		ctl_ridx = rt2860_rates[ridx].ctl_ridx;
	} else if (ic->ic_fixed_rate != -1) {
		ridx = sc->fixed_ridx;
		ctl_ridx = rt2860_rates[ridx].ctl_ridx;
	} else {
		ridx = rn->ridx[ni->ni_txrate];
		ctl_ridx = rn->ctl_ridx[ni->ni_txrate];
	}

	/* get MCS code from rate index */
	mcs = rt2860_rates[ridx].mcs;

	txwisize = sizeof(struct rt2860_txwi);
	if (sc->mac_ver == 0x5592)
		txwisize += sizeof(uint32_t);
	xferlen = txwisize + m->m_pkthdr.len;

	/* roundup to 32-bit alignment */
	xferlen = (xferlen + 3) & ~3;

	txd = (struct rt2870_txd *)data->buf;
	txd->flags = RT2860_TX_QSEL_EDCA;
	txd->len = htole16(xferlen);

	/* setup TX Wireless Information */
	txwi = (struct rt2860_txwi *)(txd + 1);
	txwi->flags = 0;
	txwi->xflags = hasqos ? 0 : RT2860_TX_NSEQ;
	txwi->wcid = (type == IEEE80211_FC0_TYPE_DATA) ?
	    RUN_AID2WCID(ni->ni_associd) : 0xff;
	txwi->len = htole16(m->m_pkthdr.len);
	if (rt2860_rates[ridx].phy == IEEE80211_T_DS) {
		txwi->phy = htole16(RT2860_PHY_CCK);
		if (ridx != RT2860_RIDX_CCK1 &&
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			mcs |= RT2860_PHY_SHPRE;
	} else
		txwi->phy = htole16(RT2860_PHY_OFDM);
	txwi->phy |= htole16(mcs);

	txwi->txop = RT2860_TX_TXOP_BACKOFF;

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (!hasqos || (qos & IEEE80211_QOS_ACK_POLICY_MASK) !=
	     IEEE80211_QOS_ACK_POLICY_NOACK)) {
		txwi->xflags |= RT2860_TX_ACK;
		if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
			dur = rt2860_rates[ctl_ridx].sp_ack_dur;
		else
			dur = rt2860_rates[ctl_ridx].lp_ack_dur;
		*(uint16_t *)wh->i_dur = htole16(dur);
	}

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct run_tx_radiotap_header *tap = &sc->sc_txtap;
		struct mbuf mb;

		tap->wt_flags = 0;
		tap->wt_rate = rt2860_rates[ridx].rate;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		if (mcs & RT2860_PHY_SHPRE)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_txtap_len;
		mb.m_next = m;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_OUT);
	}
#endif

	m_copydata(m, 0, m->m_pkthdr.len, (caddr_t)txwi + txwisize);
	m_freem(m);

	xferlen += sizeof (*txd) + 4;

	usbd_setup_xfer(data->xfer, ring->pipeh, data, data->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, RUN_TX_TIMEOUT, run_txeof);
	error = usbd_transfer(data->xfer);
	if (__predict_false(error != USBD_IN_PROGRESS && error != 0))
		return error;

	ieee80211_release_node(ic, ni);

	ring->cur = (ring->cur + 1) % RUN_TX_RING_COUNT;
	if (++ring->queued >= RUN_TX_RING_COUNT)
		sc->qfullmsk |= 1 << qid;

	return 0;
}

void
run_start(struct ifnet *ifp)
{
	struct run_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->qfullmsk != 0) {
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
		if (run_tx(sc, m, ni) != 0) {
			ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			continue;
		}

		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

void
run_watchdog(struct ifnet *ifp)
{
	struct run_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			/* run_init(ifp); XXX needs a process context! */
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
run_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct run_softc *sc = ifp->if_softc;
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
			if (!(ifp->if_flags & IFF_RUNNING))
				run_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				run_stop(ifp, 1);
		}
		break;

	case SIOCS80211CHANNEL:
		/*
		 * This allows for fast channel switching in monitor mode
		 * (used by kismet).
		 */
		error = ieee80211_ioctl(ifp, cmd, data);
		if (error == ENETRESET &&
		    ic->ic_opmode == IEEE80211_M_MONITOR) {
			if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
			    (IFF_UP | IFF_RUNNING))
				run_set_chan(sc, ic->ic_ibss_chan);
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			run_stop(ifp, 0);
			run_init(ifp);
		}
		error = 0;
	}

	splx(s);

	usbd_ref_decr(sc->sc_udev);

	return error;
}

void
run_iq_calib(struct run_softc *sc, u_int chan)
{
	uint16_t val;

	/* Tx0 IQ gain. */
	run_bbp_write(sc, 158, 0x2c);
	if (chan <= 14)
		run_efuse_read(sc, RT5390_EEPROM_IQ_GAIN_CAL_TX0_2GHZ, &val);
	else if (chan <= 64) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_GAIN_CAL_TX0_CH36_TO_CH64_5GHZ, &val);
	} else if (chan <= 138) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_GAIN_CAL_TX0_CH100_TO_CH138_5GHZ, &val);
	} else if (chan <= 165) {
		run_efuse_read(sc,
	    RT5390_EEPROM_IQ_GAIN_CAL_TX0_CH140_TO_CH165_5GHZ,
		    &val);
	} else
		val = 0;
	run_bbp_write(sc, 159, val);

	/* Tx0 IQ phase. */
	run_bbp_write(sc, 158, 0x2d);
	if (chan <= 14) {
		run_efuse_read(sc, RT5390_EEPROM_IQ_PHASE_CAL_TX0_2GHZ, &val);
	} else if (chan <= 64) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX0_CH36_TO_CH64_5GHZ, &val);
	} else if (chan <= 138) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX0_CH100_TO_CH138_5GHZ, &val);
	} else if (chan <= 165) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX0_CH140_TO_CH165_5GHZ, &val);
	} else
		val = 0;
	run_bbp_write(sc, 159, val);

	/* Tx1 IQ gain. */
	run_bbp_write(sc, 158, 0x4a);
	if (chan <= 14) {
		run_efuse_read(sc, RT5390_EEPROM_IQ_GAIN_CAL_TX1_2GHZ, &val);
	} else if (chan <= 64) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_GAIN_CAL_TX1_CH36_TO_CH64_5GHZ, &val);
	} else if (chan <= 138) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_GAIN_CAL_TX1_CH100_TO_CH138_5GHZ, &val);
	} else if (chan <= 165) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_GAIN_CAL_TX1_CH140_TO_CH165_5GHZ, &val);
	} else
		val = 0;
	run_bbp_write(sc, 159, val);

	/* Tx1 IQ phase. */
	run_bbp_write(sc, 158, 0x4b);
	if (chan <= 14) {
		run_efuse_read(sc, RT5390_EEPROM_IQ_PHASE_CAL_TX1_2GHZ, &val);
	} else if (chan <= 64) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX1_CH36_TO_CH64_5GHZ, &val);
	} else if (chan <= 138) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX1_CH100_TO_CH138_5GHZ, &val);
	} else if (chan <= 165) {
		run_efuse_read(sc,
		    RT5390_EEPROM_IQ_PHASE_CAL_TX1_CH140_TO_CH165_5GHZ, &val);
	} else
		val = 0;
	run_bbp_write(sc, 159, val);

	/* RF IQ compensation control. */
	run_bbp_write(sc, 158, 0x04);
	run_efuse_read(sc, RT5390_EEPROM_RF_IQ_COMPENSATION_CTL, &val);
	run_bbp_write(sc, 159, val);

	/* RF IQ imbalance compensation control. */
	run_bbp_write(sc, 158, 0x03);
	run_efuse_read(sc,
	    RT5390_EEPROM_RF_IQ_IMBALANCE_COMPENSATION_CTL, &val);
	run_bbp_write(sc, 159, val);
}

void
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

void
run_rt2870_set_chan(struct run_softc *sc, u_int chan)
{
	const struct rfprog *rfprog = rt2860_rf2850;
	uint32_t r2, r3, r4;
	int8_t txpow1, txpow2;
	int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rfprog[i].chan != chan; i++)
		;

	r2 = rfprog[i].r2;
	if (sc->ntxchains == 1)
		r2 |= 1 << 12;		/* 1T: disable Tx chain 2 */
	if (sc->nrxchains == 1)
		r2 |= 1 << 15 | 1 << 4;	/* 1R: disable Rx chains 2 & 3 */
	else if (sc->nrxchains == 2)
		r2 |= 1 << 4;		/* 2R: disable Rx chain 3 */

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];
	txpow2 = sc->txpow2[i];
	if (chan > 14) {
		if (txpow1 >= 0)
			txpow1 = txpow1 << 1 | 1;
		else
			txpow1 = (7 + txpow1) << 1;
		if (txpow2 >= 0)
			txpow2 = txpow2 << 1 | 1;
		else
			txpow2 = (7 + txpow2) << 1;
	}
	r3 = rfprog[i].r3 | txpow1 << 7;
	r4 = rfprog[i].r4 | sc->freq << 13 | txpow2 << 4;

	run_rt2870_rf_write(sc, RT2860_RF1, rfprog[i].r1);
	run_rt2870_rf_write(sc, RT2860_RF2, r2);
	run_rt2870_rf_write(sc, RT2860_RF3, r3);
	run_rt2870_rf_write(sc, RT2860_RF4, r4);

	DELAY(200);

	run_rt2870_rf_write(sc, RT2860_RF1, rfprog[i].r1);
	run_rt2870_rf_write(sc, RT2860_RF2, r2);
	run_rt2870_rf_write(sc, RT2860_RF3, r3 | 1);
	run_rt2870_rf_write(sc, RT2860_RF4, r4);

	DELAY(200);

	run_rt2870_rf_write(sc, RT2860_RF1, rfprog[i].r1);
	run_rt2870_rf_write(sc, RT2860_RF2, r2);
	run_rt2870_rf_write(sc, RT2860_RF3, r3);
	run_rt2870_rf_write(sc, RT2860_RF4, r4);
}

void
run_rt3070_set_chan(struct run_softc *sc, u_int chan)
{
	int8_t txpow1, txpow2;
	uint8_t rf;
	int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++)
		;

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

void
run_rt3572_set_chan(struct run_softc *sc, u_int chan)
{
	int8_t txpow1, txpow2;
	uint32_t tmp;
	uint8_t rf;
	int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++)
		;

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

	DELAY(2000);
}

void
run_rt3593_set_chan(struct run_softc *sc, u_int chan)
{
	int8_t txpow1, txpow2, txpow3;
	uint8_t h20mhz, rf;
	int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++)
		;

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
	rf &= ~RT3593_VCOCAL;
	rf |= (chan <= 14) ? RT3593_VCOCAL : 0xbe;
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

void
run_rt5390_set_chan(struct run_softc *sc, u_int chan)
{
	int8_t txpow1, txpow2;
	uint8_t rf;
	int i;

	/* find the settings for this channel (we know it exists) */
	for (i = 0; rt2860_rf2850[i].chan != chan; i++)
		;

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
		DELAY(10);
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
	rf |= RT3593_VCOCAL;
	run_rt3070_rf_write(sc, 3, rf);
}

void
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
	for (i = 0; rt2860_rf2850[i].chan != chan; i++, freqs++)
		;

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
	rf |= RT3593_VCOCAL;
	run_rt3070_rf_write(sc, 3, rf);
}

void
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

void
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

int
run_set_chan(struct run_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	u_int chan, group;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return EINVAL;

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

	DELAY(1000);

	/* Perform IQ calibration. */
	if (sc->mac_ver >= 0x5392)
		run_iq_calib(sc, chan);

	return 0;
}

void
run_enable_tsf_sync(struct run_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	run_read(sc, RT2860_BCN_TIME_CFG, &tmp);
	tmp &= ~0x1fffff;
	tmp |= ic->ic_bss->ni_intval * 16;
	tmp |= RT2860_TSF_TIMER_EN | RT2860_TBTT_TIMER_EN;
	/* local TSF is always updated with remote TSF on beacon reception */
	tmp |= 1 << RT2860_TSF_SYNC_MODE_SHIFT;
	run_write(sc, RT2860_BCN_TIME_CFG, tmp);
}

void
run_enable_mrr(struct run_softc *sc)
{
#define CCK(mcs)	(mcs)
#define OFDM(mcs)	(1 << 3 | (mcs))
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

void
run_set_txpreamble(struct run_softc *sc)
{
	uint32_t tmp;

	run_read(sc, RT2860_AUTO_RSP_CFG, &tmp);
	if (sc->sc_ic.ic_flags & IEEE80211_F_SHPREAMBLE)
		tmp |= RT2860_CCK_SHORT_EN;
	else
		tmp &= ~RT2860_CCK_SHORT_EN;
	run_write(sc, RT2860_AUTO_RSP_CFG, tmp);
}

void
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

void
run_set_leds(struct run_softc *sc, uint16_t which)
{
	(void)run_mcu_cmd(sc, RT2860_MCU_CMD_LEDS,
	    which | (sc->leds & 0x7f));
}

void
run_set_bssid(struct run_softc *sc, const uint8_t *bssid)
{
	run_write(sc, RT2860_MAC_BSSID_DW0,
	    bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24);
	run_write(sc, RT2860_MAC_BSSID_DW1,
	    bssid[4] | bssid[5] << 8);
}

void
run_set_macaddr(struct run_softc *sc, const uint8_t *addr)
{
	run_write(sc, RT2860_MAC_ADDR_DW0,
	    addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	run_write(sc, RT2860_MAC_ADDR_DW1,
	    addr[4] | addr[5] << 8 | 0xff << 16);
}

void
run_updateslot(struct ieee80211com *ic)
{
	/* do it in a process context */
	run_do_async(ic->ic_softc, run_updateslot_cb, NULL, 0);
}

void
run_updateslot_cb(struct run_softc *sc, void *arg)
{
	uint32_t tmp;

	run_read(sc, RT2860_BKOFF_SLOT_CFG, &tmp);
	tmp &= ~0xff;
	tmp |= (sc->sc_ic.ic_flags & IEEE80211_F_SHSLOT) ?
	    IEEE80211_DUR_DS_SHSLOT : IEEE80211_DUR_DS_SLOT;
	run_write(sc, RT2860_BKOFF_SLOT_CFG, tmp);
}

#if NBPFILTER > 0
int8_t
run_rssi2dbm(struct run_softc *sc, uint8_t rssi, uint8_t rxchain)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c = ic->ic_ibss_chan;
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

	return -12 - delta - rssi;
}
#endif

void
run_rt5390_bbp_init(struct run_softc *sc)
{
	int i;
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

int
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
		return ETIMEDOUT;

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
	return 0;
}

int
run_rt3070_rf_init(struct run_softc *sc)
{
	uint32_t tmp;
	uint8_t bbp4, mingain, rf, target;
	int i;

	run_rt3070_rf_read(sc, 30, &rf);
	/* toggle RF R30 bit 7 */
	run_rt3070_rf_write(sc, 30, rf | 0x80);
	DELAY(1000);
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
			tmp |= 0x0d000000;	/* 1.35V */
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
			DELAY(1);	/* wait for 1msec */
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
	return 0;
}

void
run_rt3593_rf_init(struct run_softc *sc)
{
	uint32_t tmp;
	uint8_t rf;
	int i;

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
	run_rt3070_rf_write(sc, 2, RT3593_RESCAL);

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
	DELAY(1);
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

void
run_rt5390_rf_init(struct run_softc *sc)
{
	uint32_t tmp;
	uint8_t rf;
	int i;

	/* Toggle RF R2 to initiate calibration. */
	if (sc->mac_ver == 0x5390) {
		run_rt3070_rf_read(sc, 2, &rf);
		run_rt3070_rf_write(sc, 2, rf | RT3593_RESCAL);
		DELAY(10);
		run_rt3070_rf_write(sc, 2, rf & ~RT3593_RESCAL);
	} else {
		run_rt3070_rf_write(sc, 2, RT3593_RESCAL);
		DELAY(10);
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

int
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
		DELAY(1000);
		/* read received power */
		run_bbp_read(sc, 55, &bbp55_pb);
		if (bbp55_pb != 0)
			break;
	}
	if (ntries == 100)
		return ETIMEDOUT;

	/* set power and frequency of stopband test tone */
	run_bbp_write(sc, 24, 0x06);
	for (ntries = 0; ntries < 100; ntries++) {
		/* transmit test tone */
		run_bbp_write(sc, 25, 0x90);
		DELAY(1000);
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

	return 0;
}

void
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
		/* enable DC filter */
		if (sc->mac_rev >= 0x0211) {
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

void
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

void
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

int
run_txrx_enable(struct run_softc *sc)
{
	uint32_t tmp;
	int error, ntries;

	run_write(sc, RT2860_MAC_SYS_CTRL, RT2860_MAC_TX_EN);
	for (ntries = 0; ntries < 200; ntries++) {
		if ((error = run_read(sc, RT2860_WPDMA_GLO_CFG, &tmp)) != 0)
			return error;
		if ((tmp & (RT2860_TX_DMA_BUSY | RT2860_RX_DMA_BUSY)) == 0)
			break;
		DELAY(1000);
	}
	if (ntries == 200)
		return ETIMEDOUT;

	DELAY(50);

	tmp |= RT2860_RX_DMA_EN | RT2860_TX_DMA_EN | RT2860_TX_WB_DDONE;
	run_write(sc, RT2860_WPDMA_GLO_CFG, tmp);

	/* enable Rx bulk aggregation (set timeout and limit) */
	tmp = RT2860_USB_TX_EN | RT2860_USB_RX_EN | RT2860_USB_RX_AGG_EN |
	    RT2860_USB_RX_AGG_TO(128) | RT2860_USB_RX_AGG_LMT(2);
	run_write(sc, RT2860_USB_DMA_CFG, tmp);

	/* set Rx filter */
	tmp = RT2860_DROP_CRC_ERR | RT2860_DROP_PHY_ERR;
	if (sc->sc_ic.ic_opmode != IEEE80211_M_MONITOR) {
		tmp |= RT2860_DROP_UC_NOME | RT2860_DROP_DUPL |
		    RT2860_DROP_CTS | RT2860_DROP_BA | RT2860_DROP_ACK |
		    RT2860_DROP_VER_ERR | RT2860_DROP_CTRL_RSV |
		    RT2860_DROP_CFACK | RT2860_DROP_CFEND;
		if (sc->sc_ic.ic_opmode == IEEE80211_M_STA)
			tmp |= RT2860_DROP_RTS | RT2860_DROP_PSPOLL;
	}
	run_write(sc, RT2860_RX_FILTR_CFG, tmp);

	run_write(sc, RT2860_MAC_SYS_CTRL,
	    RT2860_MAC_RX_EN | RT2860_MAC_TX_EN);

	return 0;
}

void
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

int
run_init(struct ifnet *ifp)
{
	struct run_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	uint8_t bbp1, bbp3;
	int i, error, qid, ridx, ntries;

	if (usbd_is_dying(sc->sc_udev))
		return ENXIO;

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = run_read(sc, RT2860_ASIC_VER_ID, &tmp)) != 0)
			goto fail;
		if (tmp != 0 && tmp != 0xffffffff)
			break;
		DELAY(10);
	}
	if (ntries == 100) {
		error = ETIMEDOUT;
		goto fail;
	}

	if ((error = run_load_microcode(sc)) != 0) {
		printf("%s: could not load 8051 microcode\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/* init host command ring */
	sc->cmdq.cur = sc->cmdq.next = sc->cmdq.queued = 0;

	/* init Tx rings (4 EDCAs) */
	for (qid = 0; qid < 4; qid++) {
		if ((error = run_alloc_tx_ring(sc, qid)) != 0)
			goto fail;
	}
	/* init Rx ring */
	if ((error = run_alloc_rx_ring(sc)) != 0)
		goto fail;

	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	run_set_macaddr(sc, ic->ic_myaddr);

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = run_read(sc, RT2860_WPDMA_GLO_CFG, &tmp)) != 0)
			goto fail;
		if ((tmp & (RT2860_TX_DMA_BUSY | RT2860_RX_DMA_BUSY)) == 0)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for DMA engine\n",
		    sc->sc_dev.dv_xname);
		error = ETIMEDOUT;
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

	if ((error = run_reset(sc)) != 0) {
		printf("%s: could not reset chipset\n", sc->sc_dev.dv_xname);
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
		if ((error = run_read(sc, RT2860_MAC_STATUS_REG, &tmp)) != 0)
			goto fail;
		if (!(tmp & (RT2860_RX_STATUS_BUSY | RT2860_TX_STATUS_BUSY)))
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		error = ETIMEDOUT;
		goto fail;
	}

	/* clear Host to MCU mailbox */
	run_write(sc, RT2860_H2M_BBPAGENT, 0);
	run_write(sc, RT2860_H2M_MAILBOX, 0);
	DELAY(1000);

	if ((error = run_bbp_init(sc)) != 0) {
		printf("%s: could not initialize BBP\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	/* abort TSF synchronization */
	run_read(sc, RT2860_BCN_TIME_CFG, &tmp);
	tmp &= ~(RT2860_BCN_TX_EN | RT2860_TSF_TIMER_EN |
	    RT2860_TBTT_TIMER_EN);
	run_write(sc, RT2860_BCN_TIME_CFG, tmp);

	/* clear RX WCID search table */
	run_set_region_4(sc, RT2860_WCID_ENTRY(0), 0, 512);
	/* clear WCID attribute table */
	run_set_region_4(sc, RT2860_WCID_ATTR(0), 0, 8 * 32);
	/* clear shared key table */
	run_set_region_4(sc, RT2860_SKEY(0, 0), 0, 8 * 32);
	/* clear shared key mode */
	run_set_region_4(sc, RT2860_SKEY_MODE_0_7, 0, 4);

	run_read(sc, RT2860_US_CYC_CNT, &tmp);
	tmp = (tmp & ~0xff) | 0x1e;
	run_write(sc, RT2860_US_CYC_CNT, tmp);

	if (sc->mac_rev != 0x0101)
		run_write(sc, RT2860_TXOP_CTRL_CFG, 0x0000583f);

	run_write(sc, RT2860_WMM_TXOP0_CFG, 0);
	run_write(sc, RT2860_WMM_TXOP1_CFG, 48 << 16 | 96);

	/* write vendor-specific BBP values (from EEPROM) */
	if (sc->mac_ver < 0x3593) {
		for (i = 0; i < 8; i++) {
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
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	run_set_chan(sc, ic->ic_ibss_chan);

	/* turn radio LED on */
	run_set_leds(sc, RT2860_LED_RADIO);

	for (i = 0; i < RUN_RX_RING_COUNT; i++) {
		struct run_rx_data *data = &sc->rxq.data[i];

		usbd_setup_xfer(data->xfer, sc->rxq.pipeh, data, data->buf,
		    RUN_MAX_RXSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, run_rxeof);
		error = usbd_transfer(data->xfer);
		if (error != 0 && error != USBD_IN_PROGRESS)
			goto fail;
	}

	if ((error = run_txrx_enable(sc)) != 0)
		goto fail;

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (ic->ic_flags & IEEE80211_F_WEPON) {
		/* install WEP keys */
		for (i = 0; i < IEEE80211_WEP_NKID; i++) {
			if (ic->ic_nw_keys[i].k_cipher != IEEE80211_CIPHER_NONE)
				(void)run_set_key(ic, NULL, &ic->ic_nw_keys[i]);
		}
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	if (error != 0)
fail:		run_stop(ifp, 1);
	return error;
}

void
run_stop(struct ifnet *ifp, int disable)
{
	struct run_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;
	int s, ntries, qid;

	if (ifp->if_flags & IFF_RUNNING)
		run_set_leds(sc, 0);	/* turn all LEDs off */

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_del(&sc->scan_to);
	timeout_del(&sc->calib_to);

	s = splusb();
	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	/* wait for all queued asynchronous commands to complete */
	usb_wait_task(sc->sc_udev, &sc->sc_task);
	splx(s);

	/* Disable Tx/Rx DMA. */
	run_read(sc, RT2860_WPDMA_GLO_CFG, &tmp);
	tmp &= ~(RT2860_RX_DMA_EN | RT2860_TX_DMA_EN);
	run_write(sc, RT2860_WPDMA_GLO_CFG, tmp);

	for (ntries = 0; ntries < 100; ntries++) {
		if (run_read(sc, RT2860_WPDMA_GLO_CFG, &tmp) != 0)
			break;
		if ((tmp & (RT2860_TX_DMA_BUSY | RT2860_RX_DMA_BUSY)) == 0)
			break;
		DELAY(10);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for DMA engine\n",
		    sc->sc_dev.dv_xname);
	}

	/* disable Tx/Rx */
	run_read(sc, RT2860_MAC_SYS_CTRL, &tmp);
	tmp &= ~(RT2860_MAC_RX_EN | RT2860_MAC_TX_EN);
	run_write(sc, RT2860_MAC_SYS_CTRL, tmp);

	/* wait for pending Tx to complete */
	for (ntries = 0; ntries < 100; ntries++) {
		if (run_read(sc, RT2860_TXRXQ_PCNT, &tmp) != 0)
			break;
		if ((tmp & RT2860_TX2Q_PCNT_MASK) == 0)
			break;
	}
	DELAY(1000);
	run_write(sc, RT2860_USB_DMA_CFG, 0);

	/* reset adapter */
	run_write(sc, RT2860_MAC_SYS_CTRL, RT2860_BBP_HRST | RT2860_MAC_SRST);
	run_write(sc, RT2860_MAC_SYS_CTRL, 0);

	/* reset Tx and Rx rings */
	sc->qfullmsk = 0;
	for (qid = 0; qid < 4; qid++)
		run_free_tx_ring(sc, qid);
	run_free_rx_ring(sc);
}

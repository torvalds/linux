/*-
 * Copyright (c) 2008 Weongyo Jeong <weongyo@FreeBSD.org>
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

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"

#include <dev/usb/wlan/if_urtwreg.h>
#include <dev/usb/wlan/if_urtwvar.h>

/* copy some rate indices from if_rtwn_ridx.h */
#define	URTW_RIDX_CCK5		2
#define	URTW_RIDX_CCK11		3
#define	URTW_RIDX_OFDM6		4
#define	URTW_RIDX_OFDM24	8

static SYSCTL_NODE(_hw_usb, OID_AUTO, urtw, CTLFLAG_RW, 0, "USB Realtek 8187L");
#ifdef URTW_DEBUG
int urtw_debug = 0;
SYSCTL_INT(_hw_usb_urtw, OID_AUTO, debug, CTLFLAG_RWTUN, &urtw_debug, 0,
    "control debugging printfs");
enum {
	URTW_DEBUG_XMIT		= 0x00000001,	/* basic xmit operation */
	URTW_DEBUG_RECV		= 0x00000002,	/* basic recv operation */
	URTW_DEBUG_RESET	= 0x00000004,	/* reset processing */
	URTW_DEBUG_TX_PROC	= 0x00000008,	/* tx ISR proc */
	URTW_DEBUG_RX_PROC	= 0x00000010,	/* rx ISR proc */
	URTW_DEBUG_STATE	= 0x00000020,	/* 802.11 state transitions */
	URTW_DEBUG_STAT		= 0x00000040,	/* statistic */
	URTW_DEBUG_INIT		= 0x00000080,	/* initialization of dev */
	URTW_DEBUG_TXSTATUS	= 0x00000100,	/* tx status */
	URTW_DEBUG_ANY		= 0xffffffff
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
static int urtw_preamble_mode = URTW_PREAMBLE_MODE_LONG;
SYSCTL_INT(_hw_usb_urtw, OID_AUTO, preamble_mode, CTLFLAG_RWTUN,
    &urtw_preamble_mode, 0, "set the preable mode (long or short)");

/* recognized device vendors/products */
#define urtw_lookup(v, p)						\
	((const struct urtw_type *)usb_lookup(urtw_devs, v, p))
#define	URTW_DEV_B(v,p)							\
	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, URTW_REV_RTL8187B) }
#define	URTW_DEV_L(v,p)							\
	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, URTW_REV_RTL8187L) }
#define	URTW_REV_RTL8187B	0
#define	URTW_REV_RTL8187L	1
static const STRUCT_USB_HOST_ID urtw_devs[] = {
	URTW_DEV_B(NETGEAR, WG111V3),
	URTW_DEV_B(REALTEK, RTL8187B_0),
	URTW_DEV_B(REALTEK, RTL8187B_1),
	URTW_DEV_B(REALTEK, RTL8187B_2),
	URTW_DEV_B(SITECOMEU, WL168V4),
	URTW_DEV_L(ASUS, P5B_WIFI),
	URTW_DEV_L(BELKIN, F5D7050E),
	URTW_DEV_L(LINKSYS4, WUSB54GCV2),
	URTW_DEV_L(NETGEAR, WG111V2),
	URTW_DEV_L(REALTEK, RTL8187),
	URTW_DEV_L(SITECOMEU, WL168V1),
	URTW_DEV_L(SURECOM, EP9001G2A),
	{ USB_VPI(USB_VENDOR_OVISLINK, 0x8187, URTW_REV_RTL8187L) },
	{ USB_VPI(USB_VENDOR_DICKSMITH, 0x9401, URTW_REV_RTL8187L) },
	{ USB_VPI(USB_VENDOR_HP, 0xca02, URTW_REV_RTL8187L) },
	{ USB_VPI(USB_VENDOR_LOGITEC, 0x010c, URTW_REV_RTL8187L) },
	{ USB_VPI(USB_VENDOR_NETGEAR, 0x6100, URTW_REV_RTL8187L) },
	{ USB_VPI(USB_VENDOR_SPHAIRON, 0x0150, URTW_REV_RTL8187L) },
	{ USB_VPI(USB_VENDOR_QCOM, 0x6232, URTW_REV_RTL8187L) },
#undef URTW_DEV_L
#undef URTW_DEV_B
};

#define urtw_read8_m(sc, val, data)	do {			\
	error = urtw_read8_c(sc, val, data);			\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_write8_m(sc, val, data)	do {			\
	error = urtw_write8_c(sc, val, data);			\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_read16_m(sc, val, data)	do {			\
	error = urtw_read16_c(sc, val, data);			\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_write16_m(sc, val, data)	do {			\
	error = urtw_write16_c(sc, val, data);			\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_read32_m(sc, val, data)	do {			\
	error = urtw_read32_c(sc, val, data);			\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_write32_m(sc, val, data)	do {			\
	error = urtw_write32_c(sc, val, data);			\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_8187_write_phy_ofdm(sc, val, data)	do {		\
	error = urtw_8187_write_phy_ofdm_c(sc, val, data);	\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_8187_write_phy_cck(sc, val, data)	do {		\
	error = urtw_8187_write_phy_cck_c(sc, val, data);	\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_8225_write(sc, val, data)	do {			\
	error = urtw_8225_write_c(sc, val, data);		\
	if (error != 0)						\
		goto fail;					\
} while (0)

struct urtw_pair {
	uint32_t	reg;
	uint32_t	val;
};

static uint8_t urtw_8225_agc[] = {
	0x9e, 0x9e, 0x9e, 0x9e, 0x9e, 0x9e, 0x9e, 0x9e, 0x9d, 0x9c, 0x9b,
	0x9a, 0x99, 0x98, 0x97, 0x96, 0x95, 0x94, 0x93, 0x92, 0x91, 0x90,
	0x8f, 0x8e, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x88, 0x87, 0x86, 0x85,
	0x84, 0x83, 0x82, 0x81, 0x80, 0x3f, 0x3e, 0x3d, 0x3c, 0x3b, 0x3a,
	0x39, 0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x30, 0x2f,
	0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x29, 0x28, 0x27, 0x26, 0x25, 0x24,
	0x23, 0x22, 0x21, 0x20, 0x1f, 0x1e, 0x1d, 0x1c, 0x1b, 0x1a, 0x19,
	0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0f, 0x0e,
	0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03,
	0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
};

static uint8_t urtw_8225z2_agc[] = {
	0x5e, 0x5e, 0x5e, 0x5e, 0x5d, 0x5b, 0x59, 0x57, 0x55, 0x53, 0x51,
	0x4f, 0x4d, 0x4b, 0x49, 0x47, 0x45, 0x43, 0x41, 0x3f, 0x3d, 0x3b,
	0x39, 0x37, 0x35, 0x33, 0x31, 0x2f, 0x2d, 0x2b, 0x29, 0x27, 0x25,
	0x23, 0x21, 0x1f, 0x1d, 0x1b, 0x19, 0x17, 0x15, 0x13, 0x11, 0x0f,
	0x0d, 0x0b, 0x09, 0x07, 0x05, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x19, 0x19,
	0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x20, 0x21, 0x22, 0x23,
	0x24, 0x25, 0x26, 0x26, 0x27, 0x27, 0x28, 0x28, 0x29, 0x2a, 0x2a,
	0x2a, 0x2b, 0x2b, 0x2b, 0x2c, 0x2c, 0x2c, 0x2d, 0x2d, 0x2d, 0x2d,
	0x2e, 0x2e, 0x2e, 0x2e, 0x2f, 0x2f, 0x2f, 0x30, 0x30, 0x31, 0x31,
	0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
	0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31
};

static uint32_t urtw_8225_channel[] = {
	0x0000,		/* dummy channel 0  */
	0x085c,		/* 1  */
	0x08dc,		/* 2  */
	0x095c,		/* 3  */
	0x09dc,		/* 4  */
	0x0a5c,		/* 5  */
	0x0adc,		/* 6  */
	0x0b5c,		/* 7  */
	0x0bdc,		/* 8  */
	0x0c5c,		/* 9  */
	0x0cdc,		/* 10  */
	0x0d5c,		/* 11  */
	0x0ddc,		/* 12  */
	0x0e5c,		/* 13  */
	0x0f72,		/* 14  */
};

static uint8_t urtw_8225_gain[] = {
	0x23, 0x88, 0x7c, 0xa5,		/* -82dbm  */
	0x23, 0x88, 0x7c, 0xb5,		/* -82dbm  */
	0x23, 0x88, 0x7c, 0xc5,		/* -82dbm  */
	0x33, 0x80, 0x79, 0xc5,		/* -78dbm  */
	0x43, 0x78, 0x76, 0xc5,		/* -74dbm  */
	0x53, 0x60, 0x73, 0xc5,		/* -70dbm  */
	0x63, 0x58, 0x70, 0xc5,		/* -66dbm  */
};

static struct urtw_pair urtw_8225_rf_part1[] = {
	{ 0x00, 0x0067 }, { 0x01, 0x0fe0 }, { 0x02, 0x044d }, { 0x03, 0x0441 },
	{ 0x04, 0x0486 }, { 0x05, 0x0bc0 }, { 0x06, 0x0ae6 }, { 0x07, 0x082a },
	{ 0x08, 0x001f }, { 0x09, 0x0334 }, { 0x0a, 0x0fd4 }, { 0x0b, 0x0391 },
	{ 0x0c, 0x0050 }, { 0x0d, 0x06db }, { 0x0e, 0x0029 }, { 0x0f, 0x0914 },
};

static struct urtw_pair urtw_8225_rf_part2[] = {
	{ 0x00, 0x01 }, { 0x01, 0x02 }, { 0x02, 0x42 }, { 0x03, 0x00 },
	{ 0x04, 0x00 }, { 0x05, 0x00 }, { 0x06, 0x40 }, { 0x07, 0x00 },
	{ 0x08, 0x40 }, { 0x09, 0xfe }, { 0x0a, 0x09 }, { 0x0b, 0x80 },
	{ 0x0c, 0x01 }, { 0x0e, 0xd3 }, { 0x0f, 0x38 }, { 0x10, 0x84 },
	{ 0x11, 0x06 }, { 0x12, 0x20 }, { 0x13, 0x20 }, { 0x14, 0x00 },
	{ 0x15, 0x40 }, { 0x16, 0x00 }, { 0x17, 0x40 }, { 0x18, 0xef },
	{ 0x19, 0x19 }, { 0x1a, 0x20 }, { 0x1b, 0x76 }, { 0x1c, 0x04 },
	{ 0x1e, 0x95 }, { 0x1f, 0x75 }, { 0x20, 0x1f }, { 0x21, 0x27 },
	{ 0x22, 0x16 }, { 0x24, 0x46 }, { 0x25, 0x20 }, { 0x26, 0x90 },
	{ 0x27, 0x88 }
};

static struct urtw_pair urtw_8225_rf_part3[] = {
	{ 0x00, 0x98 }, { 0x03, 0x20 }, { 0x04, 0x7e }, { 0x05, 0x12 },
	{ 0x06, 0xfc }, { 0x07, 0x78 }, { 0x08, 0x2e }, { 0x10, 0x9b },
	{ 0x11, 0x88 }, { 0x12, 0x47 }, { 0x13, 0xd0 }, { 0x19, 0x00 },
	{ 0x1a, 0xa0 }, { 0x1b, 0x08 }, { 0x40, 0x86 }, { 0x41, 0x8d },
	{ 0x42, 0x15 }, { 0x43, 0x18 }, { 0x44, 0x1f }, { 0x45, 0x1e },
	{ 0x46, 0x1a }, { 0x47, 0x15 }, { 0x48, 0x10 }, { 0x49, 0x0a },
	{ 0x4a, 0x05 }, { 0x4b, 0x02 }, { 0x4c, 0x05 }
};

static uint16_t urtw_8225_rxgain[] = {
	0x0400, 0x0401, 0x0402, 0x0403, 0x0404, 0x0405, 0x0408, 0x0409,
	0x040a, 0x040b, 0x0502, 0x0503, 0x0504, 0x0505, 0x0540, 0x0541,
	0x0542, 0x0543, 0x0544, 0x0545, 0x0580, 0x0581, 0x0582, 0x0583,
	0x0584, 0x0585, 0x0588, 0x0589, 0x058a, 0x058b, 0x0643, 0x0644,
	0x0645, 0x0680, 0x0681, 0x0682, 0x0683, 0x0684, 0x0685, 0x0688,
	0x0689, 0x068a, 0x068b, 0x068c, 0x0742, 0x0743, 0x0744, 0x0745,
	0x0780, 0x0781, 0x0782, 0x0783, 0x0784, 0x0785, 0x0788, 0x0789,
	0x078a, 0x078b, 0x078c, 0x078d, 0x0790, 0x0791, 0x0792, 0x0793,
	0x0794, 0x0795, 0x0798, 0x0799, 0x079a, 0x079b, 0x079c, 0x079d,
	0x07a0, 0x07a1, 0x07a2, 0x07a3, 0x07a4, 0x07a5, 0x07a8, 0x07a9,
	0x07aa, 0x07ab, 0x07ac, 0x07ad, 0x07b0, 0x07b1, 0x07b2, 0x07b3,
	0x07b4, 0x07b5, 0x07b8, 0x07b9, 0x07ba, 0x07bb, 0x07bb
};

static uint8_t urtw_8225_threshold[] = {
	0x8d, 0x8d, 0x8d, 0x8d, 0x9d, 0xad, 0xbd,
};

static uint8_t urtw_8225_tx_gain_cck_ofdm[] = {
	0x02, 0x06, 0x0e, 0x1e, 0x3e, 0x7e
};

static uint8_t urtw_8225_txpwr_cck[] = {
	0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02,
	0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02,
	0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02,
	0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02,
	0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03,
	0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03
};

static uint8_t urtw_8225_txpwr_cck_ch14[] = {
	0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00,
	0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00,
	0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00,
	0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00,
	0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00,
	0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00
};

static uint8_t urtw_8225_txpwr_ofdm[]={
	0x80, 0x90, 0xa2, 0xb5, 0xcb, 0xe4
};

static uint8_t urtw_8225v2_gain_bg[]={
	0x23, 0x15, 0xa5,		/* -82-1dbm  */
	0x23, 0x15, 0xb5,		/* -82-2dbm  */
	0x23, 0x15, 0xc5,		/* -82-3dbm  */
	0x33, 0x15, 0xc5,		/* -78dbm  */
	0x43, 0x15, 0xc5,		/* -74dbm  */
	0x53, 0x15, 0xc5,		/* -70dbm  */
	0x63, 0x15, 0xc5,		/* -66dbm  */
};

static struct urtw_pair urtw_8225v2_rf_part1[] = {
	{ 0x00, 0x02bf }, { 0x01, 0x0ee0 }, { 0x02, 0x044d }, { 0x03, 0x0441 },
	{ 0x04, 0x08c3 }, { 0x05, 0x0c72 }, { 0x06, 0x00e6 }, { 0x07, 0x082a },
	{ 0x08, 0x003f }, { 0x09, 0x0335 }, { 0x0a, 0x09d4 }, { 0x0b, 0x07bb },
	{ 0x0c, 0x0850 }, { 0x0d, 0x0cdf }, { 0x0e, 0x002b }, { 0x0f, 0x0114 }
};

static struct urtw_pair urtw_8225v2b_rf_part0[] = {
	{ 0x00, 0x00b7 }, { 0x01, 0x0ee0 }, { 0x02, 0x044d }, { 0x03, 0x0441 },
	{ 0x04, 0x08c3 }, { 0x05, 0x0c72 }, { 0x06, 0x00e6 }, { 0x07, 0x082a },
	{ 0x08, 0x003f }, { 0x09, 0x0335 }, { 0x0a, 0x09d4 }, { 0x0b, 0x07bb },
	{ 0x0c, 0x0850 }, { 0x0d, 0x0cdf }, { 0x0e, 0x002b }, { 0x0f, 0x0114 }
};

static struct urtw_pair urtw_8225v2b_rf_part1[] = {
	{0x0f0, 0x32}, {0x0f1, 0x32}, {0x0f2, 0x00},
	{0x0f3, 0x00}, {0x0f4, 0x32}, {0x0f5, 0x43},
	{0x0f6, 0x00}, {0x0f7, 0x00}, {0x0f8, 0x46},
	{0x0f9, 0xa4}, {0x0fa, 0x00}, {0x0fb, 0x00},
	{0x0fc, 0x96}, {0x0fd, 0xa4}, {0x0fe, 0x00},
	{0x0ff, 0x00}, {0x158, 0x4b}, {0x159, 0x00},
	{0x15a, 0x4b}, {0x15b, 0x00}, {0x160, 0x4b},
	{0x161, 0x09}, {0x162, 0x4b}, {0x163, 0x09},
	{0x1ce, 0x0f}, {0x1cf, 0x00}, {0x1e0, 0xff},
	{0x1e1, 0x0f}, {0x1e2, 0x00}, {0x1f0, 0x4e},
	{0x1f1, 0x01}, {0x1f2, 0x02}, {0x1f3, 0x03},
	{0x1f4, 0x04}, {0x1f5, 0x05}, {0x1f6, 0x06},
	{0x1f7, 0x07}, {0x1f8, 0x08}, {0x24e, 0x00},
	{0x20c, 0x04}, {0x221, 0x61}, {0x222, 0x68},
	{0x223, 0x6f}, {0x224, 0x76}, {0x225, 0x7d},
	{0x226, 0x84}, {0x227, 0x8d}, {0x24d, 0x08},
	{0x250, 0x05}, {0x251, 0xf5}, {0x252, 0x04},
	{0x253, 0xa0}, {0x254, 0x1f}, {0x255, 0x23},
	{0x256, 0x45}, {0x257, 0x67}, {0x258, 0x08},
	{0x259, 0x08}, {0x25a, 0x08}, {0x25b, 0x08},
	{0x260, 0x08}, {0x261, 0x08}, {0x262, 0x08},
	{0x263, 0x08}, {0x264, 0xcf}, {0x272, 0x56},
	{0x273, 0x9a}, {0x034, 0xf0}, {0x035, 0x0f},
	{0x05b, 0x40}, {0x084, 0x88}, {0x085, 0x24},
	{0x088, 0x54}, {0x08b, 0xb8}, {0x08c, 0x07},
	{0x08d, 0x00}, {0x094, 0x1b}, {0x095, 0x12},
	{0x096, 0x00}, {0x097, 0x06}, {0x09d, 0x1a},
	{0x09f, 0x10}, {0x0b4, 0x22}, {0x0be, 0x80},
	{0x0db, 0x00}, {0x0ee, 0x00}, {0x091, 0x03},
	{0x24c, 0x00}, {0x39f, 0x00}, {0x08c, 0x01},
	{0x08d, 0x10}, {0x08e, 0x08}, {0x08f, 0x00}
};

static struct urtw_pair urtw_8225v2_rf_part2[] = {
	{ 0x00, 0x01 }, { 0x01, 0x02 }, { 0x02, 0x42 }, { 0x03, 0x00 },
	{ 0x04, 0x00 },	{ 0x05, 0x00 }, { 0x06, 0x40 }, { 0x07, 0x00 },
	{ 0x08, 0x40 }, { 0x09, 0xfe }, { 0x0a, 0x08 }, { 0x0b, 0x80 },
	{ 0x0c, 0x01 }, { 0x0d, 0x43 }, { 0x0e, 0xd3 }, { 0x0f, 0x38 },
	{ 0x10, 0x84 }, { 0x11, 0x07 }, { 0x12, 0x20 }, { 0x13, 0x20 },
	{ 0x14, 0x00 }, { 0x15, 0x40 }, { 0x16, 0x00 }, { 0x17, 0x40 },
	{ 0x18, 0xef }, { 0x19, 0x19 }, { 0x1a, 0x20 }, { 0x1b, 0x15 },
	{ 0x1c, 0x04 }, { 0x1d, 0xc5 }, { 0x1e, 0x95 }, { 0x1f, 0x75 },
	{ 0x20, 0x1f }, { 0x21, 0x17 }, { 0x22, 0x16 }, { 0x23, 0x80 },
	{ 0x24, 0x46 }, { 0x25, 0x00 }, { 0x26, 0x90 }, { 0x27, 0x88 }
};

static struct urtw_pair urtw_8225v2b_rf_part2[] = {
	{ 0x00, 0x10 }, { 0x01, 0x0d }, { 0x02, 0x01 }, { 0x03, 0x00 },
	{ 0x04, 0x14 }, { 0x05, 0xfb }, { 0x06, 0xfb }, { 0x07, 0x60 },
	{ 0x08, 0x00 }, { 0x09, 0x60 }, { 0x0a, 0x00 }, { 0x0b, 0x00 },
	{ 0x0c, 0x00 }, { 0x0d, 0x5c }, { 0x0e, 0x00 }, { 0x0f, 0x00 },
	{ 0x10, 0x40 }, { 0x11, 0x00 }, { 0x12, 0x40 }, { 0x13, 0x00 },
	{ 0x14, 0x00 }, { 0x15, 0x00 }, { 0x16, 0xa8 }, { 0x17, 0x26 },
	{ 0x18, 0x32 }, { 0x19, 0x33 }, { 0x1a, 0x07 }, { 0x1b, 0xa5 },
	{ 0x1c, 0x6f }, { 0x1d, 0x55 }, { 0x1e, 0xc8 }, { 0x1f, 0xb3 },
	{ 0x20, 0x0a }, { 0x21, 0xe1 }, { 0x22, 0x2C }, { 0x23, 0x8a },
	{ 0x24, 0x86 }, { 0x25, 0x83 }, { 0x26, 0x34 }, { 0x27, 0x0f },
	{ 0x28, 0x4f }, { 0x29, 0x24 }, { 0x2a, 0x6f }, { 0x2b, 0xc2 },
	{ 0x2c, 0x6b }, { 0x2d, 0x40 }, { 0x2e, 0x80 }, { 0x2f, 0x00 },
	{ 0x30, 0xc0 }, { 0x31, 0xc1 }, { 0x32, 0x58 }, { 0x33, 0xf1 },
	{ 0x34, 0x00 }, { 0x35, 0xe4 }, { 0x36, 0x90 }, { 0x37, 0x3e },
	{ 0x38, 0x6d }, { 0x39, 0x3c }, { 0x3a, 0xfb }, { 0x3b, 0x07 }
};

static struct urtw_pair urtw_8225v2_rf_part3[] = {
	{ 0x00, 0x98 }, { 0x03, 0x20 }, { 0x04, 0x7e }, { 0x05, 0x12 },
	{ 0x06, 0xfc }, { 0x07, 0x78 }, { 0x08, 0x2e }, { 0x09, 0x11 },
	{ 0x0a, 0x17 }, { 0x0b, 0x11 }, { 0x10, 0x9b }, { 0x11, 0x88 },
	{ 0x12, 0x47 }, { 0x13, 0xd0 }, { 0x19, 0x00 }, { 0x1a, 0xa0 },
	{ 0x1b, 0x08 }, { 0x1d, 0x00 }, { 0x40, 0x86 }, { 0x41, 0x9d },
	{ 0x42, 0x15 }, { 0x43, 0x18 }, { 0x44, 0x36 }, { 0x45, 0x35 },
	{ 0x46, 0x2e }, { 0x47, 0x25 }, { 0x48, 0x1c }, { 0x49, 0x12 },
	{ 0x4a, 0x09 }, { 0x4b, 0x04 }, { 0x4c, 0x05 }
};

static uint16_t urtw_8225v2_rxgain[] = {
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0008, 0x0009,
	0x000a, 0x000b, 0x0102, 0x0103, 0x0104, 0x0105, 0x0140, 0x0141,
	0x0142, 0x0143, 0x0144, 0x0145, 0x0180, 0x0181, 0x0182, 0x0183,
	0x0184, 0x0185, 0x0188, 0x0189, 0x018a, 0x018b, 0x0243, 0x0244,
	0x0245, 0x0280, 0x0281, 0x0282, 0x0283, 0x0284, 0x0285, 0x0288,
	0x0289, 0x028a, 0x028b, 0x028c, 0x0342, 0x0343, 0x0344, 0x0345,
	0x0380, 0x0381, 0x0382, 0x0383, 0x0384, 0x0385, 0x0388, 0x0389,
	0x038a, 0x038b, 0x038c, 0x038d, 0x0390, 0x0391, 0x0392, 0x0393,
	0x0394, 0x0395, 0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 0x039d,
	0x03a0, 0x03a1, 0x03a2, 0x03a3, 0x03a4, 0x03a5, 0x03a8, 0x03a9,
	0x03aa, 0x03ab, 0x03ac, 0x03ad, 0x03b0, 0x03b1, 0x03b2, 0x03b3,
	0x03b4, 0x03b5, 0x03b8, 0x03b9, 0x03ba, 0x03bb, 0x03bb
};

static uint16_t urtw_8225v2b_rxgain[] = {
	0x0400, 0x0401, 0x0402, 0x0403, 0x0404, 0x0405, 0x0408, 0x0409,
	0x040a, 0x040b, 0x0502, 0x0503, 0x0504, 0x0505, 0x0540, 0x0541,
	0x0542, 0x0543, 0x0544, 0x0545, 0x0580, 0x0581, 0x0582, 0x0583,
	0x0584, 0x0585, 0x0588, 0x0589, 0x058a, 0x058b, 0x0643, 0x0644,
	0x0645, 0x0680, 0x0681, 0x0682, 0x0683, 0x0684, 0x0685, 0x0688,
	0x0689, 0x068a, 0x068b, 0x068c, 0x0742, 0x0743, 0x0744, 0x0745,
	0x0780, 0x0781, 0x0782, 0x0783, 0x0784, 0x0785, 0x0788, 0x0789,
	0x078a, 0x078b, 0x078c, 0x078d, 0x0790, 0x0791, 0x0792, 0x0793,
	0x0794, 0x0795, 0x0798, 0x0799, 0x079a, 0x079b, 0x079c, 0x079d,
	0x07a0, 0x07a1, 0x07a2, 0x07a3, 0x07a4, 0x07a5, 0x07a8, 0x07a9,
	0x03aa, 0x03ab, 0x03ac, 0x03ad, 0x03b0, 0x03b1, 0x03b2, 0x03b3,
	0x03b4, 0x03b5, 0x03b8, 0x03b9, 0x03ba, 0x03bb, 0x03bb
};

static uint8_t urtw_8225v2_tx_gain_cck_ofdm[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
	0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
	0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
};

static uint8_t urtw_8225v2_txpwr_cck[] = {
	0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04
};

static uint8_t urtw_8225v2_txpwr_cck_ch14[] = {
	0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00
};

static uint8_t urtw_8225v2b_txpwr_cck[] = {
	0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04,
	0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03,
	0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03,
	0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03
};

static uint8_t urtw_8225v2b_txpwr_cck_ch14[] = {
	0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00,
	0x30, 0x2f, 0x29, 0x15, 0x00, 0x00, 0x00, 0x00,
	0x30, 0x2f, 0x29, 0x15, 0x00, 0x00, 0x00, 0x00,
	0x30, 0x2f, 0x29, 0x15, 0x00, 0x00, 0x00, 0x00
};

static struct urtw_pair urtw_ratetable[] = {
	{  2,  0 }, {   4,  1 }, { 11, 2 }, { 12, 4 }, { 18, 5 },
	{ 22,  3 }, {  24,  6 }, { 36, 7 }, { 48, 8 }, { 72, 9 },
	{ 96, 10 }, { 108, 11 }
};

#if 0
static const uint8_t urtw_8187b_reg_table[][3] = {
	{ 0xf0, 0x32, 0 }, { 0xf1, 0x32, 0 }, { 0xf2, 0x00, 0 },
	{ 0xf3, 0x00, 0 }, { 0xf4, 0x32, 0 }, { 0xf5, 0x43, 0 },
	{ 0xf6, 0x00, 0 }, { 0xf7, 0x00, 0 }, { 0xf8, 0x46, 0 },
	{ 0xf9, 0xa4, 0 }, { 0xfa, 0x00, 0 }, { 0xfb, 0x00, 0 },
	{ 0xfc, 0x96, 0 }, { 0xfd, 0xa4, 0 }, { 0xfe, 0x00, 0 },
	{ 0xff, 0x00, 0 }, { 0x58, 0x4b, 1 }, { 0x59, 0x00, 1 },
	{ 0x5a, 0x4b, 1 }, { 0x5b, 0x00, 1 }, { 0x60, 0x4b, 1 },
	{ 0x61, 0x09, 1 }, { 0x62, 0x4b, 1 }, { 0x63, 0x09, 1 },
	{ 0xce, 0x0f, 1 }, { 0xcf, 0x00, 1 }, { 0xe0, 0xff, 1 },
	{ 0xe1, 0x0f, 1 }, { 0xe2, 0x00, 1 }, { 0xf0, 0x4e, 1 },
	{ 0xf1, 0x01, 1 }, { 0xf2, 0x02, 1 }, { 0xf3, 0x03, 1 },
	{ 0xf4, 0x04, 1 }, { 0xf5, 0x05, 1 }, { 0xf6, 0x06, 1 },
	{ 0xf7, 0x07, 1 }, { 0xf8, 0x08, 1 }, { 0x4e, 0x00, 2 },
	{ 0x0c, 0x04, 2 }, { 0x21, 0x61, 2 }, { 0x22, 0x68, 2 },
	{ 0x23, 0x6f, 2 }, { 0x24, 0x76, 2 }, { 0x25, 0x7d, 2 },
	{ 0x26, 0x84, 2 }, { 0x27, 0x8d, 2 }, { 0x4d, 0x08, 2 },
	{ 0x50, 0x05, 2 }, { 0x51, 0xf5, 2 }, { 0x52, 0x04, 2 },
	{ 0x53, 0xa0, 2 }, { 0x54, 0x1f, 2 }, { 0x55, 0x23, 2 },
	{ 0x56, 0x45, 2 }, { 0x57, 0x67, 2 }, { 0x58, 0x08, 2 },
	{ 0x59, 0x08, 2 }, { 0x5a, 0x08, 2 }, { 0x5b, 0x08, 2 },
	{ 0x60, 0x08, 2 }, { 0x61, 0x08, 2 }, { 0x62, 0x08, 2 },
	{ 0x63, 0x08, 2 }, { 0x64, 0xcf, 2 }, { 0x72, 0x56, 2 },
	{ 0x73, 0x9a, 2 }, { 0x34, 0xf0, 0 }, { 0x35, 0x0f, 0 },
	{ 0x5b, 0x40, 0 }, { 0x84, 0x88, 0 }, { 0x85, 0x24, 0 },
	{ 0x88, 0x54, 0 }, { 0x8b, 0xb8, 0 }, { 0x8c, 0x07, 0 },
	{ 0x8d, 0x00, 0 }, { 0x94, 0x1b, 0 }, { 0x95, 0x12, 0 },
	{ 0x96, 0x00, 0 }, { 0x97, 0x06, 0 }, { 0x9d, 0x1a, 0 },
	{ 0x9f, 0x10, 0 }, { 0xb4, 0x22, 0 }, { 0xbe, 0x80, 0 },
	{ 0xdb, 0x00, 0 }, { 0xee, 0x00, 0 }, { 0x91, 0x03, 0 },
	{ 0x4c, 0x00, 2 }, { 0x9f, 0x00, 3 }, { 0x8c, 0x01, 0 },
	{ 0x8d, 0x10, 0 }, { 0x8e, 0x08, 0 }, { 0x8f, 0x00, 0 }
};
#endif

static usb_callback_t urtw_bulk_rx_callback;
static usb_callback_t urtw_bulk_tx_callback;
static usb_callback_t urtw_bulk_tx_status_callback;

static const struct usb_config urtw_8187b_usbconfig[URTW_8187B_N_XFERS] = {
	[URTW_8187B_BULK_RX] = {
		.type = UE_BULK,
		.endpoint = 0x83,
		.direction = UE_DIR_IN,
		.bufsize = MCLBYTES,
		.flags = {
			.ext_buffer = 1,
			.pipe_bof = 1,
			.short_xfer_ok = 1
		},
		.callback = urtw_bulk_rx_callback
	},
	[URTW_8187B_BULK_TX_STATUS] = {
		.type = UE_BULK,
		.endpoint = 0x89,
		.direction = UE_DIR_IN,
		.bufsize = sizeof(uint64_t),
		.flags = {
			.pipe_bof = 1,
			.short_xfer_ok = 1
		},
		.callback = urtw_bulk_tx_status_callback
	},
	[URTW_8187B_BULK_TX_BE] = {
		.type = UE_BULK,
		.endpoint = URTW_8187B_TXPIPE_BE,
		.direction = UE_DIR_OUT,
		.bufsize = URTW_TX_MAXSIZE * URTW_TX_DATA_LIST_COUNT,
		.flags = {
			.force_short_xfer = 1,
			.pipe_bof = 1,
		},
		.callback = urtw_bulk_tx_callback,
		.timeout = URTW_DATA_TIMEOUT
	},
	[URTW_8187B_BULK_TX_BK] = {
		.type = UE_BULK,
		.endpoint = URTW_8187B_TXPIPE_BK,
		.direction = UE_DIR_OUT,
		.bufsize = URTW_TX_MAXSIZE,
		.flags = {
			.ext_buffer = 1,
			.force_short_xfer = 1,
			.pipe_bof = 1,
		},
		.callback = urtw_bulk_tx_callback,
		.timeout = URTW_DATA_TIMEOUT
	},
	[URTW_8187B_BULK_TX_VI] = {
		.type = UE_BULK,
		.endpoint = URTW_8187B_TXPIPE_VI,
		.direction = UE_DIR_OUT,
		.bufsize = URTW_TX_MAXSIZE,
		.flags = {
			.ext_buffer = 1,
			.force_short_xfer = 1,
			.pipe_bof = 1,
		},
		.callback = urtw_bulk_tx_callback,
		.timeout = URTW_DATA_TIMEOUT
	},
	[URTW_8187B_BULK_TX_VO] = {
		.type = UE_BULK,
		.endpoint = URTW_8187B_TXPIPE_VO,
		.direction = UE_DIR_OUT,
		.bufsize = URTW_TX_MAXSIZE,
		.flags = {
			.ext_buffer = 1,
			.force_short_xfer = 1,
			.pipe_bof = 1,
		},
		.callback = urtw_bulk_tx_callback,
		.timeout = URTW_DATA_TIMEOUT
	},
	[URTW_8187B_BULK_TX_EP12] = {
		.type = UE_BULK,
		.endpoint = 0xc,
		.direction = UE_DIR_OUT,
		.bufsize = URTW_TX_MAXSIZE,
		.flags = {
			.ext_buffer = 1,
			.force_short_xfer = 1,
			.pipe_bof = 1,
		},
		.callback = urtw_bulk_tx_callback,
		.timeout = URTW_DATA_TIMEOUT
	}
};

static const struct usb_config urtw_8187l_usbconfig[URTW_8187L_N_XFERS] = {
	[URTW_8187L_BULK_RX] = {
		.type = UE_BULK,
		.endpoint = 0x81,
		.direction = UE_DIR_IN,
		.bufsize = MCLBYTES,
		.flags = {
			.ext_buffer = 1,
			.pipe_bof = 1,
			.short_xfer_ok = 1
		},
		.callback = urtw_bulk_rx_callback
	},
	[URTW_8187L_BULK_TX_LOW] = {
		.type = UE_BULK,
		.endpoint = 0x2,
		.direction = UE_DIR_OUT,
		.bufsize = URTW_TX_MAXSIZE * URTW_TX_DATA_LIST_COUNT,
		.flags = {
			.force_short_xfer = 1,
			.pipe_bof = 1,
		},
		.callback = urtw_bulk_tx_callback,
		.timeout = URTW_DATA_TIMEOUT
	},
	[URTW_8187L_BULK_TX_NORMAL] = {
		.type = UE_BULK,
		.endpoint = 0x3,
		.direction = UE_DIR_OUT,
		.bufsize = URTW_TX_MAXSIZE,
		.flags = {
			.ext_buffer = 1,
			.force_short_xfer = 1,
			.pipe_bof = 1,
		},
		.callback = urtw_bulk_tx_callback,
		.timeout = URTW_DATA_TIMEOUT
	},
};

static struct ieee80211vap *urtw_vap_create(struct ieee80211com *,
			    const char [IFNAMSIZ], int, enum ieee80211_opmode,
			    int, const uint8_t [IEEE80211_ADDR_LEN],
			    const uint8_t [IEEE80211_ADDR_LEN]);
static void		urtw_vap_delete(struct ieee80211vap *);
static void		urtw_init(struct urtw_softc *);
static void		urtw_stop(struct urtw_softc *);
static void		urtw_parent(struct ieee80211com *);
static int		urtw_transmit(struct ieee80211com *, struct mbuf *);
static void		urtw_start(struct urtw_softc *);
static int		urtw_alloc_rx_data_list(struct urtw_softc *);
static int		urtw_alloc_tx_data_list(struct urtw_softc *);
static int		urtw_raw_xmit(struct ieee80211_node *, struct mbuf *,
			    const struct ieee80211_bpf_params *);
static void		urtw_scan_start(struct ieee80211com *);
static void		urtw_scan_end(struct ieee80211com *);
static void		urtw_getradiocaps(struct ieee80211com *, int, int *,
			   struct ieee80211_channel[]);
static void		urtw_set_channel(struct ieee80211com *);
static void		urtw_update_promisc(struct ieee80211com *);
static void		urtw_update_mcast(struct ieee80211com *);
static int		urtw_tx_start(struct urtw_softc *,
			    struct ieee80211_node *, struct mbuf *,
			    struct urtw_data *, int);
static int		urtw_newstate(struct ieee80211vap *,
			    enum ieee80211_state, int);
static void		urtw_led_ch(void *);
static void		urtw_ledtask(void *, int);
static void		urtw_watchdog(void *);
static void		urtw_set_multi(void *);
static int		urtw_isbmode(uint16_t);
static uint16_t		urtw_rtl2rate(uint32_t);
static usb_error_t	urtw_set_rate(struct urtw_softc *);
static usb_error_t	urtw_update_msr(struct urtw_softc *);
static usb_error_t	urtw_read8_c(struct urtw_softc *, int, uint8_t *);
static usb_error_t	urtw_read16_c(struct urtw_softc *, int, uint16_t *);
static usb_error_t	urtw_read32_c(struct urtw_softc *, int, uint32_t *);
static usb_error_t	urtw_write8_c(struct urtw_softc *, int, uint8_t);
static usb_error_t	urtw_write16_c(struct urtw_softc *, int, uint16_t);
static usb_error_t	urtw_write32_c(struct urtw_softc *, int, uint32_t);
static usb_error_t	urtw_eprom_cs(struct urtw_softc *, int);
static usb_error_t	urtw_eprom_ck(struct urtw_softc *);
static usb_error_t	urtw_eprom_sendbits(struct urtw_softc *, int16_t *,
			    int);
static usb_error_t	urtw_eprom_read32(struct urtw_softc *, uint32_t,
			    uint32_t *);
static usb_error_t	urtw_eprom_readbit(struct urtw_softc *, int16_t *);
static usb_error_t	urtw_eprom_writebit(struct urtw_softc *, int16_t);
static usb_error_t	urtw_get_macaddr(struct urtw_softc *);
static usb_error_t	urtw_get_txpwr(struct urtw_softc *);
static usb_error_t	urtw_get_rfchip(struct urtw_softc *);
static usb_error_t	urtw_led_init(struct urtw_softc *);
static usb_error_t	urtw_8185_rf_pins_enable(struct urtw_softc *);
static usb_error_t	urtw_8185_tx_antenna(struct urtw_softc *, uint8_t);
static usb_error_t	urtw_8187_write_phy(struct urtw_softc *, uint8_t,
			    uint32_t);
static usb_error_t	urtw_8187_write_phy_ofdm_c(struct urtw_softc *,
			    uint8_t, uint32_t);
static usb_error_t	urtw_8187_write_phy_cck_c(struct urtw_softc *, uint8_t,
			    uint32_t);
static usb_error_t	urtw_8225_setgain(struct urtw_softc *, int16_t);
static usb_error_t	urtw_8225_usb_init(struct urtw_softc *);
static usb_error_t	urtw_8225_write_c(struct urtw_softc *, uint8_t,
			    uint16_t);
static usb_error_t	urtw_8225_write_s16(struct urtw_softc *, uint8_t, int,
			    uint16_t *);
static usb_error_t	urtw_8225_read(struct urtw_softc *, uint8_t,
			    uint32_t *);
static usb_error_t	urtw_8225_rf_init(struct urtw_softc *);
static usb_error_t	urtw_8225_rf_set_chan(struct urtw_softc *, int);
static usb_error_t	urtw_8225_rf_set_sens(struct urtw_softc *, int);
static usb_error_t	urtw_8225_set_txpwrlvl(struct urtw_softc *, int);
static usb_error_t	urtw_8225_rf_stop(struct urtw_softc *);
static usb_error_t	urtw_8225v2_rf_init(struct urtw_softc *);
static usb_error_t	urtw_8225v2_rf_set_chan(struct urtw_softc *, int);
static usb_error_t	urtw_8225v2_set_txpwrlvl(struct urtw_softc *, int);
static usb_error_t	urtw_8225v2_setgain(struct urtw_softc *, int16_t);
static usb_error_t	urtw_8225_isv2(struct urtw_softc *, int *);
static usb_error_t	urtw_8225v2b_rf_init(struct urtw_softc *);
static usb_error_t	urtw_8225v2b_rf_set_chan(struct urtw_softc *, int);
static usb_error_t	urtw_read8e(struct urtw_softc *, int, uint8_t *);
static usb_error_t	urtw_write8e(struct urtw_softc *, int, uint8_t);
static usb_error_t	urtw_8180_set_anaparam(struct urtw_softc *, uint32_t);
static usb_error_t	urtw_8185_set_anaparam2(struct urtw_softc *, uint32_t);
static usb_error_t	urtw_intr_enable(struct urtw_softc *);
static usb_error_t	urtw_intr_disable(struct urtw_softc *);
static usb_error_t	urtw_reset(struct urtw_softc *);
static usb_error_t	urtw_led_on(struct urtw_softc *, int);
static usb_error_t	urtw_led_ctl(struct urtw_softc *, int);
static usb_error_t	urtw_led_blink(struct urtw_softc *);
static usb_error_t	urtw_led_mode0(struct urtw_softc *, int);
static usb_error_t	urtw_led_mode1(struct urtw_softc *, int);
static usb_error_t	urtw_led_mode2(struct urtw_softc *, int);
static usb_error_t	urtw_led_mode3(struct urtw_softc *, int);
static usb_error_t	urtw_rx_setconf(struct urtw_softc *);
static usb_error_t	urtw_rx_enable(struct urtw_softc *);
static usb_error_t	urtw_tx_enable(struct urtw_softc *sc);
static void		urtw_free_tx_data_list(struct urtw_softc *);
static void		urtw_free_rx_data_list(struct urtw_softc *);
static void		urtw_free_data_list(struct urtw_softc *,
			    struct urtw_data data[], int, int);
static usb_error_t	urtw_adapter_start(struct urtw_softc *);
static usb_error_t	urtw_adapter_start_b(struct urtw_softc *);
static usb_error_t	urtw_set_mode(struct urtw_softc *, uint32_t);
static usb_error_t	urtw_8187b_cmd_reset(struct urtw_softc *);
static usb_error_t	urtw_do_request(struct urtw_softc *,
			    struct usb_device_request *, void *);
static usb_error_t	urtw_8225v2b_set_txpwrlvl(struct urtw_softc *, int);
static usb_error_t	urtw_led_off(struct urtw_softc *, int);
static void		urtw_abort_xfers(struct urtw_softc *);
static struct urtw_data *
			urtw_getbuf(struct urtw_softc *sc);
static int		urtw_compute_txtime(uint16_t, uint16_t, uint8_t,
			    uint8_t);
static void		urtw_updateslot(struct ieee80211com *);
static void		urtw_updateslottask(void *, int);
static void		urtw_sysctl_node(struct urtw_softc *);

static int
urtw_match(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != URTW_CONFIG_INDEX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != URTW_IFACE_INDEX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(urtw_devs, sizeof(urtw_devs), uaa));
}

static int
urtw_attach(device_t dev)
{
	const struct usb_config *setup_start;
	int ret = ENXIO;
	struct urtw_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t iface_index = URTW_IFACE_INDEX;		/* XXX */
	uint16_t n_setup;
	uint32_t data;
	usb_error_t error;

	device_set_usb_desc(dev);

	sc->sc_dev = dev;
	sc->sc_udev = uaa->device;
	if (USB_GET_DRIVER_INFO(uaa) == URTW_REV_RTL8187B)
		sc->sc_flags |= URTW_RTL8187B;
#ifdef URTW_DEBUG
	sc->sc_debug = urtw_debug;
#endif

	mtx_init(&sc->sc_mtx, device_get_nameunit(sc->sc_dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	usb_callout_init_mtx(&sc->sc_led_ch, &sc->sc_mtx, 0);
	TASK_INIT(&sc->sc_led_task, 0, urtw_ledtask, sc);
	TASK_INIT(&sc->sc_updateslot_task, 0, urtw_updateslottask, sc);
	callout_init(&sc->sc_watchdog_ch, 0);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	if (sc->sc_flags & URTW_RTL8187B) {
		setup_start = urtw_8187b_usbconfig;
		n_setup = URTW_8187B_N_XFERS;
	} else {
		setup_start = urtw_8187l_usbconfig;
		n_setup = URTW_8187L_N_XFERS;
	}

	error = usbd_transfer_setup(uaa->device, &iface_index, sc->sc_xfer,
	    setup_start, n_setup, sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "could not allocate USB transfers, "
		    "err=%s\n", usbd_errstr(error));
		ret = ENXIO;
		goto fail0;
	}

	if (sc->sc_flags & URTW_RTL8187B) {
		sc->sc_tx_dma_buf = 
		    usbd_xfer_get_frame_buffer(sc->sc_xfer[
		    URTW_8187B_BULK_TX_BE], 0);
	} else {
		sc->sc_tx_dma_buf =
		    usbd_xfer_get_frame_buffer(sc->sc_xfer[
		    URTW_8187L_BULK_TX_LOW], 0);
	}

	URTW_LOCK(sc);

	urtw_read32_m(sc, URTW_RX, &data);
	sc->sc_epromtype = (data & URTW_RX_9356SEL) ? URTW_EEPROM_93C56 :
	    URTW_EEPROM_93C46;

	error = urtw_get_rfchip(sc);
	if (error != 0)
		goto fail;
	error = urtw_get_macaddr(sc);
	if (error != 0)
		goto fail;
	error = urtw_get_txpwr(sc);
	if (error != 0)
		goto fail;
	error = urtw_led_init(sc);
	if (error != 0)
		goto fail;

	URTW_UNLOCK(sc);

	sc->sc_rts_retry = URTW_DEFAULT_RTS_RETRY;
	sc->sc_tx_retry = URTW_DEFAULT_TX_RETRY;
	sc->sc_currate = URTW_RIDX_CCK11;
	sc->sc_preamble_mode = urtw_preamble_mode;

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
	    IEEE80211_C_BGSCAN |	/* capable of bg scanning */
	    IEEE80211_C_WPA;		/* 802.11i */

	/* XXX TODO: setup regdomain if URTW_EPROM_CHANPLAN_BY_HW bit is set.*/
 
	urtw_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	ieee80211_ifattach(ic);
	ic->ic_raw_xmit = urtw_raw_xmit;
	ic->ic_scan_start = urtw_scan_start;
	ic->ic_scan_end = urtw_scan_end;
	ic->ic_getradiocaps = urtw_getradiocaps;
	ic->ic_set_channel = urtw_set_channel;
	ic->ic_updateslot = urtw_updateslot;
	ic->ic_vap_create = urtw_vap_create;
	ic->ic_vap_delete = urtw_vap_delete;
	ic->ic_update_promisc = urtw_update_promisc;
	ic->ic_update_mcast = urtw_update_mcast;
	ic->ic_parent = urtw_parent;
	ic->ic_transmit = urtw_transmit;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_txtap.wt_ihdr, sizeof(sc->sc_txtap),
	    URTW_TX_RADIOTAP_PRESENT,
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
	    URTW_RX_RADIOTAP_PRESENT);

	urtw_sysctl_node(sc);

	if (bootverbose)
		ieee80211_announce(ic);
	return (0);

fail:
	URTW_UNLOCK(sc);
	usbd_transfer_unsetup(sc->sc_xfer, (sc->sc_flags & URTW_RTL8187B) ?
	    URTW_8187B_N_XFERS : URTW_8187L_N_XFERS);
fail0:
	return (ret);
}

static int
urtw_detach(device_t dev)
{
	struct urtw_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	unsigned int x;
	unsigned int n_xfers;

	/* Prevent further ioctls */
	URTW_LOCK(sc);
	sc->sc_flags |= URTW_DETACHED;
	urtw_stop(sc);
	URTW_UNLOCK(sc);

	ieee80211_draintask(ic, &sc->sc_updateslot_task);
	ieee80211_draintask(ic, &sc->sc_led_task);

	usb_callout_drain(&sc->sc_led_ch);
	callout_drain(&sc->sc_watchdog_ch);

	n_xfers = (sc->sc_flags & URTW_RTL8187B) ?
	    URTW_8187B_N_XFERS : URTW_8187L_N_XFERS;

	/* prevent further allocations from RX/TX data lists */
	URTW_LOCK(sc);
	STAILQ_INIT(&sc->sc_tx_active);
	STAILQ_INIT(&sc->sc_tx_inactive);
	STAILQ_INIT(&sc->sc_tx_pending);

	STAILQ_INIT(&sc->sc_rx_active);
	STAILQ_INIT(&sc->sc_rx_inactive);
	URTW_UNLOCK(sc);

	/* drain USB transfers */
	for (x = 0; x != n_xfers; x++)
		usbd_transfer_drain(sc->sc_xfer[x]);

	/* free data buffers */
	URTW_LOCK(sc);
	urtw_free_tx_data_list(sc);
	urtw_free_rx_data_list(sc);
	URTW_UNLOCK(sc);

	/* free USB transfers and some data buffers */
	usbd_transfer_unsetup(sc->sc_xfer, n_xfers);

	ieee80211_ifdetach(ic);
	mbufq_drain(&sc->sc_snd);
	mtx_destroy(&sc->sc_mtx);
	return (0);
}

static void
urtw_free_tx_data_list(struct urtw_softc *sc)
{
	urtw_free_data_list(sc, sc->sc_tx, URTW_TX_DATA_LIST_COUNT, 0);
}

static void
urtw_free_rx_data_list(struct urtw_softc *sc)
{
	urtw_free_data_list(sc, sc->sc_rx, URTW_RX_DATA_LIST_COUNT, 1);
}

static void
urtw_free_data_list(struct urtw_softc *sc, struct urtw_data data[], int ndata,
    int fillmbuf)
{
	int i;

	for (i = 0; i < ndata; i++) {
		struct urtw_data *dp = &data[i];

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

static struct ieee80211vap *
urtw_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct urtw_vap *uvp;
	struct ieee80211vap *vap;

	if (!TAILQ_EMPTY(&ic->ic_vaps))		/* only one at a time */
		return (NULL);
	uvp = malloc(sizeof(struct urtw_vap), M_80211_VAP, M_WAITOK | M_ZERO);
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
	vap->iv_newstate = urtw_newstate;

	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change,
	    ieee80211_media_status, mac);
	ic->ic_opmode = opmode;
	return (vap);
}

static void
urtw_vap_delete(struct ieee80211vap *vap)
{
	struct urtw_vap *uvp = URTW_VAP(vap);

	ieee80211_vap_detach(vap);
	free(uvp, M_80211_VAP);
}

static void
urtw_init(struct urtw_softc *sc)
{
	usb_error_t error;
	int ret;

	URTW_ASSERT_LOCKED(sc);

	if (sc->sc_flags & URTW_RUNNING)
		urtw_stop(sc);

	error = (sc->sc_flags & URTW_RTL8187B) ? urtw_adapter_start_b(sc) :
	    urtw_adapter_start(sc);
	if (error != 0)
		goto fail;

	/* reset softc variables  */
	sc->sc_txtimer = 0;

	if (!(sc->sc_flags & URTW_INIT_ONCE)) {
		ret = urtw_alloc_rx_data_list(sc);
		if (ret != 0)
			goto fail;
		ret = urtw_alloc_tx_data_list(sc);
		if (ret != 0)
			goto fail;
		sc->sc_flags |= URTW_INIT_ONCE;
	}

	error = urtw_rx_enable(sc);
	if (error != 0)
		goto fail;
	error = urtw_tx_enable(sc);
	if (error != 0)
		goto fail;

	if (sc->sc_flags & URTW_RTL8187B)
		usbd_transfer_start(sc->sc_xfer[URTW_8187B_BULK_TX_STATUS]);

	sc->sc_flags |= URTW_RUNNING;

	callout_reset(&sc->sc_watchdog_ch, hz, urtw_watchdog, sc);
fail:
	return;
}

static usb_error_t
urtw_adapter_start_b(struct urtw_softc *sc)
{
	uint8_t data8;
	usb_error_t error;

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;

	urtw_read8_m(sc, URTW_CONFIG3, &data8);
	urtw_write8_m(sc, URTW_CONFIG3,
	    data8 | URTW_CONFIG3_ANAPARAM_WRITE | URTW_CONFIG3_GNT_SELECT);
	urtw_write32_m(sc, URTW_ANAPARAM2, URTW_8187B_8225_ANAPARAM2_ON);
	urtw_write32_m(sc, URTW_ANAPARAM, URTW_8187B_8225_ANAPARAM_ON);
	urtw_write8_m(sc, URTW_ANAPARAM3, URTW_8187B_8225_ANAPARAM3_ON);

	urtw_write8_m(sc, 0x61, 0x10);
	urtw_read8_m(sc, 0x62, &data8);
	urtw_write8_m(sc, 0x62, data8 & ~(1 << 5));
	urtw_write8_m(sc, 0x62, data8 | (1 << 5));

	urtw_read8_m(sc, URTW_CONFIG3, &data8);
	data8 &= ~URTW_CONFIG3_ANAPARAM_WRITE;
	urtw_write8_m(sc, URTW_CONFIG3, data8);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	error = urtw_8187b_cmd_reset(sc);
	if (error)
		goto fail;

	error = sc->sc_rf_init(sc);
	if (error != 0)
		goto fail;
	urtw_write8_m(sc, URTW_CMD, URTW_CMD_RX_ENABLE | URTW_CMD_TX_ENABLE);

	/* fix RTL8187B RX stall */
	error = urtw_intr_enable(sc);
	if (error)
		goto fail;

	error = urtw_write8e(sc, 0x41, 0xf4);
	if (error)
		goto fail;
	error = urtw_write8e(sc, 0x40, 0x00);
	if (error)
		goto fail;
	error = urtw_write8e(sc, 0x42, 0x00);
	if (error)
		goto fail;
	error = urtw_write8e(sc, 0x42, 0x01);
	if (error)
		goto fail;
	error = urtw_write8e(sc, 0x40, 0x0f);
	if (error)
		goto fail;
	error = urtw_write8e(sc, 0x42, 0x00);
	if (error)
		goto fail;
	error = urtw_write8e(sc, 0x42, 0x01);
	if (error)
		goto fail;

	urtw_read8_m(sc, 0xdb, &data8);
	urtw_write8_m(sc, 0xdb, data8 | (1 << 2));
	urtw_write16_m(sc, 0x372, 0x59fa);
	urtw_write16_m(sc, 0x374, 0x59d2);
	urtw_write16_m(sc, 0x376, 0x59d2);
	urtw_write16_m(sc, 0x378, 0x19fa);
	urtw_write16_m(sc, 0x37a, 0x19fa);
	urtw_write16_m(sc, 0x37c, 0x00d0);
	urtw_write8_m(sc, 0x61, 0);

	urtw_write8_m(sc, 0x180, 0x0f);
	urtw_write8_m(sc, 0x183, 0x03);
	urtw_write8_m(sc, 0xda, 0x10);
	urtw_write8_m(sc, 0x24d, 0x08);
	urtw_write32_m(sc, URTW_HSSI_PARA, 0x0600321b);

	urtw_write16_m(sc, 0x1ec, 0x800);	/* RX MAX SIZE */
fail:
	return (error);
}

static usb_error_t
urtw_adapter_start(struct urtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	usb_error_t error;

	error = urtw_reset(sc);
	if (error)
		goto fail;

	urtw_write8_m(sc, URTW_ADDR_MAGIC1, 0);
	urtw_write8_m(sc, URTW_GPIO, 0);

	/* for led  */
	urtw_write8_m(sc, URTW_ADDR_MAGIC1, 4);
	error = urtw_led_ctl(sc, URTW_LED_CTL_POWER_ON);
	if (error != 0)
		goto fail;

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;
	/* applying MAC address again.  */
	urtw_write32_m(sc, URTW_MAC0, ((uint32_t *)ic->ic_macaddr)[0]);
	urtw_write16_m(sc, URTW_MAC4, ((uint32_t *)ic->ic_macaddr)[1] & 0xffff);
	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	error = urtw_update_msr(sc);
	if (error)
		goto fail;

	urtw_write32_m(sc, URTW_INT_TIMEOUT, 0);
	urtw_write8_m(sc, URTW_WPA_CONFIG, 0);
	urtw_write8_m(sc, URTW_RATE_FALLBACK, URTW_RATE_FALLBACK_ENABLE | 0x1);
	error = urtw_set_rate(sc);
	if (error != 0)
		goto fail;

	error = sc->sc_rf_init(sc);
	if (error != 0)
		goto fail;
	if (sc->sc_rf_set_sens != NULL)
		sc->sc_rf_set_sens(sc, sc->sc_sens);

	/* XXX correct? to call write16  */
	urtw_write16_m(sc, URTW_PSR, 1);
	urtw_write16_m(sc, URTW_ADDR_MAGIC2, 0x10);
	urtw_write8_m(sc, URTW_TALLY_SEL, 0x80);
	urtw_write8_m(sc, URTW_ADDR_MAGIC3, 0x60);
	/* XXX correct? to call write16  */
	urtw_write16_m(sc, URTW_PSR, 0);
	urtw_write8_m(sc, URTW_ADDR_MAGIC1, 4);

	error = urtw_intr_enable(sc);
	if (error != 0)
		goto fail;

fail:
	return (error);
}

static usb_error_t
urtw_set_mode(struct urtw_softc *sc, uint32_t mode)
{
	uint8_t data;
	usb_error_t error;

	urtw_read8_m(sc, URTW_EPROM_CMD, &data);
	data = (data & ~URTW_EPROM_CMD_MASK) | (mode << URTW_EPROM_CMD_SHIFT);
	data = data & ~(URTW_EPROM_CS | URTW_EPROM_CK);
	urtw_write8_m(sc, URTW_EPROM_CMD, data);
fail:
	return (error);
}

static usb_error_t
urtw_8187b_cmd_reset(struct urtw_softc *sc)
{
	int i;
	uint8_t data8;
	usb_error_t error;

	/* XXX the code can be duplicate with urtw_reset().  */
	urtw_read8_m(sc, URTW_CMD, &data8);
	data8 = (data8 & 0x2) | URTW_CMD_RST;
	urtw_write8_m(sc, URTW_CMD, data8);

	for (i = 0; i < 20; i++) {
		usb_pause_mtx(&sc->sc_mtx, 2);
		urtw_read8_m(sc, URTW_CMD, &data8);
		if (!(data8 & URTW_CMD_RST))
			break;
	}
	if (i >= 20) {
		device_printf(sc->sc_dev, "reset timeout\n");
		goto fail;
	}
fail:
	return (error);
}

static usb_error_t
urtw_do_request(struct urtw_softc *sc,
    struct usb_device_request *req, void *data)
{
	usb_error_t err;
	int ntries = 10;

	URTW_ASSERT_LOCKED(sc);

	while (ntries--) {
		err = usbd_do_request_flags(sc->sc_udev, &sc->sc_mtx,
		    req, data, 0, NULL, 250 /* ms */);
		if (err == 0)
			break;

		DPRINTF(sc, URTW_DEBUG_INIT,
		    "Control request failed, %s (retrying)\n",
		    usbd_errstr(err));
		usb_pause_mtx(&sc->sc_mtx, hz / 100);
	}
	return (err);
}

static void
urtw_stop(struct urtw_softc *sc)
{
	uint8_t data8;
	usb_error_t error;

	URTW_ASSERT_LOCKED(sc);

	sc->sc_flags &= ~URTW_RUNNING;

	error = urtw_intr_disable(sc);
	if (error)
		goto fail;
	urtw_read8_m(sc, URTW_CMD, &data8);
	data8 &= ~(URTW_CMD_RX_ENABLE | URTW_CMD_TX_ENABLE);
	urtw_write8_m(sc, URTW_CMD, data8);

	error = sc->sc_rf_stop(sc);
	if (error != 0)
		goto fail;

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;
	urtw_read8_m(sc, URTW_CONFIG4, &data8);
	urtw_write8_m(sc, URTW_CONFIG4, data8 | URTW_CONFIG4_VCOOFF);
	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;
fail:
	if (error)
		device_printf(sc->sc_dev, "failed to stop (%s)\n",
		    usbd_errstr(error));

	usb_callout_stop(&sc->sc_led_ch);
	callout_stop(&sc->sc_watchdog_ch);

	urtw_abort_xfers(sc);
}

static void
urtw_abort_xfers(struct urtw_softc *sc)
{
	int i, max;

	URTW_ASSERT_LOCKED(sc);

	max = (sc->sc_flags & URTW_RTL8187B) ? URTW_8187B_N_XFERS :
	    URTW_8187L_N_XFERS;

	/* abort any pending transfers */
	for (i = 0; i < max; i++)
		usbd_transfer_stop(sc->sc_xfer[i]);
}

static void
urtw_parent(struct ieee80211com *ic)
{
	struct urtw_softc *sc = ic->ic_softc;
	int startall = 0;

	URTW_LOCK(sc);
	if (sc->sc_flags & URTW_DETACHED) {
		URTW_UNLOCK(sc);
		return;
	}

	if (ic->ic_nrunning > 0) {
		if (sc->sc_flags & URTW_RUNNING) {
			if (ic->ic_promisc > 0 || ic->ic_allmulti > 0)
				urtw_set_multi(sc);
		} else {
			urtw_init(sc);
			startall = 1;
		}
	} else if (sc->sc_flags & URTW_RUNNING)
		urtw_stop(sc);
	URTW_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

static int
urtw_transmit(struct ieee80211com *ic, struct mbuf *m)   
{
	struct urtw_softc *sc = ic->ic_softc;
	int error;

	URTW_LOCK(sc);
	if ((sc->sc_flags & URTW_RUNNING) == 0) {
		URTW_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		URTW_UNLOCK(sc);
		return (error);
	}
	urtw_start(sc);
	URTW_UNLOCK(sc);

	return (0);
}

static void
urtw_start(struct urtw_softc *sc)
{
	struct urtw_data *bf;
	struct ieee80211_node *ni;
	struct mbuf *m;

	URTW_ASSERT_LOCKED(sc);

	if ((sc->sc_flags & URTW_RUNNING) == 0)
		return;

	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		bf = urtw_getbuf(sc);
		if (bf == NULL) {
			mbufq_prepend(&sc->sc_snd, m);
			break;
		}

		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		m->m_pkthdr.rcvif = NULL;

		if (urtw_tx_start(sc, ni, m, bf, URTW_PRIORITY_NORMAL) != 0) {
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			STAILQ_INSERT_HEAD(&sc->sc_tx_inactive, bf, next);
			ieee80211_free_node(ni);
			break;
		}

		sc->sc_txtimer = 5;
		callout_reset(&sc->sc_watchdog_ch, hz, urtw_watchdog, sc);
	}
}

static int
urtw_alloc_data_list(struct urtw_softc *sc, struct urtw_data data[],
    int ndata, int maxsz, void *dma_buf)
{
	int i, error;

	for (i = 0; i < ndata; i++) {
		struct urtw_data *dp = &data[i];

		dp->sc = sc;
		if (dma_buf == NULL) {
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
			dp->buf = ((uint8_t *)dma_buf) +
			    (i * maxsz);
		}
		dp->ni = NULL;
	}
	return (0);

fail:	urtw_free_data_list(sc, data, ndata, 1);
	return (error);
}

static int
urtw_alloc_rx_data_list(struct urtw_softc *sc)
{
	int error, i;

	error = urtw_alloc_data_list(sc,
	    sc->sc_rx, URTW_RX_DATA_LIST_COUNT,
	    MCLBYTES, NULL /* mbufs */);
	if (error != 0)
		return (error);

	STAILQ_INIT(&sc->sc_rx_active);
	STAILQ_INIT(&sc->sc_rx_inactive);

	for (i = 0; i < URTW_RX_DATA_LIST_COUNT; i++)
		STAILQ_INSERT_HEAD(&sc->sc_rx_inactive, &sc->sc_rx[i], next);

	return (0);
}

static int
urtw_alloc_tx_data_list(struct urtw_softc *sc)
{
	int error, i;

	error = urtw_alloc_data_list(sc,
	    sc->sc_tx, URTW_TX_DATA_LIST_COUNT, URTW_TX_MAXSIZE,
	    sc->sc_tx_dma_buf /* no mbufs */);
	if (error != 0)
		return (error);

	STAILQ_INIT(&sc->sc_tx_active);
	STAILQ_INIT(&sc->sc_tx_inactive);
	STAILQ_INIT(&sc->sc_tx_pending);

	for (i = 0; i < URTW_TX_DATA_LIST_COUNT; i++)
		STAILQ_INSERT_HEAD(&sc->sc_tx_inactive, &sc->sc_tx[i],
		    next);

	return (0);
}

static int
urtw_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct urtw_softc *sc = ic->ic_softc;
	struct urtw_data *bf;

	/* prevent management frames from being sent if we're not ready */
	if (!(sc->sc_flags & URTW_RUNNING)) {
		m_freem(m);
		return ENETDOWN;
	}
	URTW_LOCK(sc);
	bf = urtw_getbuf(sc);
	if (bf == NULL) {
		m_freem(m);
		URTW_UNLOCK(sc);
		return (ENOBUFS);		/* XXX */
	}

	if (urtw_tx_start(sc, ni, m, bf, URTW_PRIORITY_LOW) != 0) {
		STAILQ_INSERT_HEAD(&sc->sc_tx_inactive, bf, next);
		URTW_UNLOCK(sc);
		return (EIO);
	}
	URTW_UNLOCK(sc);

	sc->sc_txtimer = 5;
	return (0);
}

static void
urtw_scan_start(struct ieee80211com *ic)
{

	/* XXX do nothing?  */
}

static void
urtw_scan_end(struct ieee80211com *ic)
{

	/* XXX do nothing?  */
}

static void
urtw_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	uint8_t bands[IEEE80211_MODE_BYTES];

	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	ieee80211_add_channels_default_2ghz(chans, maxchans, nchans, bands, 0);
}

static void
urtw_set_channel(struct ieee80211com *ic)
{
	struct urtw_softc *sc = ic->ic_softc;
	uint32_t data, orig;
	usb_error_t error;

	/*
	 * if the user set a channel explicitly using ifconfig(8) this function
	 * can be called earlier than we're expected that in some cases the
	 * initialization would be failed if setting a channel is called before
	 * the init have done.
	 */
	if (!(sc->sc_flags & URTW_RUNNING))
		return;

	if (sc->sc_curchan != NULL && sc->sc_curchan == ic->ic_curchan)
		return;

	URTW_LOCK(sc);

	/*
	 * during changing th channel we need to temporarily be disable 
	 * TX.
	 */
	urtw_read32_m(sc, URTW_TX_CONF, &orig);
	data = orig & ~URTW_TX_LOOPBACK_MASK;
	urtw_write32_m(sc, URTW_TX_CONF, data | URTW_TX_LOOPBACK_MAC);

	error = sc->sc_rf_set_chan(sc, ieee80211_chan2ieee(ic, ic->ic_curchan));
	if (error != 0)
		goto fail;
	usb_pause_mtx(&sc->sc_mtx, 10);
	urtw_write32_m(sc, URTW_TX_CONF, orig);

	urtw_write16_m(sc, URTW_ATIM_WND, 2);
	urtw_write16_m(sc, URTW_ATIM_TR_ITV, 100);
	urtw_write16_m(sc, URTW_BEACON_INTERVAL, 100);
	urtw_write16_m(sc, URTW_BEACON_INTERVAL_TIME, 100);

fail:
	URTW_UNLOCK(sc);

	sc->sc_curchan = ic->ic_curchan;

	if (error != 0)
		device_printf(sc->sc_dev, "could not change the channel\n");
}

static void
urtw_update_promisc(struct ieee80211com *ic)
{
	struct urtw_softc *sc = ic->ic_softc;

	URTW_LOCK(sc);
	if (sc->sc_flags & URTW_RUNNING)
		urtw_rx_setconf(sc);
	URTW_UNLOCK(sc);
}

static void
urtw_update_mcast(struct ieee80211com *ic)
{

	/* XXX do nothing?  */
}

static int
urtw_tx_start(struct urtw_softc *sc, struct ieee80211_node *ni, struct mbuf *m0,
    struct urtw_data *data, int prior)
{
	struct ieee80211_frame *wh = mtod(m0, struct ieee80211_frame *);
	struct ieee80211_key *k;
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct usb_xfer *rtl8187b_pipes[URTW_8187B_TXPIPE_MAX] = {
		sc->sc_xfer[URTW_8187B_BULK_TX_BE],
		sc->sc_xfer[URTW_8187B_BULK_TX_BK],
		sc->sc_xfer[URTW_8187B_BULK_TX_VI],
		sc->sc_xfer[URTW_8187B_BULK_TX_VO]
	};
	struct usb_xfer *xfer;
	int dur = 0, rtsdur = 0, rtsenable = 0, ctsenable = 0, rate, type,
	    pkttime = 0, txdur = 0, isshort = 0, xferlen, ismcast;
	uint16_t acktime, rtstime, ctstime;
	uint32_t flags;
	usb_error_t error;

	URTW_ASSERT_LOCKED(sc);

	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	/*
	 * Software crypto.
	 */
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			device_printf(sc->sc_dev,
			    "ieee80211_crypto_encap returns NULL.\n");
			/* XXX we don't expect the fragmented frames  */
			m_freem(m0);
			return (ENOBUFS);
		}

		/* in case packet header moved, reset pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct urtw_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		ieee80211_radiotap_tx(vap, m0);
	}

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
		else
			rate = urtw_rtl2rate(sc->sc_currate);
	}

	sc->sc_stats.txrates[sc->sc_currate]++;

	if (ismcast)
		txdur = pkttime = urtw_compute_txtime(m0->m_pkthdr.len +
		    IEEE80211_CRC_LEN, rate, 0, 0);
	else {
		acktime = urtw_compute_txtime(14, 2,0, 0);
		if ((m0->m_pkthdr.len + 4) > vap->iv_rtsthreshold) {
			rtsenable = 1;
			ctsenable = 0;
			rtstime = urtw_compute_txtime(URTW_ACKCTS_LEN, 2, 0, 0);
			ctstime = urtw_compute_txtime(14, 2, 0, 0);
			pkttime = urtw_compute_txtime(m0->m_pkthdr.len +
			    IEEE80211_CRC_LEN, rate, 0, isshort);
			rtsdur = ctstime + pkttime + acktime +
			    3 * URTW_ASIFS_TIME;
			txdur = rtstime + rtsdur;
		} else {
			rtsenable = ctsenable = rtsdur = 0;
			pkttime = urtw_compute_txtime(m0->m_pkthdr.len +
			    IEEE80211_CRC_LEN, rate, 0, isshort);
			txdur = pkttime + URTW_ASIFS_TIME + acktime;
		}

		if (wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG)
			dur = urtw_compute_txtime(m0->m_pkthdr.len +
			    IEEE80211_CRC_LEN, rate, 0, isshort) +
			    3 * URTW_ASIFS_TIME +
			    2 * acktime;
		else
			dur = URTW_ASIFS_TIME + acktime;
	}
	USETW(wh->i_dur, dur);

	xferlen = m0->m_pkthdr.len;
	xferlen += (sc->sc_flags & URTW_RTL8187B) ? (4 * 8) : (4 * 3);
	if ((0 == xferlen % 64) || (0 == xferlen % 512))
		xferlen += 1;

	memset(data->buf, 0, URTW_TX_MAXSIZE);
	flags = m0->m_pkthdr.len & 0xfff;
	flags |= URTW_TX_FLAG_NO_ENC;
	if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
	    (ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_PREAMBLE) &&
	    (sc->sc_preamble_mode == URTW_PREAMBLE_MODE_SHORT) &&
	    (sc->sc_currate != 0))
		flags |= URTW_TX_FLAG_SPLCP;
	if (wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG)
		flags |= URTW_TX_FLAG_MOREFRAG;

	flags |= (sc->sc_currate & 0xf) << URTW_TX_FLAG_TXRATE_SHIFT;

	if (sc->sc_flags & URTW_RTL8187B) {
		struct urtw_8187b_txhdr *tx;

		tx = (struct urtw_8187b_txhdr *)data->buf;
		if (ctsenable)
			flags |= URTW_TX_FLAG_CTS;
		if (rtsenable) {
			flags |= URTW_TX_FLAG_RTS;
			flags |= URTW_RIDX_CCK5 << URTW_TX_FLAG_RTSRATE_SHIFT;
			tx->rtsdur = rtsdur;
		}
		tx->flag = htole32(flags);
		tx->txdur = txdur;
		if (type == IEEE80211_FC0_TYPE_MGT &&
		    (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
		    IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			tx->retry = 1;
		else
			tx->retry = URTW_TX_MAXRETRY;
		m_copydata(m0, 0, m0->m_pkthdr.len, (uint8_t *)(tx + 1));
	} else {
		struct urtw_8187l_txhdr *tx;

		tx = (struct urtw_8187l_txhdr *)data->buf;
		if (rtsenable) {
			flags |= URTW_TX_FLAG_RTS;
			tx->rtsdur = rtsdur;
		}
		flags |= URTW_RIDX_CCK5 << URTW_TX_FLAG_RTSRATE_SHIFT;
		tx->flag = htole32(flags);
		tx->retry = 3;		/* CW minimum  */
		tx->retry |= 7 << 4;	/* CW maximum  */
		tx->retry |= URTW_TX_MAXRETRY << 8;	/* retry limitation  */
		m_copydata(m0, 0, m0->m_pkthdr.len, (uint8_t *)(tx + 1));
	}

	data->buflen = xferlen;
	data->ni = ni;
	data->m = m0;

	if (sc->sc_flags & URTW_RTL8187B) {
		switch (type) {
		case IEEE80211_FC0_TYPE_CTL:
		case IEEE80211_FC0_TYPE_MGT:
			xfer = sc->sc_xfer[URTW_8187B_BULK_TX_EP12];
			break;
		default:
			KASSERT(M_WME_GETAC(m0) < URTW_8187B_TXPIPE_MAX,
			    ("unsupported WME pipe %d", M_WME_GETAC(m0)));
			xfer = rtl8187b_pipes[M_WME_GETAC(m0)];
			break;
		}
	} else
		xfer = (prior == URTW_PRIORITY_LOW) ?
		    sc->sc_xfer[URTW_8187L_BULK_TX_LOW] :
		    sc->sc_xfer[URTW_8187L_BULK_TX_NORMAL];

	STAILQ_INSERT_TAIL(&sc->sc_tx_pending, data, next);
	usbd_transfer_start(xfer);

	error = urtw_led_ctl(sc, URTW_LED_CTL_TX);
	if (error != 0)
		device_printf(sc->sc_dev, "could not control LED (%d)\n",
		    error);
	return (0);
}

static int
urtw_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct urtw_softc *sc = ic->ic_softc;
	struct urtw_vap *uvp = URTW_VAP(vap);
	struct ieee80211_node *ni;
	usb_error_t error = 0;

	DPRINTF(sc, URTW_DEBUG_STATE, "%s: %s -> %s\n", __func__,
	    ieee80211_state_name[vap->iv_state],
	    ieee80211_state_name[nstate]);

	sc->sc_state = nstate;

	IEEE80211_UNLOCK(ic);
	URTW_LOCK(sc);
	usb_callout_stop(&sc->sc_led_ch);
	callout_stop(&sc->sc_watchdog_ch);

	switch (nstate) {
	case IEEE80211_S_INIT:
	case IEEE80211_S_SCAN:
	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		break;
	case IEEE80211_S_RUN:
		ni = ieee80211_ref_node(vap->iv_bss);
		/* setting bssid.  */
		urtw_write32_m(sc, URTW_BSSID, ((uint32_t *)ni->ni_bssid)[0]);
		urtw_write16_m(sc, URTW_BSSID + 4,
		    ((uint16_t *)ni->ni_bssid)[2]);
		urtw_update_msr(sc);
		/* XXX maybe the below would be incorrect.  */
		urtw_write16_m(sc, URTW_ATIM_WND, 2);
		urtw_write16_m(sc, URTW_ATIM_TR_ITV, 100);
		urtw_write16_m(sc, URTW_BEACON_INTERVAL, 0x64);
		urtw_write16_m(sc, URTW_BEACON_INTERVAL_TIME, 100);
		error = urtw_led_ctl(sc, URTW_LED_CTL_LINK);
		if (error != 0)
			device_printf(sc->sc_dev,
			    "could not control LED (%d)\n", error);
		ieee80211_free_node(ni);
		break;
	default:
		break;
	}
fail:
	URTW_UNLOCK(sc);
	IEEE80211_LOCK(ic);
	return (uvp->newstate(vap, nstate, arg));
}

static void
urtw_watchdog(void *arg)
{
	struct urtw_softc *sc = arg;

	if (sc->sc_txtimer > 0) {
		if (--sc->sc_txtimer == 0) {
			device_printf(sc->sc_dev, "device timeout\n");
			counter_u64_add(sc->sc_ic.ic_oerrors, 1);
			return;
		}
		callout_reset(&sc->sc_watchdog_ch, hz, urtw_watchdog, sc);
	}
}

static void
urtw_set_multi(void *arg)
{
	/* XXX don't know how to set a device.  Lack of docs. */
}

static usb_error_t
urtw_set_rate(struct urtw_softc *sc)
{
	int i, basic_rate, min_rr_rate, max_rr_rate;
	uint16_t data;
	usb_error_t error;

	basic_rate = URTW_RIDX_OFDM24;
	min_rr_rate = URTW_RIDX_OFDM6;
	max_rr_rate = URTW_RIDX_OFDM24;

	urtw_write8_m(sc, URTW_RESP_RATE,
	    max_rr_rate << URTW_RESP_MAX_RATE_SHIFT |
	    min_rr_rate << URTW_RESP_MIN_RATE_SHIFT);

	urtw_read16_m(sc, URTW_BRSR, &data);
	data &= ~URTW_BRSR_MBR_8185;

	for (i = 0; i <= basic_rate; i++)
		data |= (1 << i);

	urtw_write16_m(sc, URTW_BRSR, data);
fail:
	return (error);
}

static uint16_t
urtw_rtl2rate(uint32_t rate)
{
	unsigned int i;

	for (i = 0; i < nitems(urtw_ratetable); i++) {
		if (rate == urtw_ratetable[i].val)
			return urtw_ratetable[i].reg;
	}

	return (0);
}

static usb_error_t
urtw_update_msr(struct urtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t data;
	usb_error_t error;

	urtw_read8_m(sc, URTW_MSR, &data);
	data &= ~URTW_MSR_LINK_MASK;

	if (sc->sc_state == IEEE80211_S_RUN) {
		switch (ic->ic_opmode) {
		case IEEE80211_M_STA:
		case IEEE80211_M_MONITOR:
			data |= URTW_MSR_LINK_STA;
			if (sc->sc_flags & URTW_RTL8187B)
				data |= URTW_MSR_LINK_ENEDCA;
			break;
		case IEEE80211_M_IBSS:
			data |= URTW_MSR_LINK_ADHOC;
			break;
		case IEEE80211_M_HOSTAP:
			data |= URTW_MSR_LINK_HOSTAP;
			break;
		default:
			DPRINTF(sc, URTW_DEBUG_STATE,
			    "unsupported operation mode 0x%x\n",
			    ic->ic_opmode);
			error = USB_ERR_INVAL;
			goto fail;
		}
	} else
		data |= URTW_MSR_LINK_NONE;

	urtw_write8_m(sc, URTW_MSR, data);
fail:
	return (error);
}

static usb_error_t
urtw_read8_c(struct urtw_softc *sc, int val, uint8_t *data)
{
	struct usb_device_request req;
	usb_error_t error;

	URTW_ASSERT_LOCKED(sc);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = URTW_8187_GETREGS_REQ;
	USETW(req.wValue, (val & 0xff) | 0xff00);
	USETW(req.wIndex, (val >> 8) & 0x3);
	USETW(req.wLength, sizeof(uint8_t));

	error = urtw_do_request(sc, &req, data);
	return (error);
}

static usb_error_t
urtw_read16_c(struct urtw_softc *sc, int val, uint16_t *data)
{
	struct usb_device_request req;
	usb_error_t error;

	URTW_ASSERT_LOCKED(sc);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = URTW_8187_GETREGS_REQ;
	USETW(req.wValue, (val & 0xff) | 0xff00);
	USETW(req.wIndex, (val >> 8) & 0x3);
	USETW(req.wLength, sizeof(uint16_t));

	error = urtw_do_request(sc, &req, data);
	return (error);
}

static usb_error_t
urtw_read32_c(struct urtw_softc *sc, int val, uint32_t *data)
{
	struct usb_device_request req;
	usb_error_t error;

	URTW_ASSERT_LOCKED(sc);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = URTW_8187_GETREGS_REQ;
	USETW(req.wValue, (val & 0xff) | 0xff00);
	USETW(req.wIndex, (val >> 8) & 0x3);
	USETW(req.wLength, sizeof(uint32_t));

	error = urtw_do_request(sc, &req, data);
	return (error);
}

static usb_error_t
urtw_write8_c(struct urtw_softc *sc, int val, uint8_t data)
{
	struct usb_device_request req;

	URTW_ASSERT_LOCKED(sc);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URTW_8187_SETREGS_REQ;
	USETW(req.wValue, (val & 0xff) | 0xff00);
	USETW(req.wIndex, (val >> 8) & 0x3);
	USETW(req.wLength, sizeof(uint8_t));

	return (urtw_do_request(sc, &req, &data));
}

static usb_error_t
urtw_write16_c(struct urtw_softc *sc, int val, uint16_t data)
{
	struct usb_device_request req;

	URTW_ASSERT_LOCKED(sc);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URTW_8187_SETREGS_REQ;
	USETW(req.wValue, (val & 0xff) | 0xff00);
	USETW(req.wIndex, (val >> 8) & 0x3);
	USETW(req.wLength, sizeof(uint16_t));

	return (urtw_do_request(sc, &req, &data));
}

static usb_error_t
urtw_write32_c(struct urtw_softc *sc, int val, uint32_t data)
{
	struct usb_device_request req;

	URTW_ASSERT_LOCKED(sc);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URTW_8187_SETREGS_REQ;
	USETW(req.wValue, (val & 0xff) | 0xff00);
	USETW(req.wIndex, (val >> 8) & 0x3);
	USETW(req.wLength, sizeof(uint32_t));

	return (urtw_do_request(sc, &req, &data));
}

static usb_error_t
urtw_get_macaddr(struct urtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t data;
	usb_error_t error;

	error = urtw_eprom_read32(sc, URTW_EPROM_MACADDR, &data);
	if (error != 0)
		goto fail;
	ic->ic_macaddr[0] = data & 0xff;
	ic->ic_macaddr[1] = (data & 0xff00) >> 8;
	error = urtw_eprom_read32(sc, URTW_EPROM_MACADDR + 1, &data);
	if (error != 0)
		goto fail;
	ic->ic_macaddr[2] = data & 0xff;
	ic->ic_macaddr[3] = (data & 0xff00) >> 8;
	error = urtw_eprom_read32(sc, URTW_EPROM_MACADDR + 2, &data);
	if (error != 0)
		goto fail;
	ic->ic_macaddr[4] = data & 0xff;
	ic->ic_macaddr[5] = (data & 0xff00) >> 8;
fail:
	return (error);
}

static usb_error_t
urtw_eprom_read32(struct urtw_softc *sc, uint32_t addr, uint32_t *data)
{
#define URTW_READCMD_LEN		3
	int addrlen, i;
	int16_t addrstr[8], data16, readcmd[] = { 1, 1, 0 };
	usb_error_t error;

	/* NB: make sure the buffer is initialized  */
	*data = 0;

	/* enable EPROM programming */
	urtw_write8_m(sc, URTW_EPROM_CMD, URTW_EPROM_CMD_PROGRAM_MODE);
	DELAY(URTW_EPROM_DELAY);

	error = urtw_eprom_cs(sc, URTW_EPROM_ENABLE);
	if (error != 0)
		goto fail;
	error = urtw_eprom_ck(sc);
	if (error != 0)
		goto fail;
	error = urtw_eprom_sendbits(sc, readcmd, URTW_READCMD_LEN);
	if (error != 0)
		goto fail;
	if (sc->sc_epromtype == URTW_EEPROM_93C56) {
		addrlen = 8;
		addrstr[0] = addr & (1 << 7);
		addrstr[1] = addr & (1 << 6);
		addrstr[2] = addr & (1 << 5);
		addrstr[3] = addr & (1 << 4);
		addrstr[4] = addr & (1 << 3);
		addrstr[5] = addr & (1 << 2);
		addrstr[6] = addr & (1 << 1);
		addrstr[7] = addr & (1 << 0);
	} else {
		addrlen=6;
		addrstr[0] = addr & (1 << 5);
		addrstr[1] = addr & (1 << 4);
		addrstr[2] = addr & (1 << 3);
		addrstr[3] = addr & (1 << 2);
		addrstr[4] = addr & (1 << 1);
		addrstr[5] = addr & (1 << 0);
	}
	error = urtw_eprom_sendbits(sc, addrstr, addrlen);
	if (error != 0)
		goto fail;

	error = urtw_eprom_writebit(sc, 0);
	if (error != 0)
		goto fail;

	for (i = 0; i < 16; i++) {
		error = urtw_eprom_ck(sc);
		if (error != 0)
			goto fail;
		error = urtw_eprom_readbit(sc, &data16);
		if (error != 0)
			goto fail;

		(*data) |= (data16 << (15 - i));
	}

	error = urtw_eprom_cs(sc, URTW_EPROM_DISABLE);
	if (error != 0)
		goto fail;
	error = urtw_eprom_ck(sc);
	if (error != 0)
		goto fail;

	/* now disable EPROM programming */
	urtw_write8_m(sc, URTW_EPROM_CMD, URTW_EPROM_CMD_NORMAL_MODE);
fail:
	return (error);
#undef URTW_READCMD_LEN
}

static usb_error_t
urtw_eprom_cs(struct urtw_softc *sc, int able)
{
	uint8_t data;
	usb_error_t error;

	urtw_read8_m(sc, URTW_EPROM_CMD, &data);
	if (able == URTW_EPROM_ENABLE)
		urtw_write8_m(sc, URTW_EPROM_CMD, data | URTW_EPROM_CS);
	else
		urtw_write8_m(sc, URTW_EPROM_CMD, data & ~URTW_EPROM_CS);
	DELAY(URTW_EPROM_DELAY);
fail:
	return (error);
}

static usb_error_t
urtw_eprom_ck(struct urtw_softc *sc)
{
	uint8_t data;
	usb_error_t error;

	/* masking  */
	urtw_read8_m(sc, URTW_EPROM_CMD, &data);
	urtw_write8_m(sc, URTW_EPROM_CMD, data | URTW_EPROM_CK);
	DELAY(URTW_EPROM_DELAY);
	/* unmasking  */
	urtw_read8_m(sc, URTW_EPROM_CMD, &data);
	urtw_write8_m(sc, URTW_EPROM_CMD, data & ~URTW_EPROM_CK);
	DELAY(URTW_EPROM_DELAY);
fail:
	return (error);
}

static usb_error_t
urtw_eprom_readbit(struct urtw_softc *sc, int16_t *data)
{
	uint8_t data8;
	usb_error_t error;

	urtw_read8_m(sc, URTW_EPROM_CMD, &data8);
	*data = (data8 & URTW_EPROM_READBIT) ? 1 : 0;
	DELAY(URTW_EPROM_DELAY);

fail:
	return (error);
}

static usb_error_t
urtw_eprom_writebit(struct urtw_softc *sc, int16_t bit)
{
	uint8_t data;
	usb_error_t error;

	urtw_read8_m(sc, URTW_EPROM_CMD, &data);
	if (bit != 0)
		urtw_write8_m(sc, URTW_EPROM_CMD, data | URTW_EPROM_WRITEBIT);
	else
		urtw_write8_m(sc, URTW_EPROM_CMD, data & ~URTW_EPROM_WRITEBIT);
	DELAY(URTW_EPROM_DELAY);
fail:
	return (error);
}

static usb_error_t
urtw_eprom_sendbits(struct urtw_softc *sc, int16_t *buf, int buflen)
{
	int i = 0;
	usb_error_t error = 0;

	for (i = 0; i < buflen; i++) {
		error = urtw_eprom_writebit(sc, buf[i]);
		if (error != 0)
			goto fail;
		error = urtw_eprom_ck(sc);
		if (error != 0)
			goto fail;
	}
fail:
	return (error);
}


static usb_error_t
urtw_get_txpwr(struct urtw_softc *sc)
{
	int i, j;
	uint32_t data;
	usb_error_t error;

	error = urtw_eprom_read32(sc, URTW_EPROM_TXPW_BASE, &data);
	if (error != 0)
		goto fail;
	sc->sc_txpwr_cck_base = data & 0xf;
	sc->sc_txpwr_ofdm_base = (data >> 4) & 0xf;

	for (i = 1, j = 0; i < 6; i += 2, j++) {
		error = urtw_eprom_read32(sc, URTW_EPROM_TXPW0 + j, &data);
		if (error != 0)
			goto fail;
		sc->sc_txpwr_cck[i] = data & 0xf;
		sc->sc_txpwr_cck[i + 1] = (data & 0xf00) >> 8;
		sc->sc_txpwr_ofdm[i] = (data & 0xf0) >> 4;
		sc->sc_txpwr_ofdm[i + 1] = (data & 0xf000) >> 12;
	}
	for (i = 1, j = 0; i < 4; i += 2, j++) {
		error = urtw_eprom_read32(sc, URTW_EPROM_TXPW1 + j, &data);
		if (error != 0)
			goto fail;
		sc->sc_txpwr_cck[i + 6] = data & 0xf;
		sc->sc_txpwr_cck[i + 6 + 1] = (data & 0xf00) >> 8;
		sc->sc_txpwr_ofdm[i + 6] = (data & 0xf0) >> 4;
		sc->sc_txpwr_ofdm[i + 6 + 1] = (data & 0xf000) >> 12;
	}
	if (sc->sc_flags & URTW_RTL8187B) {
		error = urtw_eprom_read32(sc, URTW_EPROM_TXPW2, &data);
		if (error != 0)
			goto fail;
		sc->sc_txpwr_cck[1 + 6 + 4] = data & 0xf;
		sc->sc_txpwr_ofdm[1 + 6 + 4] = (data & 0xf0) >> 4;
		error = urtw_eprom_read32(sc, 0x0a, &data);
		if (error != 0)
			goto fail;
		sc->sc_txpwr_cck[2 + 6 + 4] = data & 0xf;
		sc->sc_txpwr_ofdm[2 + 6 + 4] = (data & 0xf0) >> 4;
		error = urtw_eprom_read32(sc, 0x1c, &data);
		if (error != 0)
			goto fail;
		sc->sc_txpwr_cck[3 + 6 + 4] = data & 0xf;
		sc->sc_txpwr_cck[3 + 6 + 4 + 1] = (data & 0xf00) >> 8;
		sc->sc_txpwr_ofdm[3 + 6 + 4] = (data & 0xf0) >> 4;
		sc->sc_txpwr_ofdm[3 + 6 + 4 + 1] = (data & 0xf000) >> 12;
	} else {
		for (i = 1, j = 0; i < 4; i += 2, j++) {
			error = urtw_eprom_read32(sc, URTW_EPROM_TXPW2 + j,
			    &data);
			if (error != 0)
				goto fail;
			sc->sc_txpwr_cck[i + 6 + 4] = data & 0xf;
			sc->sc_txpwr_cck[i + 6 + 4 + 1] = (data & 0xf00) >> 8;
			sc->sc_txpwr_ofdm[i + 6 + 4] = (data & 0xf0) >> 4;
			sc->sc_txpwr_ofdm[i + 6 + 4 + 1] = (data & 0xf000) >> 12;
		}
	}
fail:
	return (error);
}


static usb_error_t
urtw_get_rfchip(struct urtw_softc *sc)
{
	int ret;
	uint8_t data8;
	uint32_t data;
	usb_error_t error;

	if (sc->sc_flags & URTW_RTL8187B) {
		urtw_read8_m(sc, 0xe1, &data8);
		switch (data8) {
		case 0:
			sc->sc_flags |= URTW_RTL8187B_REV_B;
			break;
		case 1:
			sc->sc_flags |= URTW_RTL8187B_REV_D;
			break;
		case 2:
			sc->sc_flags |= URTW_RTL8187B_REV_E;
			break;
		default:
			device_printf(sc->sc_dev, "unknown type: %#x\n", data8);
			sc->sc_flags |= URTW_RTL8187B_REV_B;
			break;
		}
	} else {
		urtw_read32_m(sc, URTW_TX_CONF, &data);
		switch (data & URTW_TX_HWMASK) {
		case URTW_TX_R8187vD_B:
			sc->sc_flags |= URTW_RTL8187B;
			break;
		case URTW_TX_R8187vD:
			break;
		default:
			device_printf(sc->sc_dev, "unknown RTL8187L type: %#x\n",
			    data & URTW_TX_HWMASK);
			break;
		}
	}

	error = urtw_eprom_read32(sc, URTW_EPROM_RFCHIPID, &data);
	if (error != 0)
		goto fail;
	switch (data & 0xff) {
	case URTW_EPROM_RFCHIPID_RTL8225U:
		error = urtw_8225_isv2(sc, &ret);
		if (error != 0)
			goto fail;
		if (ret == 0) {
			sc->sc_rf_init = urtw_8225_rf_init;
			sc->sc_rf_set_sens = urtw_8225_rf_set_sens;
			sc->sc_rf_set_chan = urtw_8225_rf_set_chan;
			sc->sc_rf_stop = urtw_8225_rf_stop;
		} else {
			sc->sc_rf_init = urtw_8225v2_rf_init;
			sc->sc_rf_set_chan = urtw_8225v2_rf_set_chan;
			sc->sc_rf_stop = urtw_8225_rf_stop;
		}
		sc->sc_max_sens = URTW_8225_RF_MAX_SENS;
		sc->sc_sens = URTW_8225_RF_DEF_SENS;
		break;
	case URTW_EPROM_RFCHIPID_RTL8225Z2:
		sc->sc_rf_init = urtw_8225v2b_rf_init;
		sc->sc_rf_set_chan = urtw_8225v2b_rf_set_chan;
		sc->sc_max_sens = URTW_8225_RF_MAX_SENS;
		sc->sc_sens = URTW_8225_RF_DEF_SENS;
		sc->sc_rf_stop = urtw_8225_rf_stop;
		break;
	default:
		DPRINTF(sc, URTW_DEBUG_STATE,
		    "unsupported RF chip %d\n", data & 0xff);
		error = USB_ERR_INVAL;
		goto fail;
	}

	device_printf(sc->sc_dev, "%s rf %s hwrev %s\n",
	    (sc->sc_flags & URTW_RTL8187B) ? "rtl8187b" : "rtl8187l",
	    ((data & 0xff) == URTW_EPROM_RFCHIPID_RTL8225U) ? "rtl8225u" :
	    "rtl8225z2",
	    (sc->sc_flags & URTW_RTL8187B) ? ((data8 == 0) ? "b" :
		(data8 == 1) ? "d" : "e") : "none");

fail:
	return (error);
}


static usb_error_t
urtw_led_init(struct urtw_softc *sc)
{
	uint32_t rev;
	usb_error_t error;

	urtw_read8_m(sc, URTW_PSR, &sc->sc_psr);
	error = urtw_eprom_read32(sc, URTW_EPROM_SWREV, &rev);
	if (error != 0)
		goto fail;

	switch (rev & URTW_EPROM_CID_MASK) {
	case URTW_EPROM_CID_ALPHA0:
		sc->sc_strategy = URTW_SW_LED_MODE1;
		break;
	case URTW_EPROM_CID_SERCOMM_PS:
		sc->sc_strategy = URTW_SW_LED_MODE3;
		break;
	case URTW_EPROM_CID_HW_LED:
		sc->sc_strategy = URTW_HW_LED;
		break;
	case URTW_EPROM_CID_RSVD0:
	case URTW_EPROM_CID_RSVD1:
	default:
		sc->sc_strategy = URTW_SW_LED_MODE0;
		break;
	}

	sc->sc_gpio_ledpin = URTW_LED_PIN_GPIO0;

fail:
	return (error);
}


static usb_error_t
urtw_8225_rf_init(struct urtw_softc *sc)
{
	unsigned int i;
	uint16_t data;
	usb_error_t error;

	error = urtw_8180_set_anaparam(sc, URTW_8225_ANAPARAM_ON);
	if (error)
		goto fail;

	error = urtw_8225_usb_init(sc);
	if (error)
		goto fail;

	urtw_write32_m(sc, URTW_RF_TIMING, 0x000a8008);
	urtw_read16_m(sc, URTW_BRSR, &data);		/* XXX ??? */
	urtw_write16_m(sc, URTW_BRSR, 0xffff);
	urtw_write32_m(sc, URTW_RF_PARA, 0x100044);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;
	urtw_write8_m(sc, URTW_CONFIG3, 0x44);
	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	error = urtw_8185_rf_pins_enable(sc);
	if (error)
		goto fail;
	usb_pause_mtx(&sc->sc_mtx, 1000);

	for (i = 0; i < nitems(urtw_8225_rf_part1); i++) {
		urtw_8225_write(sc, urtw_8225_rf_part1[i].reg,
		    urtw_8225_rf_part1[i].val);
		usb_pause_mtx(&sc->sc_mtx, 1);
	}
	usb_pause_mtx(&sc->sc_mtx, 100);
	urtw_8225_write(sc,
	    URTW_8225_ADDR_2_MAGIC, URTW_8225_ADDR_2_DATA_MAGIC1);
	usb_pause_mtx(&sc->sc_mtx, 200);
	urtw_8225_write(sc,
	    URTW_8225_ADDR_2_MAGIC, URTW_8225_ADDR_2_DATA_MAGIC2);
	usb_pause_mtx(&sc->sc_mtx, 200);
	urtw_8225_write(sc,
	    URTW_8225_ADDR_0_MAGIC, URTW_8225_ADDR_0_DATA_MAGIC3);

	for (i = 0; i < 95; i++) {
		urtw_8225_write(sc, URTW_8225_ADDR_1_MAGIC, (uint8_t)(i + 1));
		urtw_8225_write(sc, URTW_8225_ADDR_2_MAGIC, urtw_8225_rxgain[i]);
	}

	urtw_8225_write(sc,
	    URTW_8225_ADDR_0_MAGIC, URTW_8225_ADDR_0_DATA_MAGIC4);
	urtw_8225_write(sc,
	    URTW_8225_ADDR_0_MAGIC, URTW_8225_ADDR_0_DATA_MAGIC5);

	for (i = 0; i < 128; i++) {
		urtw_8187_write_phy_ofdm(sc, 0xb, urtw_8225_agc[i]);
		usb_pause_mtx(&sc->sc_mtx, 1);
		urtw_8187_write_phy_ofdm(sc, 0xa, (uint8_t)i + 0x80);
		usb_pause_mtx(&sc->sc_mtx, 1);
	}

	for (i = 0; i < nitems(urtw_8225_rf_part2); i++) {
		urtw_8187_write_phy_ofdm(sc, urtw_8225_rf_part2[i].reg,
		    urtw_8225_rf_part2[i].val);
		usb_pause_mtx(&sc->sc_mtx, 1);
	}

	error = urtw_8225_setgain(sc, 4);
	if (error)
		goto fail;

	for (i = 0; i < nitems(urtw_8225_rf_part3); i++) {
		urtw_8187_write_phy_cck(sc, urtw_8225_rf_part3[i].reg,
		    urtw_8225_rf_part3[i].val);
		usb_pause_mtx(&sc->sc_mtx, 1);
	}

	urtw_write8_m(sc, URTW_TESTR, 0x0d);

	error = urtw_8225_set_txpwrlvl(sc, 1);
	if (error)
		goto fail;

	urtw_8187_write_phy_cck(sc, 0x10, 0x9b);
	usb_pause_mtx(&sc->sc_mtx, 1);
	urtw_8187_write_phy_ofdm(sc, 0x26, 0x90);
	usb_pause_mtx(&sc->sc_mtx, 1);

	/* TX ant A, 0x0 for B */
	error = urtw_8185_tx_antenna(sc, 0x3);
	if (error)
		goto fail;
	urtw_write32_m(sc, URTW_HSSI_PARA, 0x3dc00002);

	error = urtw_8225_rf_set_chan(sc, 1);
fail:
	return (error);
}

static usb_error_t
urtw_8185_rf_pins_enable(struct urtw_softc *sc)
{
	usb_error_t error = 0;

	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, 0x1ff7);
fail:
	return (error);
}

static usb_error_t
urtw_8185_tx_antenna(struct urtw_softc *sc, uint8_t ant)
{
	usb_error_t error;

	urtw_write8_m(sc, URTW_TX_ANTENNA, ant);
	usb_pause_mtx(&sc->sc_mtx, 1);
fail:
	return (error);
}

static usb_error_t
urtw_8187_write_phy_ofdm_c(struct urtw_softc *sc, uint8_t addr, uint32_t data)
{

	data = data & 0xff;
	return urtw_8187_write_phy(sc, addr, data);
}

static usb_error_t
urtw_8187_write_phy_cck_c(struct urtw_softc *sc, uint8_t addr, uint32_t data)
{

	data = data & 0xff;
	return urtw_8187_write_phy(sc, addr, data | 0x10000);
}

static usb_error_t
urtw_8187_write_phy(struct urtw_softc *sc, uint8_t addr, uint32_t data)
{
	uint32_t phyw;
	usb_error_t error;

	phyw = ((data << 8) | (addr | 0x80));
	urtw_write8_m(sc, URTW_PHY_MAGIC4, ((phyw & 0xff000000) >> 24));
	urtw_write8_m(sc, URTW_PHY_MAGIC3, ((phyw & 0x00ff0000) >> 16));
	urtw_write8_m(sc, URTW_PHY_MAGIC2, ((phyw & 0x0000ff00) >> 8));
	urtw_write8_m(sc, URTW_PHY_MAGIC1, ((phyw & 0x000000ff)));
	usb_pause_mtx(&sc->sc_mtx, 1);
fail:
	return (error);
}

static usb_error_t
urtw_8225_setgain(struct urtw_softc *sc, int16_t gain)
{
	usb_error_t error;

	urtw_8187_write_phy_ofdm(sc, 0x0d, urtw_8225_gain[gain * 4]);
	urtw_8187_write_phy_ofdm(sc, 0x1b, urtw_8225_gain[gain * 4 + 2]);
	urtw_8187_write_phy_ofdm(sc, 0x1d, urtw_8225_gain[gain * 4 + 3]);
	urtw_8187_write_phy_ofdm(sc, 0x23, urtw_8225_gain[gain * 4 + 1]);
fail:
	return (error);
}

static usb_error_t
urtw_8225_usb_init(struct urtw_softc *sc)
{
	uint8_t data;
	usb_error_t error;

	urtw_write8_m(sc, URTW_RF_PINS_SELECT + 1, 0);
	urtw_write8_m(sc, URTW_GPIO, 0);
	error = urtw_read8e(sc, 0x53, &data);
	if (error)
		goto fail;
	error = urtw_write8e(sc, 0x53, data | (1 << 7));
	if (error)
		goto fail;
	urtw_write8_m(sc, URTW_RF_PINS_SELECT + 1, 4);
	urtw_write8_m(sc, URTW_GPIO, 0x20);
	urtw_write8_m(sc, URTW_GP_ENABLE, 0);

	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, 0x80);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, 0x80);
	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, 0x80);

	usb_pause_mtx(&sc->sc_mtx, 500);
fail:
	return (error);
}

static usb_error_t
urtw_8225_write_c(struct urtw_softc *sc, uint8_t addr, uint16_t data)
{
	uint16_t d80, d82, d84;
	usb_error_t error;

	urtw_read16_m(sc, URTW_RF_PINS_OUTPUT, &d80);
	d80 &= URTW_RF_PINS_MAGIC1;
	urtw_read16_m(sc, URTW_RF_PINS_ENABLE, &d82);
	urtw_read16_m(sc, URTW_RF_PINS_SELECT, &d84);
	d84 &= URTW_RF_PINS_MAGIC2;
	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, d82 | URTW_RF_PINS_MAGIC3);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, d84 | URTW_RF_PINS_MAGIC3);
	DELAY(10);

	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, d80 | URTW_BB_HOST_BANG_EN);
	DELAY(2);
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, d80);
	DELAY(10);

	error = urtw_8225_write_s16(sc, addr, 0x8225, &data);
	if (error != 0)
		goto fail;

	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, d80 | URTW_BB_HOST_BANG_EN);
	DELAY(10);
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, d80 | URTW_BB_HOST_BANG_EN);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, d84);
	usb_pause_mtx(&sc->sc_mtx, 2);
fail:
	return (error);
}

static usb_error_t
urtw_8225_write_s16(struct urtw_softc *sc, uint8_t addr, int index,
    uint16_t *data)
{
	uint8_t buf[2];
	uint16_t data16;
	struct usb_device_request req;
	usb_error_t error = 0;

	data16 = *data;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URTW_8187_SETREGS_REQ;
	USETW(req.wValue, addr);
	USETW(req.wIndex, index);
	USETW(req.wLength, sizeof(uint16_t));
	buf[0] = (data16 & 0x00ff);
	buf[1] = (data16 & 0xff00) >> 8;

	error = urtw_do_request(sc, &req, buf);

	return (error);
}

static usb_error_t
urtw_8225_rf_set_chan(struct urtw_softc *sc, int chan)
{
	usb_error_t error;

	error = urtw_8225_set_txpwrlvl(sc, chan);
	if (error)
		goto fail;
	urtw_8225_write(sc, URTW_8225_ADDR_7_MAGIC, urtw_8225_channel[chan]);
	usb_pause_mtx(&sc->sc_mtx, 10);
fail:
	return (error);
}

static usb_error_t
urtw_8225_rf_set_sens(struct urtw_softc *sc, int sens)
{
	usb_error_t error;

	if (sens < 0 || sens > 6)
		return -1;

	if (sens > 4)
		urtw_8225_write(sc,
		    URTW_8225_ADDR_C_MAGIC, URTW_8225_ADDR_C_DATA_MAGIC1);
	else
		urtw_8225_write(sc,
		    URTW_8225_ADDR_C_MAGIC, URTW_8225_ADDR_C_DATA_MAGIC2);

	sens = 6 - sens;
	error = urtw_8225_setgain(sc, sens);
	if (error)
		goto fail;

	urtw_8187_write_phy_cck(sc, 0x41, urtw_8225_threshold[sens]);

fail:
	return (error);
}

static usb_error_t
urtw_8225_set_txpwrlvl(struct urtw_softc *sc, int chan)
{
	int i, idx, set;
	uint8_t *cck_pwltable;
	uint8_t cck_pwrlvl_max, ofdm_pwrlvl_min, ofdm_pwrlvl_max;
	uint8_t cck_pwrlvl = sc->sc_txpwr_cck[chan] & 0xff;
	uint8_t ofdm_pwrlvl = sc->sc_txpwr_ofdm[chan] & 0xff;
	usb_error_t error;

	cck_pwrlvl_max = 11;
	ofdm_pwrlvl_max = 25;	/* 12 -> 25  */
	ofdm_pwrlvl_min = 10;

	/* CCK power setting */
	cck_pwrlvl = (cck_pwrlvl > cck_pwrlvl_max) ? cck_pwrlvl_max : cck_pwrlvl;
	idx = cck_pwrlvl % 6;
	set = cck_pwrlvl / 6;
	cck_pwltable = (chan == 14) ? urtw_8225_txpwr_cck_ch14 :
	    urtw_8225_txpwr_cck;

	urtw_write8_m(sc, URTW_TX_GAIN_CCK,
	    urtw_8225_tx_gain_cck_ofdm[set] >> 1);
	for (i = 0; i < 8; i++) {
		urtw_8187_write_phy_cck(sc, 0x44 + i,
		    cck_pwltable[idx * 8 + i]);
	}
	usb_pause_mtx(&sc->sc_mtx, 1);

	/* OFDM power setting */
	ofdm_pwrlvl = (ofdm_pwrlvl > (ofdm_pwrlvl_max - ofdm_pwrlvl_min)) ?
	    ofdm_pwrlvl_max : ofdm_pwrlvl + ofdm_pwrlvl_min;
	ofdm_pwrlvl = (ofdm_pwrlvl > 35) ? 35 : ofdm_pwrlvl;

	idx = ofdm_pwrlvl % 6;
	set = ofdm_pwrlvl / 6;

	error = urtw_8185_set_anaparam2(sc, URTW_8225_ANAPARAM2_ON);
	if (error)
		goto fail;
	urtw_8187_write_phy_ofdm(sc, 2, 0x42);
	urtw_8187_write_phy_ofdm(sc, 6, 0);
	urtw_8187_write_phy_ofdm(sc, 8, 0);

	urtw_write8_m(sc, URTW_TX_GAIN_OFDM,
	    urtw_8225_tx_gain_cck_ofdm[set] >> 1);
	urtw_8187_write_phy_ofdm(sc, 0x5, urtw_8225_txpwr_ofdm[idx]);
	urtw_8187_write_phy_ofdm(sc, 0x7, urtw_8225_txpwr_ofdm[idx]);
	usb_pause_mtx(&sc->sc_mtx, 1);
fail:
	return (error);
}


static usb_error_t
urtw_8225_rf_stop(struct urtw_softc *sc)
{
	uint8_t data;
	usb_error_t error;

	urtw_8225_write(sc, 0x4, 0x1f);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;

	urtw_read8_m(sc, URTW_CONFIG3, &data);
	urtw_write8_m(sc, URTW_CONFIG3, data | URTW_CONFIG3_ANAPARAM_WRITE);
	if (sc->sc_flags & URTW_RTL8187B) {
		urtw_write32_m(sc, URTW_ANAPARAM2,
		    URTW_8187B_8225_ANAPARAM2_OFF);
		urtw_write32_m(sc, URTW_ANAPARAM, URTW_8187B_8225_ANAPARAM_OFF);
		urtw_write32_m(sc, URTW_ANAPARAM3,
		    URTW_8187B_8225_ANAPARAM3_OFF);
	} else {
		urtw_write32_m(sc, URTW_ANAPARAM2, URTW_8225_ANAPARAM2_OFF);
		urtw_write32_m(sc, URTW_ANAPARAM, URTW_8225_ANAPARAM_OFF);
	}

	urtw_write8_m(sc, URTW_CONFIG3, data & ~URTW_CONFIG3_ANAPARAM_WRITE);
	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

fail:
	return (error);
}

static usb_error_t
urtw_8225v2_rf_init(struct urtw_softc *sc)
{
	unsigned int i;
	uint16_t data;
	uint32_t data32;
	usb_error_t error;

	error = urtw_8180_set_anaparam(sc, URTW_8225_ANAPARAM_ON);
	if (error)
		goto fail;

	error = urtw_8225_usb_init(sc);
	if (error)
		goto fail;

	urtw_write32_m(sc, URTW_RF_TIMING, 0x000a8008);
	urtw_read16_m(sc, URTW_BRSR, &data);		/* XXX ??? */
	urtw_write16_m(sc, URTW_BRSR, 0xffff);
	urtw_write32_m(sc, URTW_RF_PARA, 0x100044);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;
	urtw_write8_m(sc, URTW_CONFIG3, 0x44);
	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	error = urtw_8185_rf_pins_enable(sc);
	if (error)
		goto fail;

	usb_pause_mtx(&sc->sc_mtx, 500);

	for (i = 0; i < nitems(urtw_8225v2_rf_part1); i++) {
		urtw_8225_write(sc, urtw_8225v2_rf_part1[i].reg,
		    urtw_8225v2_rf_part1[i].val);
	}
	usb_pause_mtx(&sc->sc_mtx, 50);

	urtw_8225_write(sc,
	    URTW_8225_ADDR_0_MAGIC, URTW_8225_ADDR_0_DATA_MAGIC1);

	for (i = 0; i < 95; i++) {
		urtw_8225_write(sc, URTW_8225_ADDR_1_MAGIC, (uint8_t)(i + 1));
		urtw_8225_write(sc, URTW_8225_ADDR_2_MAGIC,
		    urtw_8225v2_rxgain[i]);
	}

	urtw_8225_write(sc,
	    URTW_8225_ADDR_3_MAGIC, URTW_8225_ADDR_3_DATA_MAGIC1);
	urtw_8225_write(sc,
	    URTW_8225_ADDR_5_MAGIC, URTW_8225_ADDR_5_DATA_MAGIC1);
	urtw_8225_write(sc,
	    URTW_8225_ADDR_0_MAGIC, URTW_8225_ADDR_0_DATA_MAGIC2);
	urtw_8225_write(sc,
	    URTW_8225_ADDR_2_MAGIC, URTW_8225_ADDR_2_DATA_MAGIC1);
	usb_pause_mtx(&sc->sc_mtx, 100);
	urtw_8225_write(sc,
	    URTW_8225_ADDR_2_MAGIC, URTW_8225_ADDR_2_DATA_MAGIC2);
	usb_pause_mtx(&sc->sc_mtx, 100);

	error = urtw_8225_read(sc, URTW_8225_ADDR_6_MAGIC, &data32);
	if (error != 0)
		goto fail;
	if (data32 != URTW_8225_ADDR_6_DATA_MAGIC1)
		device_printf(sc->sc_dev, "expect 0xe6!! (0x%x)\n", data32);
	if (!(data32 & URTW_8225_ADDR_6_DATA_MAGIC2)) {
		urtw_8225_write(sc,
		    URTW_8225_ADDR_2_MAGIC, URTW_8225_ADDR_2_DATA_MAGIC1);
		usb_pause_mtx(&sc->sc_mtx, 100);
		urtw_8225_write(sc,
		    URTW_8225_ADDR_2_MAGIC, URTW_8225_ADDR_2_DATA_MAGIC2);
		usb_pause_mtx(&sc->sc_mtx, 50);
		error = urtw_8225_read(sc, URTW_8225_ADDR_6_MAGIC, &data32);
		if (error != 0)
			goto fail;
		if (!(data32 & URTW_8225_ADDR_6_DATA_MAGIC2))
			device_printf(sc->sc_dev, "RF calibration failed\n");
	}
	usb_pause_mtx(&sc->sc_mtx, 100);

	urtw_8225_write(sc,
	    URTW_8225_ADDR_0_MAGIC, URTW_8225_ADDR_0_DATA_MAGIC6);
	for (i = 0; i < 128; i++) {
		urtw_8187_write_phy_ofdm(sc, 0xb, urtw_8225_agc[i]);
		urtw_8187_write_phy_ofdm(sc, 0xa, (uint8_t)i + 0x80);
	}

	for (i = 0; i < nitems(urtw_8225v2_rf_part2); i++) {
		urtw_8187_write_phy_ofdm(sc, urtw_8225v2_rf_part2[i].reg,
		    urtw_8225v2_rf_part2[i].val);
	}

	error = urtw_8225v2_setgain(sc, 4);
	if (error)
		goto fail;

	for (i = 0; i < nitems(urtw_8225v2_rf_part3); i++) {
		urtw_8187_write_phy_cck(sc, urtw_8225v2_rf_part3[i].reg,
		    urtw_8225v2_rf_part3[i].val);
	}

	urtw_write8_m(sc, URTW_TESTR, 0x0d);

	error = urtw_8225v2_set_txpwrlvl(sc, 1);
	if (error)
		goto fail;

	urtw_8187_write_phy_cck(sc, 0x10, 0x9b);
	urtw_8187_write_phy_ofdm(sc, 0x26, 0x90);

	/* TX ant A, 0x0 for B */
	error = urtw_8185_tx_antenna(sc, 0x3);
	if (error)
		goto fail;
	urtw_write32_m(sc, URTW_HSSI_PARA, 0x3dc00002);

	error = urtw_8225_rf_set_chan(sc, 1);
fail:
	return (error);
}

static usb_error_t
urtw_8225v2_rf_set_chan(struct urtw_softc *sc, int chan)
{
	usb_error_t error;

	error = urtw_8225v2_set_txpwrlvl(sc, chan);
	if (error)
		goto fail;

	urtw_8225_write(sc, URTW_8225_ADDR_7_MAGIC, urtw_8225_channel[chan]);
	usb_pause_mtx(&sc->sc_mtx, 10);
fail:
	return (error);
}

static usb_error_t
urtw_8225_read(struct urtw_softc *sc, uint8_t addr, uint32_t *data)
{
	int i;
	int16_t bit;
	uint8_t rlen = 12, wlen = 6;
	uint16_t o1, o2, o3, tmp;
	uint32_t d2w = ((uint32_t)(addr & 0x1f)) << 27;
	uint32_t mask = 0x80000000, value = 0;
	usb_error_t error;

	urtw_read16_m(sc, URTW_RF_PINS_OUTPUT, &o1);
	urtw_read16_m(sc, URTW_RF_PINS_ENABLE, &o2);
	urtw_read16_m(sc, URTW_RF_PINS_SELECT, &o3);
	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, o2 | URTW_RF_PINS_MAGIC4);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, o3 | URTW_RF_PINS_MAGIC4);
	o1 &= ~URTW_RF_PINS_MAGIC4;
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, o1 | URTW_BB_HOST_BANG_EN);
	DELAY(5);
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, o1);
	DELAY(5);

	for (i = 0; i < (wlen / 2); i++, mask = mask >> 1) {
		bit = ((d2w & mask) != 0) ? 1 : 0;

		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, bit | o1);
		DELAY(2);
		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, bit | o1 |
		    URTW_BB_HOST_BANG_CLK);
		DELAY(2);
		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, bit | o1 |
		    URTW_BB_HOST_BANG_CLK);
		DELAY(2);
		mask = mask >> 1;
		if (i == 2)
			break;
		bit = ((d2w & mask) != 0) ? 1 : 0;
		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, bit | o1 |
		    URTW_BB_HOST_BANG_CLK);
		DELAY(2);
		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, bit | o1 |
		    URTW_BB_HOST_BANG_CLK);
		DELAY(2);
		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, bit | o1);
		DELAY(1);
	}
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, bit | o1 | URTW_BB_HOST_BANG_RW |
	    URTW_BB_HOST_BANG_CLK);
	DELAY(2);
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, bit | o1 | URTW_BB_HOST_BANG_RW);
	DELAY(2);
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, o1 | URTW_BB_HOST_BANG_RW);
	DELAY(2);

	mask = 0x800;
	for (i = 0; i < rlen; i++, mask = mask >> 1) {
		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT,
		    o1 | URTW_BB_HOST_BANG_RW);
		DELAY(2);
		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT,
		    o1 | URTW_BB_HOST_BANG_RW | URTW_BB_HOST_BANG_CLK);
		DELAY(2);
		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT,
		    o1 | URTW_BB_HOST_BANG_RW | URTW_BB_HOST_BANG_CLK);
		DELAY(2);
		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT,
		    o1 | URTW_BB_HOST_BANG_RW | URTW_BB_HOST_BANG_CLK);
		DELAY(2);

		urtw_read16_m(sc, URTW_RF_PINS_INPUT, &tmp);
		value |= ((tmp & URTW_BB_HOST_BANG_CLK) ? mask : 0);
		urtw_write16_m(sc, URTW_RF_PINS_OUTPUT,
		    o1 | URTW_BB_HOST_BANG_RW);
		DELAY(2);
	}

	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, o1 | URTW_BB_HOST_BANG_EN |
	    URTW_BB_HOST_BANG_RW);
	DELAY(2);

	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, o2);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, o3);
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, URTW_RF_PINS_OUTPUT_MAGIC1);

	if (data != NULL)
		*data = value;
fail:
	return (error);
}


static usb_error_t
urtw_8225v2_set_txpwrlvl(struct urtw_softc *sc, int chan)
{
	int i;
	uint8_t *cck_pwrtable;
	uint8_t cck_pwrlvl_max = 15, ofdm_pwrlvl_max = 25, ofdm_pwrlvl_min = 10;
	uint8_t cck_pwrlvl = sc->sc_txpwr_cck[chan] & 0xff;
	uint8_t ofdm_pwrlvl = sc->sc_txpwr_ofdm[chan] & 0xff;
	usb_error_t error;

	/* CCK power setting */
	cck_pwrlvl = (cck_pwrlvl > cck_pwrlvl_max) ? cck_pwrlvl_max : cck_pwrlvl;
	cck_pwrlvl += sc->sc_txpwr_cck_base;
	cck_pwrlvl = (cck_pwrlvl > 35) ? 35 : cck_pwrlvl;
	cck_pwrtable = (chan == 14) ? urtw_8225v2_txpwr_cck_ch14 :
	    urtw_8225v2_txpwr_cck;

	for (i = 0; i < 8; i++)
		urtw_8187_write_phy_cck(sc, 0x44 + i, cck_pwrtable[i]);

	urtw_write8_m(sc, URTW_TX_GAIN_CCK,
	    urtw_8225v2_tx_gain_cck_ofdm[cck_pwrlvl]);
	usb_pause_mtx(&sc->sc_mtx, 1);

	/* OFDM power setting */
	ofdm_pwrlvl = (ofdm_pwrlvl > (ofdm_pwrlvl_max - ofdm_pwrlvl_min)) ?
		ofdm_pwrlvl_max : ofdm_pwrlvl + ofdm_pwrlvl_min;
	ofdm_pwrlvl += sc->sc_txpwr_ofdm_base;
	ofdm_pwrlvl = (ofdm_pwrlvl > 35) ? 35 : ofdm_pwrlvl;

	error = urtw_8185_set_anaparam2(sc, URTW_8225_ANAPARAM2_ON);
	if (error)
		goto fail;

	urtw_8187_write_phy_ofdm(sc, 2, 0x42);
	urtw_8187_write_phy_ofdm(sc, 5, 0x0);
	urtw_8187_write_phy_ofdm(sc, 6, 0x40);
	urtw_8187_write_phy_ofdm(sc, 7, 0x0);
	urtw_8187_write_phy_ofdm(sc, 8, 0x40);

	urtw_write8_m(sc, URTW_TX_GAIN_OFDM,
	    urtw_8225v2_tx_gain_cck_ofdm[ofdm_pwrlvl]);
	usb_pause_mtx(&sc->sc_mtx, 1);
fail:
	return (error);
}

static usb_error_t
urtw_8225v2_setgain(struct urtw_softc *sc, int16_t gain)
{
	uint8_t *gainp;
	usb_error_t error;

	/* XXX for A?  */
	gainp = urtw_8225v2_gain_bg;
	urtw_8187_write_phy_ofdm(sc, 0x0d, gainp[gain * 3]);
	usb_pause_mtx(&sc->sc_mtx, 1);
	urtw_8187_write_phy_ofdm(sc, 0x1b, gainp[gain * 3 + 1]);
	usb_pause_mtx(&sc->sc_mtx, 1);
	urtw_8187_write_phy_ofdm(sc, 0x1d, gainp[gain * 3 + 2]);
	usb_pause_mtx(&sc->sc_mtx, 1);
	urtw_8187_write_phy_ofdm(sc, 0x21, 0x17);
	usb_pause_mtx(&sc->sc_mtx, 1);
fail:
	return (error);
}

static usb_error_t
urtw_8225_isv2(struct urtw_softc *sc, int *ret)
{
	uint32_t data;
	usb_error_t error;

	*ret = 1;

	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, URTW_RF_PINS_MAGIC5);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, URTW_RF_PINS_MAGIC5);
	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, URTW_RF_PINS_MAGIC5);
	usb_pause_mtx(&sc->sc_mtx, 500);

	urtw_8225_write(sc, URTW_8225_ADDR_0_MAGIC,
	    URTW_8225_ADDR_0_DATA_MAGIC1);

	error = urtw_8225_read(sc, URTW_8225_ADDR_8_MAGIC, &data);
	if (error != 0)
		goto fail;
	if (data != URTW_8225_ADDR_8_DATA_MAGIC1)
		*ret = 0;
	else {
		error = urtw_8225_read(sc, URTW_8225_ADDR_9_MAGIC, &data);
		if (error != 0)
			goto fail;
		if (data != URTW_8225_ADDR_9_DATA_MAGIC1)
			*ret = 0;
	}

	urtw_8225_write(sc, URTW_8225_ADDR_0_MAGIC,
	    URTW_8225_ADDR_0_DATA_MAGIC2);
fail:
	return (error);
}

static usb_error_t
urtw_8225v2b_rf_init(struct urtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	unsigned int i;
	uint8_t data8;
	usb_error_t error;

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;

	/*
	 * initialize extra registers on 8187
	 */
	urtw_write16_m(sc, URTW_BRSR_8187B, 0xfff);

	/* retry limit */
	urtw_read8_m(sc, URTW_CW_CONF, &data8);
	data8 |= URTW_CW_CONF_PERPACKET_RETRY;
	urtw_write8_m(sc, URTW_CW_CONF, data8);

	/* TX AGC */
	urtw_read8_m(sc, URTW_TX_AGC_CTL, &data8);
	data8 |= URTW_TX_AGC_CTL_PERPACKET_GAIN;
	urtw_write8_m(sc, URTW_TX_AGC_CTL, data8);

	/* Auto Rate Fallback Control */
#define	URTW_ARFR	0x1e0
	urtw_write16_m(sc, URTW_ARFR, 0xfff);
	urtw_read8_m(sc, URTW_RATE_FALLBACK, &data8);
	urtw_write8_m(sc, URTW_RATE_FALLBACK,
	    data8 | URTW_RATE_FALLBACK_ENABLE);

	urtw_read8_m(sc, URTW_MSR, &data8);
	urtw_write8_m(sc, URTW_MSR, data8 & 0xf3);
	urtw_read8_m(sc, URTW_MSR, &data8);
	urtw_write8_m(sc, URTW_MSR, data8 | URTW_MSR_LINK_ENEDCA);
	urtw_write8_m(sc, URTW_ACM_CONTROL, sc->sc_acmctl);

	urtw_write16_m(sc, URTW_ATIM_WND, 2);
	urtw_write16_m(sc, URTW_BEACON_INTERVAL, 100);
#define	URTW_FEMR_FOR_8187B	0x1d4
	urtw_write16_m(sc, URTW_FEMR_FOR_8187B, 0xffff);

	/* led type */
	urtw_read8_m(sc, URTW_CONFIG1, &data8);
	data8 = (data8 & 0x3f) | 0x80;
	urtw_write8_m(sc, URTW_CONFIG1, data8);

	/* applying MAC address again.  */
	urtw_write32_m(sc, URTW_MAC0, ((uint32_t *)ic->ic_macaddr)[0]);
	urtw_write16_m(sc, URTW_MAC4, ((uint32_t *)ic->ic_macaddr)[1] & 0xffff);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	urtw_write8_m(sc, URTW_WPA_CONFIG, 0);

	/*
	 * MAC configuration
	 */
	for (i = 0; i < nitems(urtw_8225v2b_rf_part1); i++)
		urtw_write8_m(sc, urtw_8225v2b_rf_part1[i].reg,
		    urtw_8225v2b_rf_part1[i].val);
	urtw_write16_m(sc, URTW_TID_AC_MAP, 0xfa50);
	urtw_write16_m(sc, URTW_INT_MIG, 0x0000);
	urtw_write32_m(sc, 0x1f0, 0);
	urtw_write32_m(sc, 0x1f4, 0);
	urtw_write8_m(sc, 0x1f8, 0);
	urtw_write32_m(sc, URTW_RF_TIMING, 0x4001);

#define	URTW_RFSW_CTRL	0x272
	urtw_write16_m(sc, URTW_RFSW_CTRL, 0x569a);

	/*
	 * initialize PHY
	 */
	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;
	urtw_read8_m(sc, URTW_CONFIG3, &data8);
	urtw_write8_m(sc, URTW_CONFIG3,
	    data8 | URTW_CONFIG3_ANAPARAM_WRITE);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	/* setup RFE initial timing */
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, 0x0480);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, 0x2488);
	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, 0x1fff);
	usb_pause_mtx(&sc->sc_mtx, 1100);

	for (i = 0; i < nitems(urtw_8225v2b_rf_part0); i++) {
		urtw_8225_write(sc, urtw_8225v2b_rf_part0[i].reg,
		    urtw_8225v2b_rf_part0[i].val);
		usb_pause_mtx(&sc->sc_mtx, 1);
	}
	urtw_8225_write(sc, 0x00, 0x01b7);

	for (i = 0; i < 95; i++) {
		urtw_8225_write(sc, URTW_8225_ADDR_1_MAGIC, (uint8_t)(i + 1));
		usb_pause_mtx(&sc->sc_mtx, 1);
		urtw_8225_write(sc, URTW_8225_ADDR_2_MAGIC,
		    urtw_8225v2b_rxgain[i]);
		usb_pause_mtx(&sc->sc_mtx, 1);
	}

	urtw_8225_write(sc, URTW_8225_ADDR_3_MAGIC, 0x080);
	usb_pause_mtx(&sc->sc_mtx, 1);
	urtw_8225_write(sc, URTW_8225_ADDR_5_MAGIC, 0x004);
	usb_pause_mtx(&sc->sc_mtx, 1);
	urtw_8225_write(sc, URTW_8225_ADDR_0_MAGIC, 0x0b7);
	usb_pause_mtx(&sc->sc_mtx, 1);
	usb_pause_mtx(&sc->sc_mtx, 3000);
	urtw_8225_write(sc, URTW_8225_ADDR_2_MAGIC, 0xc4d);
	usb_pause_mtx(&sc->sc_mtx, 2000);
	urtw_8225_write(sc, URTW_8225_ADDR_2_MAGIC, 0x44d);
	usb_pause_mtx(&sc->sc_mtx, 1);
	urtw_8225_write(sc, URTW_8225_ADDR_0_MAGIC, 0x2bf);
	usb_pause_mtx(&sc->sc_mtx, 1);

	urtw_write8_m(sc, URTW_TX_GAIN_CCK, 0x03);
	urtw_write8_m(sc, URTW_TX_GAIN_OFDM, 0x07);
	urtw_write8_m(sc, URTW_TX_ANTENNA, 0x03);

	urtw_8187_write_phy_ofdm(sc, 0x80, 0x12);
	for (i = 0; i < 128; i++) {
		uint32_t addr, data;

		data = (urtw_8225z2_agc[i] << 8) | 0x0000008f;
		addr = ((i + 0x80) << 8) | 0x0000008e;

		urtw_8187_write_phy_ofdm(sc, data & 0x7f, (data >> 8) & 0xff);
		urtw_8187_write_phy_ofdm(sc, addr & 0x7f, (addr >> 8) & 0xff);
		urtw_8187_write_phy_ofdm(sc, 0x0e, 0x00);
	}
	urtw_8187_write_phy_ofdm(sc, 0x80, 0x10);

	for (i = 0; i < nitems(urtw_8225v2b_rf_part2); i++)
		urtw_8187_write_phy_ofdm(sc, i, urtw_8225v2b_rf_part2[i].val);

	urtw_write32_m(sc, URTW_8187B_AC_VO, (7 << 12) | (3 << 8) | 0x1c);
	urtw_write32_m(sc, URTW_8187B_AC_VI, (7 << 12) | (3 << 8) | 0x1c);
	urtw_write32_m(sc, URTW_8187B_AC_BE, (7 << 12) | (3 << 8) | 0x1c);
	urtw_write32_m(sc, URTW_8187B_AC_BK, (7 << 12) | (3 << 8) | 0x1c);

	urtw_8187_write_phy_ofdm(sc, 0x97, 0x46);
	urtw_8187_write_phy_ofdm(sc, 0xa4, 0xb6);
	urtw_8187_write_phy_ofdm(sc, 0x85, 0xfc);
	urtw_8187_write_phy_cck(sc, 0xc1, 0x88);

fail:
	return (error);
}

static usb_error_t
urtw_8225v2b_rf_set_chan(struct urtw_softc *sc, int chan)
{
	usb_error_t error;

	error = urtw_8225v2b_set_txpwrlvl(sc, chan);
	if (error)
		goto fail;

	urtw_8225_write(sc, URTW_8225_ADDR_7_MAGIC, urtw_8225_channel[chan]);
	usb_pause_mtx(&sc->sc_mtx, 10);
fail:
	return (error);
}

static usb_error_t
urtw_8225v2b_set_txpwrlvl(struct urtw_softc *sc, int chan)
{
	int i;
	uint8_t *cck_pwrtable;
	uint8_t cck_pwrlvl_max = 15;
	uint8_t cck_pwrlvl = sc->sc_txpwr_cck[chan] & 0xff;
	uint8_t ofdm_pwrlvl = sc->sc_txpwr_ofdm[chan] & 0xff;
	usb_error_t error;

	/* CCK power setting */
	cck_pwrlvl = (cck_pwrlvl > cck_pwrlvl_max) ?
	    ((sc->sc_flags & URTW_RTL8187B_REV_B) ? cck_pwrlvl_max : 22) :
	    (cck_pwrlvl + ((sc->sc_flags & URTW_RTL8187B_REV_B) ? 0 : 7));
	cck_pwrlvl += sc->sc_txpwr_cck_base;
	cck_pwrlvl = (cck_pwrlvl > 35) ? 35 : cck_pwrlvl;
	cck_pwrtable = (chan == 14) ? urtw_8225v2b_txpwr_cck_ch14 :
	    urtw_8225v2b_txpwr_cck;

	if (sc->sc_flags & URTW_RTL8187B_REV_B)
		cck_pwrtable += (cck_pwrlvl <= 6) ? 0 :
		    ((cck_pwrlvl <= 11) ? 8 : 16);
	else
		cck_pwrtable += (cck_pwrlvl <= 5) ? 0 :
		    ((cck_pwrlvl <= 11) ? 8 : ((cck_pwrlvl <= 17) ? 16 : 24));

	for (i = 0; i < 8; i++)
		urtw_8187_write_phy_cck(sc, 0x44 + i, cck_pwrtable[i]);

	urtw_write8_m(sc, URTW_TX_GAIN_CCK,
	    urtw_8225v2_tx_gain_cck_ofdm[cck_pwrlvl] << 1);
	usb_pause_mtx(&sc->sc_mtx, 1);

	/* OFDM power setting */
	ofdm_pwrlvl = (ofdm_pwrlvl > 15) ?
	    ((sc->sc_flags & URTW_RTL8187B_REV_B) ? 17 : 25) :
	    (ofdm_pwrlvl + ((sc->sc_flags & URTW_RTL8187B_REV_B) ? 2 : 10));
	ofdm_pwrlvl += sc->sc_txpwr_ofdm_base;
	ofdm_pwrlvl = (ofdm_pwrlvl > 35) ? 35 : ofdm_pwrlvl;

	urtw_write8_m(sc, URTW_TX_GAIN_OFDM,
	    urtw_8225v2_tx_gain_cck_ofdm[ofdm_pwrlvl] << 1);

	if (sc->sc_flags & URTW_RTL8187B_REV_B) {
		if (ofdm_pwrlvl <= 11) {
			urtw_8187_write_phy_ofdm(sc, 0x87, 0x60);
			urtw_8187_write_phy_ofdm(sc, 0x89, 0x60);
		} else {
			urtw_8187_write_phy_ofdm(sc, 0x87, 0x5c);
			urtw_8187_write_phy_ofdm(sc, 0x89, 0x5c);
		}
	} else {
		if (ofdm_pwrlvl <= 11) {
			urtw_8187_write_phy_ofdm(sc, 0x87, 0x5c);
			urtw_8187_write_phy_ofdm(sc, 0x89, 0x5c);
		} else if (ofdm_pwrlvl <= 17) {
			urtw_8187_write_phy_ofdm(sc, 0x87, 0x54);
			urtw_8187_write_phy_ofdm(sc, 0x89, 0x54);
		} else {
			urtw_8187_write_phy_ofdm(sc, 0x87, 0x50);
			urtw_8187_write_phy_ofdm(sc, 0x89, 0x50);
		}
	}
	usb_pause_mtx(&sc->sc_mtx, 1);
fail:
	return (error);
}

static usb_error_t
urtw_read8e(struct urtw_softc *sc, int val, uint8_t *data)
{
	struct usb_device_request req;
	usb_error_t error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = URTW_8187_GETREGS_REQ;
	USETW(req.wValue, val | 0xfe00);
	USETW(req.wIndex, 0);
	USETW(req.wLength, sizeof(uint8_t));

	error = urtw_do_request(sc, &req, data);
	return (error);
}

static usb_error_t
urtw_write8e(struct urtw_softc *sc, int val, uint8_t data)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URTW_8187_SETREGS_REQ;
	USETW(req.wValue, val | 0xfe00);
	USETW(req.wIndex, 0);
	USETW(req.wLength, sizeof(uint8_t));

	return (urtw_do_request(sc, &req, &data));
}

static usb_error_t
urtw_8180_set_anaparam(struct urtw_softc *sc, uint32_t val)
{
	uint8_t data;
	usb_error_t error;

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;

	urtw_read8_m(sc, URTW_CONFIG3, &data);
	urtw_write8_m(sc, URTW_CONFIG3, data | URTW_CONFIG3_ANAPARAM_WRITE);
	urtw_write32_m(sc, URTW_ANAPARAM, val);
	urtw_read8_m(sc, URTW_CONFIG3, &data);
	urtw_write8_m(sc, URTW_CONFIG3, data & ~URTW_CONFIG3_ANAPARAM_WRITE);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;
fail:
	return (error);
}

static usb_error_t
urtw_8185_set_anaparam2(struct urtw_softc *sc, uint32_t val)
{
	uint8_t data;
	usb_error_t error;

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;

	urtw_read8_m(sc, URTW_CONFIG3, &data);
	urtw_write8_m(sc, URTW_CONFIG3, data | URTW_CONFIG3_ANAPARAM_WRITE);
	urtw_write32_m(sc, URTW_ANAPARAM2, val);
	urtw_read8_m(sc, URTW_CONFIG3, &data);
	urtw_write8_m(sc, URTW_CONFIG3, data & ~URTW_CONFIG3_ANAPARAM_WRITE);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;
fail:
	return (error);
}

static usb_error_t
urtw_intr_enable(struct urtw_softc *sc)
{
	usb_error_t error;

	urtw_write16_m(sc, URTW_INTR_MASK, 0xffff);
fail:
	return (error);
}

static usb_error_t
urtw_intr_disable(struct urtw_softc *sc)
{
	usb_error_t error;

	urtw_write16_m(sc, URTW_INTR_MASK, 0);
fail:
	return (error);
}

static usb_error_t
urtw_reset(struct urtw_softc *sc)
{
	uint8_t data;
	usb_error_t error;

	error = urtw_8180_set_anaparam(sc, URTW_8225_ANAPARAM_ON);
	if (error)
		goto fail;
	error = urtw_8185_set_anaparam2(sc, URTW_8225_ANAPARAM2_ON);
	if (error)
		goto fail;

	error = urtw_intr_disable(sc);
	if (error)
		goto fail;
	usb_pause_mtx(&sc->sc_mtx, 100);

	error = urtw_write8e(sc, 0x18, 0x10);
	if (error != 0)
		goto fail;
	error = urtw_write8e(sc, 0x18, 0x11);
	if (error != 0)
		goto fail;
	error = urtw_write8e(sc, 0x18, 0x00);
	if (error != 0)
		goto fail;
	usb_pause_mtx(&sc->sc_mtx, 100);

	urtw_read8_m(sc, URTW_CMD, &data);
	data = (data & 0x2) | URTW_CMD_RST;
	urtw_write8_m(sc, URTW_CMD, data);
	usb_pause_mtx(&sc->sc_mtx, 100);

	urtw_read8_m(sc, URTW_CMD, &data);
	if (data & URTW_CMD_RST) {
		device_printf(sc->sc_dev, "reset timeout\n");
		goto fail;
	}

	error = urtw_set_mode(sc, URTW_EPROM_CMD_LOAD);
	if (error)
		goto fail;
	usb_pause_mtx(&sc->sc_mtx, 100);

	error = urtw_8180_set_anaparam(sc, URTW_8225_ANAPARAM_ON);
	if (error)
		goto fail;
	error = urtw_8185_set_anaparam2(sc, URTW_8225_ANAPARAM2_ON);
	if (error)
		goto fail;
fail:
	return (error);
}

static usb_error_t
urtw_led_ctl(struct urtw_softc *sc, int mode)
{
	usb_error_t error = 0;

	switch (sc->sc_strategy) {
	case URTW_SW_LED_MODE0:
		error = urtw_led_mode0(sc, mode);
		break;
	case URTW_SW_LED_MODE1:
		error = urtw_led_mode1(sc, mode);
		break;
	case URTW_SW_LED_MODE2:
		error = urtw_led_mode2(sc, mode);
		break;
	case URTW_SW_LED_MODE3:
		error = urtw_led_mode3(sc, mode);
		break;
	default:
		DPRINTF(sc, URTW_DEBUG_STATE,
		    "unsupported LED mode %d\n", sc->sc_strategy);
		error = USB_ERR_INVAL;
		break;
	}

	return (error);
}

static usb_error_t
urtw_led_mode0(struct urtw_softc *sc, int mode)
{

	switch (mode) {
	case URTW_LED_CTL_POWER_ON:
		sc->sc_gpio_ledstate = URTW_LED_POWER_ON_BLINK;
		break;
	case URTW_LED_CTL_TX:
		if (sc->sc_gpio_ledinprogress == 1)
			return (0);

		sc->sc_gpio_ledstate = URTW_LED_BLINK_NORMAL;
		sc->sc_gpio_blinktime = 2;
		break;
	case URTW_LED_CTL_LINK:
		sc->sc_gpio_ledstate = URTW_LED_ON;
		break;
	default:
		DPRINTF(sc, URTW_DEBUG_STATE,
		    "unsupported LED mode 0x%x", mode);
		return (USB_ERR_INVAL);
	}

	switch (sc->sc_gpio_ledstate) {
	case URTW_LED_ON:
		if (sc->sc_gpio_ledinprogress != 0)
			break;
		urtw_led_on(sc, URTW_LED_GPIO);
		break;
	case URTW_LED_BLINK_NORMAL:
		if (sc->sc_gpio_ledinprogress != 0)
			break;
		sc->sc_gpio_ledinprogress = 1;
		sc->sc_gpio_blinkstate = (sc->sc_gpio_ledon != 0) ?
			URTW_LED_OFF : URTW_LED_ON;
		usb_callout_reset(&sc->sc_led_ch, hz, urtw_led_ch, sc);
		break;
	case URTW_LED_POWER_ON_BLINK:
		urtw_led_on(sc, URTW_LED_GPIO);
		usb_pause_mtx(&sc->sc_mtx, 100);
		urtw_led_off(sc, URTW_LED_GPIO);
		break;
	default:
		DPRINTF(sc, URTW_DEBUG_STATE,
		    "unknown LED status 0x%x", sc->sc_gpio_ledstate);
		return (USB_ERR_INVAL);
	}
	return (0);
}

static usb_error_t
urtw_led_mode1(struct urtw_softc *sc, int mode)
{
	return (USB_ERR_INVAL);
}

static usb_error_t
urtw_led_mode2(struct urtw_softc *sc, int mode)
{
	return (USB_ERR_INVAL);
}

static usb_error_t
urtw_led_mode3(struct urtw_softc *sc, int mode)
{
	return (USB_ERR_INVAL);
}

static usb_error_t
urtw_led_on(struct urtw_softc *sc, int type)
{
	usb_error_t error;

	if (type == URTW_LED_GPIO) {
		switch (sc->sc_gpio_ledpin) {
		case URTW_LED_PIN_GPIO0:
			urtw_write8_m(sc, URTW_GPIO, 0x01);
			urtw_write8_m(sc, URTW_GP_ENABLE, 0x00);
			break;
		default:
			DPRINTF(sc, URTW_DEBUG_STATE,
			    "unsupported LED PIN type 0x%x",
			    sc->sc_gpio_ledpin);
			error = USB_ERR_INVAL;
			goto fail;
		}
	} else {
		DPRINTF(sc, URTW_DEBUG_STATE,
		    "unsupported LED type 0x%x", type);
		error = USB_ERR_INVAL;
		goto fail;
	}

	sc->sc_gpio_ledon = 1;
fail:
	return (error);
}

static usb_error_t
urtw_led_off(struct urtw_softc *sc, int type)
{
	usb_error_t error;

	if (type == URTW_LED_GPIO) {
		switch (sc->sc_gpio_ledpin) {
		case URTW_LED_PIN_GPIO0:
			urtw_write8_m(sc, URTW_GPIO, URTW_GPIO_DATA_MAGIC1);
			urtw_write8_m(sc,
			    URTW_GP_ENABLE, URTW_GP_ENABLE_DATA_MAGIC1);
			break;
		default:
			DPRINTF(sc, URTW_DEBUG_STATE,
			    "unsupported LED PIN type 0x%x",
			    sc->sc_gpio_ledpin);
			error = USB_ERR_INVAL;
			goto fail;
		}
	} else {
		DPRINTF(sc, URTW_DEBUG_STATE,
		    "unsupported LED type 0x%x", type);
		error = USB_ERR_INVAL;
		goto fail;
	}

	sc->sc_gpio_ledon = 0;

fail:
	return (error);
}

static void
urtw_led_ch(void *arg)
{
	struct urtw_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	ieee80211_runtask(ic, &sc->sc_led_task);
}

static void
urtw_ledtask(void *arg, int pending)
{
	struct urtw_softc *sc = arg;

	if (sc->sc_strategy != URTW_SW_LED_MODE0) {
		DPRINTF(sc, URTW_DEBUG_STATE,
		    "could not process a LED strategy 0x%x",
		    sc->sc_strategy);
		return;
	}

	URTW_LOCK(sc);
	urtw_led_blink(sc);
	URTW_UNLOCK(sc);
}

static usb_error_t
urtw_led_blink(struct urtw_softc *sc)
{
	uint8_t ing = 0;
	usb_error_t error;

	if (sc->sc_gpio_blinkstate == URTW_LED_ON)
		error = urtw_led_on(sc, URTW_LED_GPIO);
	else
		error = urtw_led_off(sc, URTW_LED_GPIO);
	sc->sc_gpio_blinktime--;
	if (sc->sc_gpio_blinktime == 0)
		ing = 1;
	else {
		if (sc->sc_gpio_ledstate != URTW_LED_BLINK_NORMAL &&
		    sc->sc_gpio_ledstate != URTW_LED_BLINK_SLOWLY &&
		    sc->sc_gpio_ledstate != URTW_LED_BLINK_CM3)
			ing = 1;
	}
	if (ing == 1) {
		if (sc->sc_gpio_ledstate == URTW_LED_ON &&
		    sc->sc_gpio_ledon == 0)
			error = urtw_led_on(sc, URTW_LED_GPIO);
		else if (sc->sc_gpio_ledstate == URTW_LED_OFF &&
		    sc->sc_gpio_ledon == 1)
			error = urtw_led_off(sc, URTW_LED_GPIO);

		sc->sc_gpio_blinktime = 0;
		sc->sc_gpio_ledinprogress = 0;
		return (0);
	}

	sc->sc_gpio_blinkstate = (sc->sc_gpio_blinkstate != URTW_LED_ON) ?
	    URTW_LED_ON : URTW_LED_OFF;

	switch (sc->sc_gpio_ledstate) {
	case URTW_LED_BLINK_NORMAL:
		usb_callout_reset(&sc->sc_led_ch, hz, urtw_led_ch, sc);
		break;
	default:
		DPRINTF(sc, URTW_DEBUG_STATE,
		    "unknown LED status 0x%x",
		    sc->sc_gpio_ledstate);
		return (USB_ERR_INVAL);
	}
	return (0);
}

static usb_error_t
urtw_rx_enable(struct urtw_softc *sc)
{
	uint8_t data;
	usb_error_t error;

	usbd_transfer_start((sc->sc_flags & URTW_RTL8187B) ?
	    sc->sc_xfer[URTW_8187B_BULK_RX] : sc->sc_xfer[URTW_8187L_BULK_RX]);

	error = urtw_rx_setconf(sc);
	if (error != 0)
		goto fail;

	if ((sc->sc_flags & URTW_RTL8187B) == 0) {
		urtw_read8_m(sc, URTW_CMD, &data);
		urtw_write8_m(sc, URTW_CMD, data | URTW_CMD_RX_ENABLE);
	}
fail:
	return (error);
}

static usb_error_t
urtw_tx_enable(struct urtw_softc *sc)
{
	uint8_t data8;
	uint32_t data;
	usb_error_t error;

	if (sc->sc_flags & URTW_RTL8187B) {
		urtw_read32_m(sc, URTW_TX_CONF, &data);
		data &= ~URTW_TX_LOOPBACK_MASK;
		data &= ~(URTW_TX_DPRETRY_MASK | URTW_TX_RTSRETRY_MASK);
		data &= ~(URTW_TX_NOCRC | URTW_TX_MXDMA_MASK);
		data &= ~URTW_TX_SWPLCPLEN;
		data |= URTW_TX_HW_SEQNUM | URTW_TX_DISREQQSIZE |
		    (7 << 8) |	/* short retry limit */
		    (7 << 0) |	/* long retry limit */
		    (7 << 21);	/* MAX TX DMA */
		urtw_write32_m(sc, URTW_TX_CONF, data);

		urtw_read8_m(sc, URTW_MSR, &data8);
		data8 |= URTW_MSR_LINK_ENEDCA;
		urtw_write8_m(sc, URTW_MSR, data8);
		return (error);
	}

	urtw_read8_m(sc, URTW_CW_CONF, &data8);
	data8 &= ~(URTW_CW_CONF_PERPACKET_CW | URTW_CW_CONF_PERPACKET_RETRY);
	urtw_write8_m(sc, URTW_CW_CONF, data8);

	urtw_read8_m(sc, URTW_TX_AGC_CTL, &data8);
	data8 &= ~URTW_TX_AGC_CTL_PERPACKET_GAIN;
	data8 &= ~URTW_TX_AGC_CTL_PERPACKET_ANTSEL;
	data8 &= ~URTW_TX_AGC_CTL_FEEDBACK_ANT;
	urtw_write8_m(sc, URTW_TX_AGC_CTL, data8);

	urtw_read32_m(sc, URTW_TX_CONF, &data);
	data &= ~URTW_TX_LOOPBACK_MASK;
	data |= URTW_TX_LOOPBACK_NONE;
	data &= ~(URTW_TX_DPRETRY_MASK | URTW_TX_RTSRETRY_MASK);
	data |= sc->sc_tx_retry << URTW_TX_DPRETRY_SHIFT;
	data |= sc->sc_rts_retry << URTW_TX_RTSRETRY_SHIFT;
	data &= ~(URTW_TX_NOCRC | URTW_TX_MXDMA_MASK);
	data |= URTW_TX_MXDMA_2048 | URTW_TX_CWMIN | URTW_TX_DISCW;
	data &= ~URTW_TX_SWPLCPLEN;
	data |= URTW_TX_NOICV;
	urtw_write32_m(sc, URTW_TX_CONF, data);

	urtw_read8_m(sc, URTW_CMD, &data8);
	urtw_write8_m(sc, URTW_CMD, data8 | URTW_CMD_TX_ENABLE);
fail:
	return (error);
}

static usb_error_t
urtw_rx_setconf(struct urtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t data;
	usb_error_t error;

	urtw_read32_m(sc, URTW_RX, &data);
	data = data &~ URTW_RX_FILTER_MASK;
	if (sc->sc_flags & URTW_RTL8187B) {
		data = data | URTW_RX_FILTER_MNG | URTW_RX_FILTER_DATA |
		    URTW_RX_FILTER_MCAST | URTW_RX_FILTER_BCAST |
		    URTW_RX_FIFO_THRESHOLD_NONE |
		    URTW_MAX_RX_DMA_2048 |
		    URTW_RX_AUTORESETPHY | URTW_RCR_ONLYERLPKT;
	} else {
		data = data | URTW_RX_FILTER_MNG | URTW_RX_FILTER_DATA;
		data = data | URTW_RX_FILTER_BCAST | URTW_RX_FILTER_MCAST;

		if (ic->ic_opmode == IEEE80211_M_MONITOR) {
			data = data | URTW_RX_FILTER_ICVERR;
			data = data | URTW_RX_FILTER_PWR;
		}
		if (sc->sc_crcmon == 1 && ic->ic_opmode == IEEE80211_M_MONITOR)
			data = data | URTW_RX_FILTER_CRCERR;

		data = data &~ URTW_RX_FIFO_THRESHOLD_MASK;
		data = data | URTW_RX_FIFO_THRESHOLD_NONE |
		    URTW_RX_AUTORESETPHY;
		data = data &~ URTW_MAX_RX_DMA_MASK;
		data = data | URTW_MAX_RX_DMA_2048 | URTW_RCR_ONLYERLPKT;
	}

	/* XXX allmulti should not be checked here... */
	if (ic->ic_opmode == IEEE80211_M_MONITOR ||
	    ic->ic_promisc > 0 || ic->ic_allmulti > 0) {
		data = data | URTW_RX_FILTER_CTL;
		data = data | URTW_RX_FILTER_ALLMAC;
	} else {
		data = data | URTW_RX_FILTER_NICMAC;
		data = data | URTW_RX_CHECK_BSSID;
	}

	urtw_write32_m(sc, URTW_RX, data);
fail:
	return (error);
}

static struct mbuf *
urtw_rxeof(struct usb_xfer *xfer, struct urtw_data *data, int *rssi_p,
    int8_t *nf_p)
{
	int actlen, flen, rssi;
	struct ieee80211_frame *wh;
	struct mbuf *m, *mnew;
	struct urtw_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t noise = 0, rate;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	if (sc->sc_flags & URTW_RTL8187B) {
		struct urtw_8187b_rxhdr *rx;

		if (actlen < sizeof(*rx) + IEEE80211_ACK_LEN)
			goto fail;

		rx = (struct urtw_8187b_rxhdr *)(data->buf +
		    (actlen - (sizeof(struct urtw_8187b_rxhdr))));
		flen = le32toh(rx->flag) & 0xfff;
		if (flen > actlen - sizeof(*rx))
			goto fail;

		rate = (le32toh(rx->flag) >> URTW_RX_FLAG_RXRATE_SHIFT) & 0xf;
		/* XXX correct? */
		rssi = rx->rssi & URTW_RX_RSSI_MASK;
		noise = rx->noise;
	} else {
		struct urtw_8187l_rxhdr *rx;

		if (actlen < sizeof(*rx) + IEEE80211_ACK_LEN)
			goto fail;

		rx = (struct urtw_8187l_rxhdr *)(data->buf +
		    (actlen - (sizeof(struct urtw_8187l_rxhdr))));
		flen = le32toh(rx->flag) & 0xfff;
		if (flen > actlen - sizeof(*rx))
			goto fail;

		rate = (le32toh(rx->flag) >> URTW_RX_FLAG_RXRATE_SHIFT) & 0xf;
		/* XXX correct? */
		rssi = rx->rssi & URTW_RX_8187L_RSSI_MASK;
		noise = rx->noise;
	}

	if (flen < IEEE80211_ACK_LEN)
		goto fail;

	mnew = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (mnew == NULL)
		goto fail;

	m = data->m;
	data->m = mnew;
	data->buf = mtod(mnew, uint8_t *);

	/* finalize mbuf */
	m->m_pkthdr.len = m->m_len = flen - IEEE80211_CRC_LEN;

	if (ieee80211_radiotap_active(ic)) {
		struct urtw_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		tap->wr_dbm_antsignal = (int8_t)rssi;
	}

	wh = mtod(m, struct ieee80211_frame *);
	if (IEEE80211_IS_DATA(wh))
		sc->sc_currate = (rate > 0) ? rate : sc->sc_currate;

	*rssi_p = rssi;
	*nf_p = noise;		/* XXX correct? */

	return (m);

fail:
	counter_u64_add(ic->ic_ierrors, 1);
	return (NULL);
}

static void
urtw_bulk_rx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct urtw_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m = NULL;
	struct urtw_data *data;
	int8_t nf = -95;
	int rssi = 1;

	URTW_ASSERT_LOCKED(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		data = STAILQ_FIRST(&sc->sc_rx_active);
		if (data == NULL)
			goto setup;
		STAILQ_REMOVE_HEAD(&sc->sc_rx_active, next);
		m = urtw_rxeof(xfer, data, &rssi, &nf);
		STAILQ_INSERT_TAIL(&sc->sc_rx_inactive, data, next);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
setup:
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
		URTW_UNLOCK(sc);
		if (m != NULL) {
			if (m->m_pkthdr.len >=
			    sizeof(struct ieee80211_frame_min)) {
				ni = ieee80211_find_rxnode(ic,
				    mtod(m, struct ieee80211_frame_min *));
			} else
				ni = NULL;

			if (ni != NULL) {
				(void) ieee80211_input(ni, m, rssi, nf);
				/* node is no longer needed */
				ieee80211_free_node(ni);
			} else
				(void) ieee80211_input_all(ic, m, rssi, nf);
			m = NULL;
		}
		URTW_LOCK(sc);
		break;
	default:
		/* needs it to the inactive queue due to a error.  */
		data = STAILQ_FIRST(&sc->sc_rx_active);
		if (data != NULL) {
			STAILQ_REMOVE_HEAD(&sc->sc_rx_active, next);
			STAILQ_INSERT_TAIL(&sc->sc_rx_inactive, data, next);
		}
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			counter_u64_add(ic->ic_ierrors, 1);
			goto setup;
		}
		break;
	}
}

#define	URTW_STATUS_TYPE_TXCLOSE	1
#define	URTW_STATUS_TYPE_BEACON_INTR	0

static void
urtw_txstatus_eof(struct usb_xfer *xfer)
{
	struct urtw_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	int actlen, type, pktretry, seq;
	uint64_t val;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	if (actlen != sizeof(uint64_t))
		return;

	val = le64toh(sc->sc_txstatus);
	type = (val >> 30) & 0x3;
	if (type == URTW_STATUS_TYPE_TXCLOSE) {
		pktretry = val & 0xff;
		seq = (val >> 16) & 0xff;
		if (pktretry == URTW_TX_MAXRETRY)
			counter_u64_add(ic->ic_oerrors, 1);
		DPRINTF(sc, URTW_DEBUG_TXSTATUS, "pktretry %d seq %#x\n",
		    pktretry, seq);
	}
}

static void
urtw_bulk_tx_status_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct urtw_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	void *dma_buf = usbd_xfer_get_frame_buffer(xfer, 0);

	URTW_ASSERT_LOCKED(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		urtw_txstatus_eof(xfer);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
setup:
		memcpy(dma_buf, &sc->sc_txstatus, sizeof(uint64_t));
		usbd_xfer_set_frame_len(xfer, 0, sizeof(uint64_t));
		usbd_transfer_submit(xfer);
		break;
	default:
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			counter_u64_add(ic->ic_ierrors, 1);
			goto setup;
		}
		break;
	}
}

static void
urtw_txeof(struct usb_xfer *xfer, struct urtw_data *data)
{
	struct urtw_softc *sc = usbd_xfer_softc(xfer);

	URTW_ASSERT_LOCKED(sc);

	if (data->m) {
		/* XXX status? */
		ieee80211_tx_complete(data->ni, data->m, 0);
		data->m = NULL;
		data->ni = NULL;
	}
	sc->sc_txtimer = 0;
}

static void
urtw_bulk_tx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct urtw_softc *sc = usbd_xfer_softc(xfer);
	struct urtw_data *data;

	URTW_ASSERT_LOCKED(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		data = STAILQ_FIRST(&sc->sc_tx_active);
		if (data == NULL)
			goto setup;
		STAILQ_REMOVE_HEAD(&sc->sc_tx_active, next);
		urtw_txeof(xfer, data);
		STAILQ_INSERT_TAIL(&sc->sc_tx_inactive, data, next);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
setup:
		data = STAILQ_FIRST(&sc->sc_tx_pending);
		if (data == NULL) {
			DPRINTF(sc, URTW_DEBUG_XMIT,
			    "%s: empty pending queue\n", __func__);
			return;
		}
		STAILQ_REMOVE_HEAD(&sc->sc_tx_pending, next);
		STAILQ_INSERT_TAIL(&sc->sc_tx_active, data, next);

		usbd_xfer_set_frame_data(xfer, 0, data->buf, data->buflen);
		usbd_transfer_submit(xfer);

		urtw_start(sc);
		break;
	default:
		data = STAILQ_FIRST(&sc->sc_tx_active);
		if (data == NULL)
			goto setup;
		if (data->ni != NULL) {
			if_inc_counter(data->ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
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

static struct urtw_data *
_urtw_getbuf(struct urtw_softc *sc)
{
	struct urtw_data *bf;

	bf = STAILQ_FIRST(&sc->sc_tx_inactive);
	if (bf != NULL)
		STAILQ_REMOVE_HEAD(&sc->sc_tx_inactive, next);
	else
		bf = NULL;
	if (bf == NULL)
		DPRINTF(sc, URTW_DEBUG_XMIT, "%s: %s\n", __func__,
		    "out of xmit buffers");
	return (bf);
}

static struct urtw_data *
urtw_getbuf(struct urtw_softc *sc)
{
	struct urtw_data *bf;

	URTW_ASSERT_LOCKED(sc);

	bf = _urtw_getbuf(sc);
	if (bf == NULL)
		DPRINTF(sc, URTW_DEBUG_XMIT, "%s: stop queue\n", __func__);
	return (bf);
}

static int
urtw_isbmode(uint16_t rate)
{

	return ((rate <= 22 && rate != 12 && rate != 18) ||
	    rate == 44) ? (1) : (0);
}

static uint16_t
urtw_rate2dbps(uint16_t rate)
{

	switch(rate) {
	case 12:
	case 18:
	case 24:
	case 36:
	case 48:
	case 72:
	case 96:
	case 108:
		return (rate * 2);
	default:
		break;
	}
	return (24);
}

static int
urtw_compute_txtime(uint16_t framelen, uint16_t rate,
    uint8_t ismgt, uint8_t isshort)
{
	uint16_t     ceiling, frametime, n_dbps;

	if (urtw_isbmode(rate)) {
		if (ismgt || !isshort || rate == 2)
			frametime = (uint16_t)(144 + 48 +
			    (framelen * 8 / (rate / 2)));
		else
			frametime = (uint16_t)(72 + 24 +
			    (framelen * 8 / (rate / 2)));
		if ((framelen * 8 % (rate / 2)) != 0)
			frametime++;
	} else {
		n_dbps = urtw_rate2dbps(rate);
		ceiling = (16 + 8 * framelen + 6) / n_dbps
		    + (((16 + 8 * framelen + 6) % n_dbps) ? 1 : 0);
		frametime = (uint16_t)(16 + 4 + 4 * ceiling + 6);
	}
	return (frametime);
}

/*
 * Callback from the 802.11 layer to update the
 * slot time based on the current setting.
 */
static void
urtw_updateslot(struct ieee80211com *ic)
{
	struct urtw_softc *sc = ic->ic_softc;

	ieee80211_runtask(ic, &sc->sc_updateslot_task);
}

static void
urtw_updateslottask(void *arg, int pending)
{
	struct urtw_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

	URTW_LOCK(sc);
	if ((sc->sc_flags & URTW_RUNNING) == 0) {
		URTW_UNLOCK(sc);
		return;
	}
	if (sc->sc_flags & URTW_RTL8187B) {
		urtw_write8_m(sc, URTW_SIFS, 0x22);
		if (IEEE80211_IS_CHAN_ANYG(ic->ic_curchan))
			urtw_write8_m(sc, URTW_SLOT, IEEE80211_DUR_SHSLOT);
		else
			urtw_write8_m(sc, URTW_SLOT, IEEE80211_DUR_SLOT);
		urtw_write8_m(sc, URTW_8187B_EIFS, 0x5b);
		urtw_write8_m(sc, URTW_CARRIER_SCOUNT, 0x5b);
	} else {
		urtw_write8_m(sc, URTW_SIFS, 0x22);
		if (sc->sc_state == IEEE80211_S_ASSOC &&
		    ic->ic_flags & IEEE80211_F_SHSLOT)
			urtw_write8_m(sc, URTW_SLOT, IEEE80211_DUR_SHSLOT);
		else
			urtw_write8_m(sc, URTW_SLOT, IEEE80211_DUR_SLOT);
		if (IEEE80211_IS_CHAN_ANYG(ic->ic_curchan)) {
			urtw_write8_m(sc, URTW_DIFS, 0x14);
			urtw_write8_m(sc, URTW_EIFS, 0x5b - 0x14);
			urtw_write8_m(sc, URTW_CW_VAL, 0x73);
		} else {
			urtw_write8_m(sc, URTW_DIFS, 0x24);
			urtw_write8_m(sc, URTW_EIFS, 0x5b - 0x24);
			urtw_write8_m(sc, URTW_CW_VAL, 0xa5);
		}
	}
fail:
	URTW_UNLOCK(sc);
}

static void
urtw_sysctl_node(struct urtw_softc *sc)
{
#define	URTW_SYSCTL_STAT_ADD32(c, h, n, p, d)	\
	SYSCTL_ADD_UINT(c, h, OID_AUTO, n, CTLFLAG_RD, p, 0, d)
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child, *parent;
	struct sysctl_oid *tree;
	struct urtw_stats *stats = &sc->sc_stats;

	ctx = device_get_sysctl_ctx(sc->sc_dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev));

	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "URTW statistics");
	parent = SYSCTL_CHILDREN(tree);

	/* Tx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "Tx MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	URTW_SYSCTL_STAT_ADD32(ctx, child, "1m", &stats->txrates[0],
	    "1 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "2m", &stats->txrates[1],
	    "2 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "5.5m", &stats->txrates[2],
	    "5.5 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "6m", &stats->txrates[4],
	    "6 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "9m", &stats->txrates[5],
	    "9 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "11m", &stats->txrates[3],
	    "11 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "12m", &stats->txrates[6],
	    "12 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "18m", &stats->txrates[7],
	    "18 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "24m", &stats->txrates[8],
	    "24 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "36m", &stats->txrates[9],
	    "36 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "48m", &stats->txrates[10],
	    "48 Mbit/s");
	URTW_SYSCTL_STAT_ADD32(ctx, child, "54m", &stats->txrates[11],
	    "54 Mbit/s");
#undef URTW_SYSCTL_STAT_ADD32
}

static device_method_t urtw_methods[] = {
	DEVMETHOD(device_probe, urtw_match),
	DEVMETHOD(device_attach, urtw_attach),
	DEVMETHOD(device_detach, urtw_detach),
	DEVMETHOD_END
};
static driver_t urtw_driver = {
	.name = "urtw",
	.methods = urtw_methods,
	.size = sizeof(struct urtw_softc)
};
static devclass_t urtw_devclass;

DRIVER_MODULE(urtw, uhub, urtw_driver, urtw_devclass, NULL, 0);
MODULE_DEPEND(urtw, wlan, 1, 1, 1);
MODULE_DEPEND(urtw, usb, 1, 1, 1);
MODULE_VERSION(urtw, 1);
USB_PNP_HOST_INFO(urtw_devs);

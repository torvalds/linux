/*	$OpenBSD: if_urtw.c,v 1.74 2024/09/01 03:09:00 jsg Exp $	*/

/*-
 * Copyright (c) 2009 Martynas Venckus <martynas@openbsd.org>
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

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
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
#include <net80211/ieee80211_radiotap.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_urtwreg.h>

#ifdef URTW_DEBUG
#define	DPRINTF(x)	do { if (urtw_debug) printf x; } while (0)
#define	DPRINTFN(n, x)	do { if (urtw_debug >= (n)) printf x; } while (0)
int urtw_debug = 0;
#else
#define	DPRINTF(x)
#define	DPRINTFN(n, x)
#endif

/*
 * Recognized device vendors/products.
 */
static const struct urtw_type {
	struct usb_devno	dev;
	uint8_t			rev;
} urtw_devs[] = {
#define	URTW_DEV_RTL8187(v, p)	\
	    { { USB_VENDOR_##v, USB_PRODUCT_##v##_##p }, URTW_HWREV_8187 }
#define	URTW_DEV_RTL8187B(v, p)	\
	    { { USB_VENDOR_##v, USB_PRODUCT_##v##_##p }, URTW_HWREV_8187B }
	/* Realtek RTL8187 devices. */
	URTW_DEV_RTL8187(ASUS,		P5B_WIFI),
	URTW_DEV_RTL8187(DICKSMITH,	RTL8187),
	URTW_DEV_RTL8187(LINKSYS4,	WUSB54GCV2),
	URTW_DEV_RTL8187(LOGITEC,	RTL8187),
	URTW_DEV_RTL8187(NETGEAR,	WG111V2),
	URTW_DEV_RTL8187(REALTEK,	RTL8187),
	URTW_DEV_RTL8187(SITECOMEU,	WL168V1),
	URTW_DEV_RTL8187(SPHAIRON,	RTL8187),
	URTW_DEV_RTL8187(SURECOM,	EP9001G2A),
	/* Realtek RTL8187B devices. */
	URTW_DEV_RTL8187B(BELKIN,	F5D7050E),
	URTW_DEV_RTL8187B(NETGEAR,	WG111V3),
	URTW_DEV_RTL8187B(REALTEK,	RTL8187B_0),
	URTW_DEV_RTL8187B(REALTEK,	RTL8187B_1),
	URTW_DEV_RTL8187B(REALTEK,	RTL8187B_2),
	URTW_DEV_RTL8187B(SITECOMEU,	WL168V4)
#undef	URTW_DEV_RTL8187
#undef	URTW_DEV_RTL8187B
};
#define	urtw_lookup(v, p)	\
	    ((const struct urtw_type *)usb_lookup(urtw_devs, v, p))

/*
 * Helper read/write macros.
 */
#define urtw_read8_m(sc, val, data)	do {			\
	error = urtw_read8_c(sc, val, data, 0);			\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_read8_idx_m(sc, val, data, idx)	do {		\
	error = urtw_read8_c(sc, val, data, idx);		\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_write8_m(sc, val, data)	do {			\
	error = urtw_write8_c(sc, val, data, 0);		\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_write8_idx_m(sc, val, data, idx)	do {		\
	error = urtw_write8_c(sc, val, data, idx);		\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_read16_m(sc, val, data)	do {			\
	error = urtw_read16_c(sc, val, data, 0);		\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_read16_idx_m(sc, val, data, idx)	do {		\
	error = urtw_read16_c(sc, val, data, idx);		\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_write16_m(sc, val, data)	do {			\
	error = urtw_write16_c(sc, val, data, 0);		\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_write16_idx_m(sc, val, data, idx)	do {		\
	error = urtw_write16_c(sc, val, data, idx);		\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_read32_m(sc, val, data)	do {			\
	error = urtw_read32_c(sc, val, data, 0);		\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_read32_idx_m(sc, val, data, idx)	do {		\
	error = urtw_read32_c(sc, val, data, idx);		\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_write32_m(sc, val, data)	do {			\
	error = urtw_write32_c(sc, val, data, 0);		\
	if (error != 0)						\
		goto fail;					\
} while (0)
#define urtw_write32_idx_m(sc, val, data, idx)	do {		\
	error = urtw_write32_c(sc, val, data, idx);		\
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

struct urtw_pair_idx {
	uint8_t		reg;
	uint8_t		val;
	uint8_t		idx;
};

static struct urtw_pair_idx urtw_8187b_regtbl[] = {
	{ 0xf0, 0x32, 0 }, { 0xf1, 0x32, 0 }, { 0xf2, 0x00, 0 },
	{ 0xf3, 0x00, 0 }, { 0xf4, 0x32, 0 }, { 0xf5, 0x43, 0 },
	{ 0xf6, 0x00, 0 }, { 0xf7, 0x00, 0 }, { 0xf8, 0x46, 0 },
	{ 0xf9, 0xa4, 0 }, { 0xfa, 0x00, 0 }, { 0xfb, 0x00, 0 },
	{ 0xfc, 0x96, 0 }, { 0xfd, 0xa4, 0 }, { 0xfe, 0x00, 0 },
	{ 0xff, 0x00, 0 },

	{ 0x58, 0x4b, 1 }, { 0x59, 0x00, 1 }, { 0x5a, 0x4b, 1 },
	{ 0x5b, 0x00, 1 }, { 0x60, 0x4b, 1 }, { 0x61, 0x09, 1 },
	{ 0x62, 0x4b, 1 }, { 0x63, 0x09, 1 }, { 0xce, 0x0f, 1 },
	{ 0xcf, 0x00, 1 }, { 0xe0, 0xff, 1 }, { 0xe1, 0x0f, 1 },
	{ 0xe2, 0x00, 1 }, { 0xf0, 0x4e, 1 }, { 0xf1, 0x01, 1 },
	{ 0xf2, 0x02, 1 }, { 0xf3, 0x03, 1 }, { 0xf4, 0x04, 1 },
	{ 0xf5, 0x05, 1 }, { 0xf6, 0x06, 1 }, { 0xf7, 0x07, 1 },
	{ 0xf8, 0x08, 1 },

	{ 0x4e, 0x00, 2 }, { 0x0c, 0x04, 2 }, { 0x21, 0x61, 2 },
	{ 0x22, 0x68, 2 }, { 0x23, 0x6f, 2 }, { 0x24, 0x76, 2 },
	{ 0x25, 0x7d, 2 }, { 0x26, 0x84, 2 }, { 0x27, 0x8d, 2 },
	{ 0x4d, 0x08, 2 }, { 0x50, 0x05, 2 }, { 0x51, 0xf5, 2 },
	{ 0x52, 0x04, 2 }, { 0x53, 0xa0, 2 }, { 0x54, 0x1f, 2 },
	{ 0x55, 0x23, 2 }, { 0x56, 0x45, 2 }, { 0x57, 0x67, 2 },
	{ 0x58, 0x08, 2 }, { 0x59, 0x08, 2 }, { 0x5a, 0x08, 2 },
	{ 0x5b, 0x08, 2 }, { 0x60, 0x08, 2 }, { 0x61, 0x08, 2 },
	{ 0x62, 0x08, 2 }, { 0x63, 0x08, 2 }, { 0x64, 0xcf, 2 },
	{ 0x72, 0x56, 2 }, { 0x73, 0x9a, 2 },

	{ 0x34, 0xf0, 0 }, { 0x35, 0x0f, 0 }, { 0x5b, 0x40, 0 },
	{ 0x84, 0x88, 0 }, { 0x85, 0x24, 0 }, { 0x88, 0x54, 0 },
	{ 0x8b, 0xb8, 0 }, { 0x8c, 0x07, 0 }, { 0x8d, 0x00, 0 },
	{ 0x94, 0x1b, 0 }, { 0x95, 0x12, 0 }, { 0x96, 0x00, 0 },
	{ 0x97, 0x06, 0 }, { 0x9d, 0x1a, 0 }, { 0x9f, 0x10, 0 },
	{ 0xb4, 0x22, 0 }, { 0xbe, 0x80, 0 }, { 0xdb, 0x00, 0 },
	{ 0xee, 0x00, 0 }, { 0x91, 0x03, 0 },

	{ 0x4c, 0x00, 2 }, { 0x9f, 0x00, 3 }, { 0x8c, 0x01, 0 },
	{ 0x8d, 0x10, 0 }, { 0x8e, 0x08, 0 }, { 0x8f, 0x00, 0 }
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

static uint32_t urtw_8225_channel[] = {
	0x0000,		/* dummy channel 0 */
	0x085c,		/* 1 */
	0x08dc,		/* 2 */
	0x095c,		/* 3 */
	0x09dc,		/* 4 */
	0x0a5c,		/* 5 */
	0x0adc,		/* 6 */
	0x0b5c,		/* 7 */
	0x0bdc,		/* 8 */
	0x0c5c,		/* 9 */
	0x0cdc,		/* 10 */
	0x0d5c,		/* 11 */
	0x0ddc,		/* 12 */
	0x0e5c,		/* 13 */
	0x0f72,		/* 14 */
};

static uint8_t urtw_8225_gain[] = {
	0x23, 0x88, 0x7c, 0xa5,		/* -82dbm */
	0x23, 0x88, 0x7c, 0xb5,		/* -82dbm */
	0x23, 0x88, 0x7c, 0xc5,		/* -82dbm */
	0x33, 0x80, 0x79, 0xc5,		/* -78dbm */
	0x43, 0x78, 0x76, 0xc5,		/* -74dbm */
	0x53, 0x60, 0x73, 0xc5,		/* -70dbm */
	0x63, 0x58, 0x70, 0xc5,		/* -66dbm */
};

static struct urtw_pair urtw_8225_rf_part1[] = {
	{ 0x00, 0x0067 }, { 0x01, 0x0fe0 }, { 0x02, 0x044d }, { 0x03, 0x0441 },
	{ 0x04, 0x0486 }, { 0x05, 0x0bc0 }, { 0x06, 0x0ae6 }, { 0x07, 0x082a },
	{ 0x08, 0x001f }, { 0x09, 0x0334 }, { 0x0a, 0x0fd4 }, { 0x0b, 0x0391 },
	{ 0x0c, 0x0050 }, { 0x0d, 0x06db }, { 0x0e, 0x0029 }, { 0x0f, 0x0914 }
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
	0x8d, 0x8d, 0x8d, 0x8d, 0x9d, 0xad, 0xbd
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

static uint8_t urtw_8225_txpwr_ofdm[] = {
	0x80, 0x90, 0xa2, 0xb5, 0xcb, 0xe4
};

static uint8_t urtw_8225v2_agc[] = {
	0x5e, 0x5e, 0x5e, 0x5e, 0x5d, 0x5b, 0x59, 0x57,
	0x55, 0x53, 0x51, 0x4f, 0x4d, 0x4b, 0x49, 0x47,
	0x45, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x39, 0x37,
	0x35, 0x33, 0x31, 0x2f, 0x2d, 0x2b, 0x29, 0x27,
	0x25, 0x23, 0x21, 0x1f, 0x1d, 0x1b, 0x19, 0x17,
	0x15, 0x13, 0x11, 0x0f, 0x0d, 0x0b, 0x09, 0x07,
	0x05, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19,
	0x19, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
	0x26, 0x27, 0x27, 0x28, 0x28, 0x29, 0x2a, 0x2a,
	0x2a, 0x2b, 0x2b, 0x2b, 0x2c, 0x2c, 0x2c, 0x2d,
	0x2d, 0x2d, 0x2d, 0x2e, 0x2e, 0x2e, 0x2e, 0x2f,
	0x2f, 0x2f, 0x30, 0x30, 0x31, 0x31, 0x31, 0x31,
	0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
	0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31
};

static uint8_t urtw_8225v2_ofdm[] = {
	0x10, 0x0d, 0x01, 0x00, 0x14, 0xfb, 0xfb, 0x60,
	0x00, 0x60, 0x00, 0x00, 0x00, 0x5c, 0x00, 0x00,
	0x40, 0x00, 0x40, 0x00, 0x00, 0x00, 0xa8, 0x26,
	0x32, 0x33, 0x07, 0xa5, 0x6f, 0x55, 0xc8, 0xb3,
	0x0a, 0xe1, 0x2c, 0x8a, 0x86, 0x83, 0x34, 0x0f,
	0x4f, 0x24, 0x6f, 0xc2, 0x6b, 0x40, 0x80, 0x00,
	0xc0, 0xc1, 0x58, 0xf1, 0x00, 0xe4, 0x90, 0x3e,
	0x6d, 0x3c, 0xfb, 0x07
};

static uint8_t urtw_8225v2_gain_bg[] = {
	0x23, 0x15, 0xa5,		/* -82-1dbm */
	0x23, 0x15, 0xb5,		/* -82-2dbm */
	0x23, 0x15, 0xc5,		/* -82-3dbm */
	0x33, 0x15, 0xc5,		/* -78dbm */
	0x43, 0x15, 0xc5,		/* -74dbm */
	0x53, 0x15, 0xc5,		/* -70dbm */
	0x63, 0x15, 0xc5,		/* -66dbm */
};

static struct urtw_pair urtw_8225v2_rf_part1[] = {
	{ 0x00, 0x02bf }, { 0x01, 0x0ee0 }, { 0x02, 0x044d }, { 0x03, 0x0441 },
	{ 0x04, 0x08c3 }, { 0x05, 0x0c72 }, { 0x06, 0x00e6 }, { 0x07, 0x082a },
	{ 0x08, 0x003f }, { 0x09, 0x0335 }, { 0x0a, 0x09d4 }, { 0x0b, 0x07bb },
	{ 0x0c, 0x0850 }, { 0x0d, 0x0cdf }, { 0x0e, 0x002b }, { 0x0f, 0x0114 }
};

static struct urtw_pair urtw_8225v2_rf_part2[] = {
	{ 0x00, 0x01 }, { 0x01, 0x02 }, { 0x02, 0x42 }, { 0x03, 0x00 },
	{ 0x04, 0x00 }, { 0x05, 0x00 }, { 0x06, 0x40 }, { 0x07, 0x00 },
	{ 0x08, 0x40 }, { 0x09, 0xfe }, { 0x0a, 0x08 }, { 0x0b, 0x80 },
	{ 0x0c, 0x01 }, { 0x0d, 0x43 }, { 0x0e, 0xd3 }, { 0x0f, 0x38 },
	{ 0x10, 0x84 }, { 0x11, 0x07 }, { 0x12, 0x20 }, { 0x13, 0x20 },
	{ 0x14, 0x00 }, { 0x15, 0x40 }, { 0x16, 0x00 }, { 0x17, 0x40 },
	{ 0x18, 0xef }, { 0x19, 0x19 }, { 0x1a, 0x20 }, { 0x1b, 0x15 },
	{ 0x1c, 0x04 }, { 0x1d, 0xc5 }, { 0x1e, 0x95 }, { 0x1f, 0x75 },
	{ 0x20, 0x1f }, { 0x21, 0x17 }, { 0x22, 0x16 }, { 0x23, 0x80 },
	{ 0x24, 0x46 }, { 0x25, 0x00 }, { 0x26, 0x90 }, { 0x27, 0x88 }
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
	0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23
};

static uint8_t urtw_8225v2_txpwr_cck[] = {
	0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04,
	0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03,
	0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03,
	0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03
};

static uint8_t urtw_8225v2_txpwr_cck_ch14[] = {
	0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00,
	0x30, 0x2f, 0x29, 0x15, 0x00, 0x00, 0x00, 0x00,
	0x30, 0x2f, 0x29, 0x15, 0x00, 0x00, 0x00, 0x00,
	0x30, 0x2f, 0x29, 0x15, 0x00, 0x00, 0x00, 0x00
};

static struct urtw_pair urtw_8225v2_b_rf[] = {
	{ 0x00, 0x00b7 }, { 0x01, 0x0ee0 }, { 0x02, 0x044d }, { 0x03, 0x0441 },
	{ 0x04, 0x08c3 }, { 0x05, 0x0c72 }, { 0x06, 0x00e6 }, { 0x07, 0x082a },
	{ 0x08, 0x003f }, { 0x09, 0x0335 }, { 0x0a, 0x09d4 }, { 0x0b, 0x07bb },
	{ 0x0c, 0x0850 }, { 0x0d, 0x0cdf }, { 0x0e, 0x002b }, { 0x0f, 0x0114 },
	{ 0x00, 0x01b7 }
};

static struct urtw_pair urtw_ratetable[] = {
	{  2,  0 }, {   4,  1 }, { 11, 2 }, { 12, 4 }, { 18, 5 },
	{ 22,  3 }, {  24,  6 }, { 36, 7 }, { 48, 8 }, { 72, 9 },
	{ 96, 10 }, { 108, 11 }
};

int		urtw_init(struct ifnet *);
void		urtw_stop(struct ifnet *, int);
int		urtw_ioctl(struct ifnet *, u_long, caddr_t);
void		urtw_start(struct ifnet *);
int		urtw_alloc_rx_data_list(struct urtw_softc *);
void		urtw_free_rx_data_list(struct urtw_softc *);
int		urtw_alloc_tx_data_list(struct urtw_softc *);
void		urtw_free_tx_data_list(struct urtw_softc *);
void		urtw_rxeof(struct usbd_xfer *, void *,
		    usbd_status);
int		urtw_tx_start(struct urtw_softc *,
		    struct ieee80211_node *, struct mbuf *, int);
void		urtw_txeof_low(struct usbd_xfer *, void *,
		    usbd_status);
void		urtw_txeof_normal(struct usbd_xfer *, void *,
		    usbd_status);
void		urtw_next_scan(void *);
void		urtw_task(void *);
void		urtw_ledusbtask(void *);
void		urtw_ledtask(void *);
int		urtw_media_change(struct ifnet *);
int		urtw_newstate(struct ieee80211com *, enum ieee80211_state, int);
void		urtw_watchdog(struct ifnet *);
void		urtw_set_multi(struct urtw_softc *);
void		urtw_set_chan(struct urtw_softc *, struct ieee80211_channel *);
int		urtw_isbmode(uint16_t);
uint16_t	urtw_rate2rtl(int rate);
uint16_t	urtw_rtl2rate(int);
usbd_status	urtw_set_rate(struct urtw_softc *);
usbd_status	urtw_update_msr(struct urtw_softc *);
usbd_status	urtw_read8_c(struct urtw_softc *, int, uint8_t *, uint8_t);
usbd_status	urtw_read16_c(struct urtw_softc *, int, uint16_t *, uint8_t);
usbd_status	urtw_read32_c(struct urtw_softc *, int, uint32_t *, uint8_t);
usbd_status	urtw_write8_c(struct urtw_softc *, int, uint8_t, uint8_t);
usbd_status	urtw_write16_c(struct urtw_softc *, int, uint16_t, uint8_t);
usbd_status	urtw_write32_c(struct urtw_softc *, int, uint32_t, uint8_t);
usbd_status	urtw_eprom_cs(struct urtw_softc *, int);
usbd_status	urtw_eprom_ck(struct urtw_softc *);
usbd_status	urtw_eprom_sendbits(struct urtw_softc *, int16_t *,
		    int);
usbd_status	urtw_eprom_read32(struct urtw_softc *, uint32_t,
		    uint32_t *);
usbd_status	urtw_eprom_readbit(struct urtw_softc *, int16_t *);
usbd_status	urtw_eprom_writebit(struct urtw_softc *, int16_t);
usbd_status	urtw_get_macaddr(struct urtw_softc *);
usbd_status	urtw_get_txpwr(struct urtw_softc *);
usbd_status	urtw_get_rfchip(struct urtw_softc *);
usbd_status	urtw_led_init(struct urtw_softc *);
usbd_status	urtw_8185_rf_pins_enable(struct urtw_softc *);
usbd_status	urtw_8185_tx_antenna(struct urtw_softc *, uint8_t);
usbd_status	urtw_8187_write_phy(struct urtw_softc *, uint8_t, uint32_t);
usbd_status	urtw_8187_write_phy_ofdm_c(struct urtw_softc *, uint8_t,
		    uint32_t);
usbd_status	urtw_8187_write_phy_cck_c(struct urtw_softc *, uint8_t,
		    uint32_t);
usbd_status	urtw_8225_setgain(struct urtw_softc *, int16_t);
usbd_status	urtw_8225_usb_init(struct urtw_softc *);
usbd_status	urtw_8225_write_c(struct urtw_softc *, uint8_t, uint16_t);
usbd_status	urtw_8225_write_s16(struct urtw_softc *, uint8_t, int,
		    uint16_t);
usbd_status	urtw_8225_read(struct urtw_softc *, uint8_t, uint32_t *);
usbd_status	urtw_8225_rf_init(struct urtw_rf *);
usbd_status	urtw_8225_rf_set_chan(struct urtw_rf *, int);
usbd_status	urtw_8225_rf_set_sens(struct urtw_rf *);
usbd_status	urtw_8225_set_txpwrlvl(struct urtw_softc *, int);
usbd_status	urtw_8225v2_rf_init(struct urtw_rf *);
usbd_status	urtw_8225v2_rf_set_chan(struct urtw_rf *, int);
usbd_status	urtw_8225v2_set_txpwrlvl(struct urtw_softc *, int);
usbd_status	urtw_8225v2_setgain(struct urtw_softc *, int16_t);
usbd_status	urtw_8225_isv2(struct urtw_softc *, int *);
usbd_status	urtw_read8e(struct urtw_softc *, int, uint8_t *);
usbd_status	urtw_write8e(struct urtw_softc *, int, uint8_t);
usbd_status	urtw_8180_set_anaparam(struct urtw_softc *, uint32_t);
usbd_status	urtw_8185_set_anaparam2(struct urtw_softc *, uint32_t);
usbd_status	urtw_open_pipes(struct urtw_softc *);
usbd_status	urtw_close_pipes(struct urtw_softc *);
usbd_status	urtw_intr_enable(struct urtw_softc *);
usbd_status	urtw_intr_disable(struct urtw_softc *);
usbd_status	urtw_reset(struct urtw_softc *);
usbd_status	urtw_led_on(struct urtw_softc *, int);
usbd_status	urtw_led_ctl(struct urtw_softc *, int);
usbd_status	urtw_led_blink(struct urtw_softc *);
usbd_status	urtw_led_mode0(struct urtw_softc *, int);
usbd_status	urtw_led_mode1(struct urtw_softc *, int);
usbd_status	urtw_led_mode2(struct urtw_softc *, int);
usbd_status	urtw_led_mode3(struct urtw_softc *, int);
usbd_status	urtw_rx_setconf(struct urtw_softc *);
usbd_status	urtw_rx_enable(struct urtw_softc *);
usbd_status	urtw_tx_enable(struct urtw_softc *);
usbd_status	urtw_8187b_update_wmm(struct urtw_softc *);
usbd_status	urtw_8187b_reset(struct urtw_softc *);
int		urtw_8187b_init(struct ifnet *);
usbd_status	urtw_8225v2_b_config_mac(struct urtw_softc *);
usbd_status	urtw_8225v2_b_init_rfe(struct urtw_softc *);
usbd_status	urtw_8225v2_b_update_chan(struct urtw_softc *);
usbd_status	urtw_8225v2_b_rf_init(struct urtw_rf *);
usbd_status	urtw_8225v2_b_rf_set_chan(struct urtw_rf *, int);
usbd_status	urtw_8225v2_b_set_txpwrlvl(struct urtw_softc *, int);
int		urtw_set_bssid(struct urtw_softc *, const uint8_t *);
int		urtw_set_macaddr(struct urtw_softc *, const uint8_t *);

int urtw_match(struct device *, void *, void *);
void urtw_attach(struct device *, struct device *, void *);
int urtw_detach(struct device *, int);

struct cfdriver urtw_cd = {
	NULL, "urtw", DV_IFNET
};

const struct cfattach urtw_ca = {
	sizeof(struct urtw_softc), urtw_match, urtw_attach, urtw_detach
};

int
urtw_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return (UMATCH_NONE);

	return ((urtw_lookup(uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE);
}

void
urtw_attach(struct device *parent, struct device *self, void *aux)
{
	struct urtw_softc *sc = (struct urtw_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	usbd_status error;
	uint8_t data8;
	uint32_t data;
	int i;

	sc->sc_udev = uaa->device;
	sc->sc_iface = uaa->iface;
	sc->sc_hwrev = urtw_lookup(uaa->vendor, uaa->product)->rev;

	printf("%s: ", sc->sc_dev.dv_xname);

	if (sc->sc_hwrev & URTW_HWREV_8187) {
		urtw_read32_m(sc, URTW_TX_CONF, &data);
		data &= URTW_TX_HWREV_MASK;
		switch (data) {
		case URTW_TX_HWREV_8187_D:
			sc->sc_hwrev |= URTW_HWREV_8187_D;
			printf("RTL8187 rev D");
			break;
		case URTW_TX_HWREV_8187B_D:
			/*
			 * Detect Realtek RTL8187B devices that use
			 * USB IDs of RTL8187.
			 */
			sc->sc_hwrev = URTW_HWREV_8187B | URTW_HWREV_8187B_B;
			printf("RTL8187B rev B (early)");
			break;
		default:
			sc->sc_hwrev |= URTW_HWREV_8187_B;
			printf("RTL8187 rev 0x%02x", data >> 25);
			break;
		}
	} else {
		/* RTL8187B hwrev register. */
		urtw_read8_m(sc, URTW_8187B_HWREV, &data8);
		switch (data8) {
		case URTW_8187B_HWREV_8187B_B:
			sc->sc_hwrev |= URTW_HWREV_8187B_B;
			printf("RTL8187B rev B");
			break;
		case URTW_8187B_HWREV_8187B_D:
			sc->sc_hwrev |= URTW_HWREV_8187B_D;
			printf("RTL8187B rev D");
			break;
		case URTW_8187B_HWREV_8187B_E:
			sc->sc_hwrev |= URTW_HWREV_8187B_E;
			printf("RTL8187B rev E");
			break;
		default:
			sc->sc_hwrev |= URTW_HWREV_8187B_B;
			printf("RTL8187B rev 0x%02x", data8);
			break;
		}
	}

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
	error = urtw_led_init(sc);		/* XXX incomplete */
	if (error != 0)
		goto fail;

	sc->sc_rts_retry = URTW_DEFAULT_RTS_RETRY;
	sc->sc_tx_retry = URTW_DEFAULT_TX_RETRY;
	sc->sc_currate = 3;
	/* XXX for what? */
	sc->sc_preamble_mode = 2;

	usb_init_task(&sc->sc_task, urtw_task, sc, USB_TASK_TYPE_GENERIC);
	usb_init_task(&sc->sc_ledtask, urtw_ledusbtask, sc,
	    USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->scan_to, urtw_next_scan, sc);
	timeout_set(&sc->sc_led_ch, urtw_ledtask, sc);

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_TXPMGT |	/* tx power management */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
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
	if (sc->sc_hwrev & URTW_HWREV_8187) {
		sc->sc_init = urtw_init;
	} else {
		sc->sc_init = urtw_8187b_init;
	}
	ifp->if_ioctl = urtw_ioctl;
	ifp->if_start = urtw_start;
	ifp->if_watchdog = urtw_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);

	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = urtw_newstate;
	ieee80211_media_init(ifp, urtw_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(URTW_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(URTW_TX_RADIOTAP_PRESENT);
#endif

	printf(", address %s\n", ether_sprintf(ic->ic_myaddr));

	return;
fail:
	printf(": %s failed!\n", __func__);
}

int
urtw_detach(struct device *self, int flags)
{
	struct urtw_softc *sc = (struct urtw_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s;

	s = splusb();

	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
	if (timeout_initialized(&sc->sc_led_ch))
		timeout_del(&sc->sc_led_ch);

	usb_rem_wait_task(sc->sc_udev, &sc->sc_task);
	usb_rem_wait_task(sc->sc_udev, &sc->sc_ledtask);

	usbd_ref_wait(sc->sc_udev);

	if (ifp->if_softc != NULL) {
		ieee80211_ifdetach(ifp);	/* free all nodes */
		if_detach(ifp);
	}

	/* abort and free xfers */
	urtw_free_tx_data_list(sc);
	urtw_free_rx_data_list(sc);
	urtw_close_pipes(sc);

	splx(s);

	return (0);
}

usbd_status
urtw_close_pipes(struct urtw_softc *sc)
{
	usbd_status error = 0;

	if (sc->sc_rxpipe != NULL) {
		error = usbd_close_pipe(sc->sc_rxpipe);
		if (error != 0)
			goto fail;
		sc->sc_rxpipe = NULL;
	}
	if (sc->sc_txpipe_low != NULL) {
		error = usbd_close_pipe(sc->sc_txpipe_low);
		if (error != 0)
			goto fail;
		sc->sc_txpipe_low = NULL;
	}
	if (sc->sc_txpipe_normal != NULL) {
		error = usbd_close_pipe(sc->sc_txpipe_normal);
		if (error != 0)
			goto fail;
		sc->sc_txpipe_normal = NULL;
	}
fail:
	return (error);
}

usbd_status
urtw_open_pipes(struct urtw_softc *sc)
{
	usbd_status error;

	/*
	 * NB: there is no way to distinguish each pipes so we need to hardcode
	 * pipe numbers
	 */

	/* tx pipe - low priority packets */
	if (sc->sc_hwrev & URTW_HWREV_8187)
		error = usbd_open_pipe(sc->sc_iface, 0x2,
		    USBD_EXCLUSIVE_USE, &sc->sc_txpipe_low);
	else
		error = usbd_open_pipe(sc->sc_iface, 0x6,
		    USBD_EXCLUSIVE_USE, &sc->sc_txpipe_low);
	if (error != 0) {
		printf("%s: could not open Tx low pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}
	/* tx pipe - normal priority packets */
	if (sc->sc_hwrev & URTW_HWREV_8187)
		error = usbd_open_pipe(sc->sc_iface, 0x3,
		    USBD_EXCLUSIVE_USE, &sc->sc_txpipe_normal);
	else
		error = usbd_open_pipe(sc->sc_iface, 0x7,
		    USBD_EXCLUSIVE_USE, &sc->sc_txpipe_normal);
	if (error != 0) {
		printf("%s: could not open Tx normal pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}
	/* rx pipe */
	if (sc->sc_hwrev & URTW_HWREV_8187)
		error = usbd_open_pipe(sc->sc_iface, 0x81,
		    USBD_EXCLUSIVE_USE, &sc->sc_rxpipe);
	else
		error = usbd_open_pipe(sc->sc_iface, 0x83,
		    USBD_EXCLUSIVE_USE, &sc->sc_rxpipe);
	if (error != 0) {
		printf("%s: could not open Rx pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	return (0);
fail:
	(void)urtw_close_pipes(sc);
	return (error);
}

int
urtw_alloc_rx_data_list(struct urtw_softc *sc)
{
	int i, error;

	for (i = 0; i < URTW_RX_DATA_LIST_COUNT; i++) {
		struct urtw_rx_data *data = &sc->sc_rx_data[i];

		data->sc = sc;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate rx xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		if (usbd_alloc_buffer(data->xfer, URTW_RX_MAXSIZE) == NULL) {
			printf("%s: could not allocate rx buffer\n",
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
		MCLGET(data->m, M_DONTWAIT);
		if (!(data->m->m_flags & M_EXT)) {
			printf("%s: could not allocate rx mbuf cluster\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		data->buf = mtod(data->m, uint8_t *);
	}

	return (0);

fail:
	urtw_free_rx_data_list(sc);
	return (error);
}

void
urtw_free_rx_data_list(struct urtw_softc *sc)
{
	int i;

	/* Make sure no transfers are pending. */
	if (sc->sc_rxpipe != NULL)
		usbd_abort_pipe(sc->sc_rxpipe);

	for (i = 0; i < URTW_RX_DATA_LIST_COUNT; i++) {
		struct urtw_rx_data *data = &sc->sc_rx_data[i];

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
urtw_alloc_tx_data_list(struct urtw_softc *sc)
{
	int i, error;

	for (i = 0; i < URTW_TX_DATA_LIST_COUNT; i++) {
		struct urtw_tx_data *data = &sc->sc_tx_data[i];

		data->sc = sc;
		data->ni = NULL;

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate tx xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		data->buf = usbd_alloc_buffer(data->xfer, URTW_TX_MAXSIZE);
		if (data->buf == NULL) {
			printf("%s: could not allocate tx buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		if (((unsigned long)data->buf) % 4)
			printf("%s: warn: unaligned buffer %p\n",
			    sc->sc_dev.dv_xname, data->buf);
	}

	return (0);

fail:
	urtw_free_tx_data_list(sc);
	return (error);
}

void
urtw_free_tx_data_list(struct urtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int i;

	/* Make sure no transfers are pending. */
	if (sc->sc_txpipe_low != NULL)
		usbd_abort_pipe(sc->sc_txpipe_low);
	if (sc->sc_txpipe_normal != NULL)
		usbd_abort_pipe(sc->sc_txpipe_normal);

	for (i = 0; i < URTW_TX_DATA_LIST_COUNT; i++) {
		struct urtw_tx_data *data = &sc->sc_tx_data[i];

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
urtw_media_change(struct ifnet *ifp)
{
	struct urtw_softc *sc = ifp->if_softc;
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return (error);

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		error = sc->sc_init(ifp);

	return (error);
}

int
urtw_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct urtw_softc *sc = ic->ic_if.if_softc;

	usb_rem_task(sc->sc_udev, &sc->sc_task);
	timeout_del(&sc->scan_to);

	/* do it in a process context */
	sc->sc_state = nstate;
	sc->sc_arg = arg;
	usb_add_task(sc->sc_udev, &sc->sc_task);

	return (0);
}

usbd_status
urtw_led_init(struct urtw_softc *sc)
{
	uint32_t rev;
	usbd_status error;

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

usbd_status
urtw_8225_write_s16(struct urtw_softc *sc, uint8_t addr, int index,
    uint16_t data)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URTW_8187_SETREGS_REQ;
	USETW(req.wValue, addr);
	USETW(req.wIndex, index);
	USETW(req.wLength, sizeof(uint16_t));

	data = htole16(data);	
	return (usbd_do_request(sc->sc_udev, &req, &data));
}

usbd_status
urtw_8225_read(struct urtw_softc *sc, uint8_t addr, uint32_t *data)
{
	int i;
	int16_t bit;
	uint8_t rlen = 12, wlen = 6;
	uint16_t o1, o2, o3, tmp;
	uint32_t d2w = ((uint32_t)(addr & 0x1f)) << 27;
	uint32_t mask = 0x80000000, value = 0;
	usbd_status error;

	urtw_read16_m(sc, URTW_RF_PINS_OUTPUT, &o1);
	urtw_read16_m(sc, URTW_RF_PINS_ENABLE, &o2);
	urtw_read16_m(sc, URTW_RF_PINS_SELECT, &o3);
	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, o2 | 0xf);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, o3 | 0xf);
	o1 &= ~0xf;
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
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, 0x3a0);

	if (data != NULL)
		*data = value;
fail:
	return (error);
}

usbd_status
urtw_8225_write_c(struct urtw_softc *sc, uint8_t addr, uint16_t data)
{
	uint16_t d80, d82, d84;
	usbd_status error;

	urtw_read16_m(sc, URTW_RF_PINS_OUTPUT, &d80);
	d80 &= 0xfff3;
	urtw_read16_m(sc, URTW_RF_PINS_ENABLE, &d82);
	urtw_read16_m(sc, URTW_RF_PINS_SELECT, &d84);
	d84 &= 0xfff0;
	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, d82 | 0x0007);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, d84 | 0x0007);
	DELAY(10);

	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, d80 | URTW_BB_HOST_BANG_EN);
	DELAY(2);
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, d80);
	DELAY(10);

	error = urtw_8225_write_s16(sc, addr, 0x8225, data);
	if (error != 0)
		goto fail;

	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, d80 | URTW_BB_HOST_BANG_EN);
	DELAY(10);
	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, d80 | URTW_BB_HOST_BANG_EN);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, d84);
	usbd_delay_ms(sc->sc_udev, 2);
fail:
	return (error);
}

usbd_status
urtw_8225_isv2(struct urtw_softc *sc, int *ret)
{
	uint32_t data;
	usbd_status error;

	*ret = 1;

	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, 0x0080);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, 0x0080);
	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, 0x0080);
	usbd_delay_ms(sc->sc_udev, 500);

	urtw_8225_write(sc, 0x0, 0x1b7);

	error = urtw_8225_read(sc, 0x8, &data);
	if (error != 0)
		goto fail;
	if (data != 0x588)
		*ret = 0;
	else {
		error = urtw_8225_read(sc, 0x9, &data);
		if (error != 0)
			goto fail;
		if (data != 0x700)
			*ret = 0;
	}

	urtw_8225_write(sc, 0x0, 0xb7);
fail:
	return (error);
}

usbd_status
urtw_get_rfchip(struct urtw_softc *sc)
{
	struct urtw_rf *rf = &sc->sc_rf;
	int ret;
	uint32_t data;
	usbd_status error;

	rf->rf_sc = sc;

	if (sc->sc_hwrev & URTW_HWREV_8187) {
		error = urtw_eprom_read32(sc, URTW_EPROM_RFCHIPID, &data);
		if (error != 0)
			goto fail;
		switch (data & 0xff) {
		case URTW_EPROM_RFCHIPID_RTL8225U:
			error = urtw_8225_isv2(sc, &ret);
			if (error != 0)
				goto fail;
			if (ret == 0) {
				rf->init = urtw_8225_rf_init;
				rf->set_chan = urtw_8225_rf_set_chan;
				rf->set_sens = urtw_8225_rf_set_sens;
				printf(", RFv1");
			} else {
				rf->init = urtw_8225v2_rf_init;
				rf->set_chan = urtw_8225v2_rf_set_chan;
				rf->set_sens = NULL;
				printf(", RFv2");
			}
			break;
		default:
			goto fail;
		}
	} else {
		rf->init = urtw_8225v2_b_rf_init;
		rf->set_chan = urtw_8225v2_b_rf_set_chan;
		rf->set_sens = NULL;
	}

	rf->max_sens = URTW_8225_RF_MAX_SENS;
	rf->sens = URTW_8225_RF_DEF_SENS;

	return (0);

fail:
	printf("unsupported RF chip %d", data & 0xff);
	return (error);
}

usbd_status
urtw_get_txpwr(struct urtw_softc *sc)
{
	int i, j;
	uint32_t data;
	usbd_status error;

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
	if (sc->sc_hwrev & URTW_HWREV_8187) {
		for (i = 1, j = 0; i < 4; i += 2, j++) {
			error = urtw_eprom_read32(sc, URTW_EPROM_TXPW2 + j,
			    &data);
			if (error != 0)
				goto fail;
			sc->sc_txpwr_cck[i + 6 + 4] = data & 0xf;
			sc->sc_txpwr_cck[i + 6 + 4 + 1] = (data & 0xf00) >> 8;
			sc->sc_txpwr_ofdm[i + 6 + 4] = (data & 0xf0) >> 4;
			sc->sc_txpwr_ofdm[i + 6 + 4 + 1] =
			    (data & 0xf000) >> 12;
		}
	} else {
		/* Channel 11. */
		error = urtw_eprom_read32(sc, 0x1b, &data);
		if (error != 0)
			goto fail;
		sc->sc_txpwr_cck[11] = data & 0xf;
		sc->sc_txpwr_ofdm[11] = (data & 0xf0) >> 4;

		/* Channel 12. */
		error = urtw_eprom_read32(sc, 0xa, &data);
		if (error != 0)
			goto fail;
		sc->sc_txpwr_cck[12] = data & 0xf;
		sc->sc_txpwr_ofdm[12] = (data & 0xf0) >> 4;

		/* Channel 13, 14. */
		error = urtw_eprom_read32(sc, 0x1c, &data);
		if (error != 0)
			goto fail;
		sc->sc_txpwr_cck[13] = data & 0xf;
		sc->sc_txpwr_ofdm[13] = (data & 0xf0) >> 4;
		sc->sc_txpwr_cck[14] = (data & 0xf00) >> 8;
		sc->sc_txpwr_ofdm[14] = (data & 0xf000) >> 12;
	}
fail:
	return (error);
}

usbd_status
urtw_get_macaddr(struct urtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	usbd_status error;
	uint32_t data;

	error = urtw_eprom_read32(sc, URTW_EPROM_MACADDR, &data);
	if (error != 0)
		goto fail;
	ic->ic_myaddr[0] = data & 0xff;
	ic->ic_myaddr[1] = (data & 0xff00) >> 8;
	error = urtw_eprom_read32(sc, URTW_EPROM_MACADDR + 1, &data);
	if (error != 0)
		goto fail;
	ic->ic_myaddr[2] = data & 0xff;
	ic->ic_myaddr[3] = (data & 0xff00) >> 8;
	error = urtw_eprom_read32(sc, URTW_EPROM_MACADDR + 2, &data);
	if (error != 0)
		goto fail;
	ic->ic_myaddr[4] = data & 0xff;
	ic->ic_myaddr[5] = (data & 0xff00) >> 8;
fail:
	return (error);
}

usbd_status
urtw_eprom_read32(struct urtw_softc *sc, uint32_t addr, uint32_t *data)
{
#define URTW_READCMD_LEN		3
	int addrlen, i;
	int16_t addrstr[8], data16, readcmd[] = { 1, 1, 0 };
	usbd_status error;

	/* NB: make sure the buffer is initialized */
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

usbd_status
urtw_eprom_readbit(struct urtw_softc *sc, int16_t *data)
{
	uint8_t data8;
	usbd_status error;

	urtw_read8_m(sc, URTW_EPROM_CMD, &data8);
	*data = (data8 & URTW_EPROM_READBIT) ? 1 : 0;
	DELAY(URTW_EPROM_DELAY);

fail:
	return (error);
}

usbd_status
urtw_eprom_sendbits(struct urtw_softc *sc, int16_t *buf, int buflen)
{
	int i = 0;
	usbd_status error = 0;

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

usbd_status
urtw_eprom_writebit(struct urtw_softc *sc, int16_t bit)
{
	uint8_t data;
	usbd_status error;

	urtw_read8_m(sc, URTW_EPROM_CMD, &data);
	if (bit != 0)
		urtw_write8_m(sc, URTW_EPROM_CMD, data | URTW_EPROM_WRITEBIT);
	else
		urtw_write8_m(sc, URTW_EPROM_CMD, data & ~URTW_EPROM_WRITEBIT);
	DELAY(URTW_EPROM_DELAY);
fail:
	return (error);
}

usbd_status
urtw_eprom_ck(struct urtw_softc *sc)
{
	uint8_t data;
	usbd_status error;

	/* masking */
	urtw_read8_m(sc, URTW_EPROM_CMD, &data);
	urtw_write8_m(sc, URTW_EPROM_CMD, data | URTW_EPROM_CK);
	DELAY(URTW_EPROM_DELAY);
	/* unmasking */
	urtw_read8_m(sc, URTW_EPROM_CMD, &data);
	urtw_write8_m(sc, URTW_EPROM_CMD, data & ~URTW_EPROM_CK);
	DELAY(URTW_EPROM_DELAY);
fail:
	return (error);
}

usbd_status
urtw_eprom_cs(struct urtw_softc *sc, int able)
{
	uint8_t data;
	usbd_status error;

	urtw_read8_m(sc, URTW_EPROM_CMD, &data);
	if (able == URTW_EPROM_ENABLE)
		urtw_write8_m(sc, URTW_EPROM_CMD, data | URTW_EPROM_CS);
	else
		urtw_write8_m(sc, URTW_EPROM_CMD, data & ~URTW_EPROM_CS);
	DELAY(URTW_EPROM_DELAY);
fail:
	return (error);
}

usbd_status
urtw_read8_c(struct urtw_softc *sc, int val, uint8_t *data, uint8_t idx)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = URTW_8187_GETREGS_REQ;
	USETW(req.wValue, val | 0xff00);
	USETW(req.wIndex, idx & 0x03);
	USETW(req.wLength, sizeof(uint8_t));

	error = usbd_do_request(sc->sc_udev, &req, data);
	return (error);
}

usbd_status
urtw_read8e(struct urtw_softc *sc, int val, uint8_t *data)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = URTW_8187_GETREGS_REQ;
	USETW(req.wValue, val | 0xfe00);
	USETW(req.wIndex, 0);
	USETW(req.wLength, sizeof(uint8_t));

	error = usbd_do_request(sc->sc_udev, &req, data);
	return (error);
}

usbd_status
urtw_read16_c(struct urtw_softc *sc, int val, uint16_t *data, uint8_t idx)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = URTW_8187_GETREGS_REQ;
	USETW(req.wValue, val | 0xff00);
	USETW(req.wIndex, idx & 0x03);
	USETW(req.wLength, sizeof(uint16_t));

	error = usbd_do_request(sc->sc_udev, &req, data);
	*data = letoh16(*data);
	return (error);
}

usbd_status
urtw_read32_c(struct urtw_softc *sc, int val, uint32_t *data, uint8_t idx)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = URTW_8187_GETREGS_REQ;
	USETW(req.wValue, val | 0xff00);
	USETW(req.wIndex, idx & 0x03);
	USETW(req.wLength, sizeof(uint32_t));

	error = usbd_do_request(sc->sc_udev, &req, data);
	*data = letoh32(*data);
	return (error);
}

usbd_status
urtw_write8_c(struct urtw_softc *sc, int val, uint8_t data, uint8_t idx)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URTW_8187_SETREGS_REQ;
	USETW(req.wValue, val | 0xff00);
	USETW(req.wIndex, idx & 0x03);
	USETW(req.wLength, sizeof(uint8_t));

	return (usbd_do_request(sc->sc_udev, &req, &data));
}

usbd_status
urtw_write8e(struct urtw_softc *sc, int val, uint8_t data)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URTW_8187_SETREGS_REQ;
	USETW(req.wValue, val | 0xfe00);
	USETW(req.wIndex, 0);
	USETW(req.wLength, sizeof(uint8_t));

	return (usbd_do_request(sc->sc_udev, &req, &data));
}

usbd_status
urtw_write16_c(struct urtw_softc *sc, int val, uint16_t data, uint8_t idx)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URTW_8187_SETREGS_REQ;
	USETW(req.wValue, val | 0xff00);
	USETW(req.wIndex, idx & 0x03);
	USETW(req.wLength, sizeof(uint16_t));

	data = htole16(data);	
	return (usbd_do_request(sc->sc_udev, &req, &data));
}

usbd_status
urtw_write32_c(struct urtw_softc *sc, int val, uint32_t data, uint8_t idx)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URTW_8187_SETREGS_REQ;
	USETW(req.wValue, val | 0xff00);
	USETW(req.wIndex, idx & 0x03);
	USETW(req.wLength, sizeof(uint32_t));

	data = htole32(data);	
	return (usbd_do_request(sc->sc_udev, &req, &data));
}

static usbd_status
urtw_set_mode(struct urtw_softc *sc, uint32_t mode)
{
	uint8_t data;
	usbd_status error;

	urtw_read8_m(sc, URTW_EPROM_CMD, &data);
	data = (data & ~URTW_EPROM_CMD_MASK) | (mode << URTW_EPROM_CMD_SHIFT);
	data = data & ~(URTW_EPROM_CS | URTW_EPROM_CK);
	urtw_write8_m(sc, URTW_EPROM_CMD, data);
fail:
	return (error);
}

usbd_status
urtw_8180_set_anaparam(struct urtw_softc *sc, uint32_t val)
{
	uint8_t data;
	usbd_status error;

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

usbd_status
urtw_8185_set_anaparam2(struct urtw_softc *sc, uint32_t val)
{
	uint8_t data;
	usbd_status error;

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

usbd_status
urtw_intr_disable(struct urtw_softc *sc)
{
	usbd_status error;

	urtw_write16_m(sc, URTW_INTR_MASK, 0);
fail:
	return (error);
}

usbd_status
urtw_reset(struct urtw_softc *sc)
{
	uint8_t data;
	usbd_status error;

	error = urtw_8180_set_anaparam(sc, URTW_8187_8225_ANAPARAM_ON);
	if (error)
		goto fail;
	error = urtw_8185_set_anaparam2(sc, URTW_8187_8225_ANAPARAM2_ON);
	if (error)
		goto fail;

	error = urtw_intr_disable(sc);
	if (error)
		goto fail;
	usbd_delay_ms(sc->sc_udev, 100);

	error = urtw_write8e(sc, 0x18, 0x10);
	if (error != 0)
		goto fail;
	error = urtw_write8e(sc, 0x18, 0x11);
	if (error != 0)
		goto fail;
	error = urtw_write8e(sc, 0x18, 0x00);
	if (error != 0)
		goto fail;
	usbd_delay_ms(sc->sc_udev, 100);

	urtw_read8_m(sc, URTW_CMD, &data);
	data = (data & 2) | URTW_CMD_RST;
	urtw_write8_m(sc, URTW_CMD, data);
	usbd_delay_ms(sc->sc_udev, 100);

	urtw_read8_m(sc, URTW_CMD, &data);
	if (data & URTW_CMD_RST) {
		printf("%s: reset timeout\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	error = urtw_set_mode(sc, URTW_EPROM_CMD_LOAD);
	if (error)
		goto fail;
	usbd_delay_ms(sc->sc_udev, 100);

	error = urtw_8180_set_anaparam(sc, URTW_8187_8225_ANAPARAM_ON);
	if (error)
		goto fail;
	error = urtw_8185_set_anaparam2(sc, URTW_8187_8225_ANAPARAM2_ON);
	if (error)
		goto fail;
fail:
	return (error);
}

usbd_status
urtw_led_on(struct urtw_softc *sc, int type)
{
	usbd_status error = 0;

	if (type == URTW_LED_GPIO) {
		switch (sc->sc_gpio_ledpin) {
		case URTW_LED_PIN_GPIO0:
			urtw_write8_m(sc, URTW_GPIO, 0x01);
			urtw_write8_m(sc, URTW_GP_ENABLE, 0x00);
			break;
		default:
			break;
		}
	}

	sc->sc_gpio_ledon = 1;
fail:
	return (error);
}

static usbd_status
urtw_led_off(struct urtw_softc *sc, int type)
{
	usbd_status error = 0;

	if (type == URTW_LED_GPIO) {
		switch (sc->sc_gpio_ledpin) {
		case URTW_LED_PIN_GPIO0:
			urtw_write8_m(sc, URTW_GPIO, 0x01);
			urtw_write8_m(sc, URTW_GP_ENABLE, 0x01);
			break;
		default:
			break;
		}
	}

	sc->sc_gpio_ledon = 0;

fail:
	return (error);
}

usbd_status
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
		break;
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
		if (!usbd_is_dying(sc->sc_udev))
			timeout_add_msec(&sc->sc_led_ch, 100);
		break;
	case URTW_LED_POWER_ON_BLINK:
		urtw_led_on(sc, URTW_LED_GPIO);
		usbd_delay_ms(sc->sc_udev, 100);
		urtw_led_off(sc, URTW_LED_GPIO);
		break;
	default:
		break;
	}
	return (0);
}

usbd_status
urtw_led_mode1(struct urtw_softc *sc, int mode)
{
	return (USBD_INVAL);
}

usbd_status
urtw_led_mode2(struct urtw_softc *sc, int mode)
{
	return (USBD_INVAL);
}

usbd_status
urtw_led_mode3(struct urtw_softc *sc, int mode)
{
	return (USBD_INVAL);
}

void
urtw_ledusbtask(void *arg)
{
	struct urtw_softc *sc = arg;

	if (sc->sc_strategy != URTW_SW_LED_MODE0)
		return;

	urtw_led_blink(sc);
}

void
urtw_ledtask(void *arg)
{
	struct urtw_softc *sc = arg;

	/*
	 * NB: to change a status of the led we need at least a sleep so we
	 * can't do it here
	 */
	usb_add_task(sc->sc_udev, &sc->sc_ledtask);
}

usbd_status
urtw_led_ctl(struct urtw_softc *sc, int mode)
{
	usbd_status error = 0;

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
		break;
	}

	return (error);
}

usbd_status
urtw_led_blink(struct urtw_softc *sc)
{
	uint8_t ing = 0;
	usbd_status error;

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
		if (!usbd_is_dying(sc->sc_udev))
			timeout_add_msec(&sc->sc_led_ch, 100);
		break;
	default:
		break;
	}
	return (0);
}

usbd_status
urtw_update_msr(struct urtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t data;
	usbd_status error;

	urtw_read8_m(sc, URTW_MSR, &data);
	data &= ~URTW_MSR_LINK_MASK;

	/* Should always be set. */
	if (sc->sc_hwrev & URTW_HWREV_8187B)
		data |= URTW_MSR_LINK_ENEDCA;

	if (sc->sc_state == IEEE80211_S_RUN) {
		switch (ic->ic_opmode) {
		case IEEE80211_M_STA:
		case IEEE80211_M_MONITOR:
			data |= URTW_MSR_LINK_STA;
			break;
		default:
			break;
		}
	} else
		data |= URTW_MSR_LINK_NONE;

	urtw_write8_m(sc, URTW_MSR, data);
fail:
	return (error);
}

uint16_t
urtw_rate2rtl(int rate)
{
	int i;

	for (i = 0; i < nitems(urtw_ratetable); i++) {
		if (rate == urtw_ratetable[i].reg)
			return (urtw_ratetable[i].val);
	}

	return (3);
}

uint16_t
urtw_rtl2rate(int rate)
{
	int i;

	for (i = 0; i < nitems(urtw_ratetable); i++) {
		if (rate == urtw_ratetable[i].val)
			return (urtw_ratetable[i].reg);
	}

	return (0);
}

usbd_status
urtw_set_rate(struct urtw_softc *sc)
{
	int i, basic_rate, min_rr_rate, max_rr_rate;
	uint16_t data;
	usbd_status error;

	basic_rate = urtw_rate2rtl(48);
	min_rr_rate = urtw_rate2rtl(12);
	max_rr_rate = urtw_rate2rtl(48);

	urtw_write8_m(sc, URTW_RESP_RATE,
	    max_rr_rate << URTW_RESP_MAX_RATE_SHIFT |
	    min_rr_rate << URTW_RESP_MIN_RATE_SHIFT);

	urtw_read16_m(sc, URTW_8187_BRSR, &data);
	data &= ~URTW_BRSR_MBR_8185;

	for (i = 0; i <= basic_rate; i++)
		data |= (1 << i);

	urtw_write16_m(sc, URTW_8187_BRSR, data);
fail:
	return (error);
}

usbd_status
urtw_intr_enable(struct urtw_softc *sc)
{
	usbd_status error;

	urtw_write16_m(sc, URTW_INTR_MASK, 0xffff);
fail:
	return (error);
}

usbd_status
urtw_rx_setconf(struct urtw_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t data;
	usbd_status error;

	urtw_read32_m(sc, URTW_RX, &data);
	data = data &~ URTW_RX_FILTER_MASK;
#if 0
	data = data | URTW_RX_FILTER_CTL;
#endif
	data = data | URTW_RX_FILTER_MNG | URTW_RX_FILTER_DATA;
	data = data | URTW_RX_FILTER_BCAST | URTW_RX_FILTER_MCAST;

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		data = data | URTW_RX_FILTER_ICVERR;
		data = data | URTW_RX_FILTER_PWR;
	}
	if (sc->sc_crcmon == 1 && ic->ic_opmode == IEEE80211_M_MONITOR)
		data = data | URTW_RX_FILTER_CRCERR;

	if (ic->ic_opmode == IEEE80211_M_MONITOR ||
	    (ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC))) {
		data = data | URTW_RX_FILTER_ALLMAC;
	} else {
		data = data | URTW_RX_FILTER_NICMAC;
		data = data | URTW_RX_CHECK_BSSID;
	}

	data = data &~ URTW_RX_FIFO_THRESHOLD_MASK;
	data = data | URTW_RX_FIFO_THRESHOLD_NONE | URTW_RX_AUTORESETPHY;
	data = data &~ URTW_MAX_RX_DMA_MASK;
	data = data | URTW_MAX_RX_DMA_2048 | URTW_RCR_ONLYERLPKT;

	urtw_write32_m(sc, URTW_RX, data);
fail:
	return (error);
}

usbd_status
urtw_rx_enable(struct urtw_softc *sc)
{
	int i;
	struct urtw_rx_data *rx_data;
	uint8_t data;
	usbd_status error;

	/*
	 * Start up the receive pipe.
	 */
	for (i = 0; i < URTW_RX_DATA_LIST_COUNT; i++) {
		rx_data = &sc->sc_rx_data[i];

		usbd_setup_xfer(rx_data->xfer, sc->sc_rxpipe, rx_data,
		    rx_data->buf, MCLBYTES, USBD_SHORT_XFER_OK,
		    USBD_NO_TIMEOUT, urtw_rxeof);
		error = usbd_transfer(rx_data->xfer);
		if (error != USBD_IN_PROGRESS && error != 0) {
			printf("%s: could not queue Rx transfer\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}

	error = urtw_rx_setconf(sc);
	if (error != 0)
		goto fail;

	urtw_read8_m(sc, URTW_CMD, &data);
	urtw_write8_m(sc, URTW_CMD, data | URTW_CMD_RX_ENABLE);
fail:
	return (error);
}

usbd_status
urtw_tx_enable(struct urtw_softc *sc)
{
	uint8_t data8;
	uint32_t data;
	usbd_status error;

	if (sc->sc_hwrev & URTW_HWREV_8187) {
		urtw_read8_m(sc, URTW_CW_CONF, &data8);
		data8 &= ~(URTW_CW_CONF_PERPACKET_CW |
		    URTW_CW_CONF_PERPACKET_RETRY);
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
	} else {
		data = URTW_TX_DURPROCMODE | URTW_TX_DISREQQSIZE |
		    URTW_TX_MXDMA_2048 | URTW_TX_SHORTRETRY |
		    URTW_TX_LONGRETRY;
		urtw_write32_m(sc, URTW_TX_CONF, data);
	}

	urtw_read8_m(sc, URTW_CMD, &data8);
	urtw_write8_m(sc, URTW_CMD, data8 | URTW_CMD_TX_ENABLE);
fail:
	return (error);
}

int
urtw_init(struct ifnet *ifp)
{
	struct urtw_softc *sc = ifp->if_softc;
	struct urtw_rf *rf = &sc->sc_rf;
	struct ieee80211com *ic = &sc->sc_ic;
	usbd_status error;

	urtw_stop(ifp, 0);

	error = urtw_reset(sc);
	if (error)
		goto fail;

	urtw_write8_m(sc, 0x85, 0);
	urtw_write8_m(sc, URTW_GPIO, 0);

	/* for led */
	urtw_write8_m(sc, 0x85, 4);
	error = urtw_led_ctl(sc, URTW_LED_CTL_POWER_ON);
	if (error != 0)
		goto fail;

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;

	/* applying MAC address again. */
	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	error = urtw_set_macaddr(sc, ic->ic_myaddr);
	if (error)
		goto fail;
	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	error = urtw_update_msr(sc);
	if (error)
		goto fail;

	urtw_write32_m(sc, URTW_INT_TIMEOUT, 0);
	urtw_write8_m(sc, URTW_WPA_CONFIG, 0);
	urtw_write8_m(sc, URTW_RATE_FALLBACK, 0x81);
	error = urtw_set_rate(sc);
	if (error != 0)
		goto fail;

	error = rf->init(rf);
	if (error != 0)
		goto fail;
	if (rf->set_sens != NULL)
		rf->set_sens(rf);

	urtw_write16_m(sc, 0x5e, 1);
	urtw_write16_m(sc, 0xfe, 0x10);
	urtw_write8_m(sc, URTW_TALLY_SEL, 0x80);
	urtw_write8_m(sc, 0xff, 0x60);
	urtw_write16_m(sc, 0x5e, 0);
	urtw_write8_m(sc, 0x85, 4);

	error = urtw_intr_enable(sc);
	if (error != 0)
		goto fail;

	/* reset softc variables */
	sc->sc_txidx = sc->sc_tx_low_queued = sc->sc_tx_normal_queued = 0;
	sc->sc_txtimer = 0;

	if (!(sc->sc_flags & URTW_INIT_ONCE)) {
		error = urtw_open_pipes(sc);
		if (error != 0)
			goto fail;
		error = urtw_alloc_rx_data_list(sc);
		if (error != 0)
			goto fail;
		error = urtw_alloc_tx_data_list(sc);
		if (error != 0)
			goto fail;
		sc->sc_flags |= URTW_INIT_ONCE;
	}

	error = urtw_rx_enable(sc);
	if (error != 0)
		goto fail;
	error = urtw_tx_enable(sc);
	if (error != 0)
		goto fail;

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags |= IFF_RUNNING;

	ifp->if_timer = 1;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return (0);
fail:
	return (error);
}

void
urtw_set_multi(struct urtw_softc *sc)
{
	struct arpcom *ac = &sc->sc_ic.ic_ac;
	struct ifnet *ifp = &ac->ac_if;

	/*
	 * XXX don't know how to set a device.  Lack of docs.  Just try to set
	 * IFF_ALLMULTI flag here.
	 */
	ifp->if_flags |= IFF_ALLMULTI;
}

int
urtw_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct urtw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifreq *ifr;
	int s, error = 0;

	if (usbd_is_dying(sc->sc_udev))
		return (ENXIO);

	usbd_ref_incr(sc->sc_udev);

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
				urtw_set_multi(sc);
			} else {
				if (!(ifp->if_flags & IFF_RUNNING))
					sc->sc_init(ifp);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				urtw_stop(ifp, 1);
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
				urtw_set_multi(sc);
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
			urtw_set_chan(sc, ic->ic_ibss_chan);
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_RUNNING | IFF_UP)) ==
		    (IFF_RUNNING | IFF_UP))
			sc->sc_init(ifp);
		error = 0;
	}

	splx(s);

	usbd_ref_decr(sc->sc_udev);

	return (error);
}

void
urtw_start(struct ifnet *ifp)
{
	struct urtw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m0;

	/*
	 * net80211 may still try to send management frames even if the
	 * IFF_RUNNING flag is not set...
	 */
	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->sc_tx_low_queued >= URTW_TX_DATA_LIST_COUNT ||
		    sc->sc_tx_normal_queued >= URTW_TX_DATA_LIST_COUNT) {
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
			if (urtw_tx_start(sc, ni, m0, URTW_PRIORITY_NORMAL)
			    != 0)
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
			if (urtw_tx_start(sc, ni, m0, URTW_PRIORITY_NORMAL)
			    != 0) {
				if (ni != NULL)
					ieee80211_release_node(ic, ni);
				ifp->if_oerrors++;
				break;
			}
		}
		sc->sc_txtimer = 5;
	}
}

void
urtw_watchdog(struct ifnet *ifp)
{
	struct urtw_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_txtimer > 0) {
		if (--sc->sc_txtimer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

void
urtw_txeof_low(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct urtw_tx_data *data = priv;
	struct urtw_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		printf("%s: could not transmit buffer: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(status));

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_txpipe_low);

		ifp->if_oerrors++;
		return;
	}

	s = splnet();

	ieee80211_release_node(ic, data->ni);
	data->ni = NULL;

	sc->sc_txtimer = 0;

	sc->sc_tx_low_queued--;
	ifq_clr_oactive(&ifp->if_snd);
	urtw_start(ifp);

	splx(s);
}

void
urtw_txeof_normal(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct urtw_tx_data *data = priv;
	struct urtw_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		printf("%s: could not transmit buffer: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(status));

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_txpipe_normal);

		ifp->if_oerrors++;
		return;
	}

	s = splnet();

	ieee80211_release_node(ic, data->ni);
	data->ni = NULL;

	sc->sc_txtimer = 0;

	sc->sc_tx_normal_queued--;
	ifq_clr_oactive(&ifp->if_snd);
	urtw_start(ifp);

	splx(s);
}

int
urtw_tx_start(struct urtw_softc *sc, struct ieee80211_node *ni, struct mbuf *m0,
    int prior)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct urtw_tx_data *data;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	usbd_status error;
	int xferlen;

	wh = mtod(m0, struct ieee80211_frame *);

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_get_txkey(ic, wh, ni);

		if ((m0 = ieee80211_encrypt(ic, m0, k)) == NULL)
			return (ENOBUFS);

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct urtw_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = 0;
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

	if (sc->sc_hwrev & URTW_HWREV_8187)
		xferlen = m0->m_pkthdr.len + 4 * 3;
	else
		xferlen = m0->m_pkthdr.len + 4 * 8;

	if ((0 == xferlen % 64) || (0 == xferlen % 512))
		xferlen += 1;

	data = &sc->sc_tx_data[sc->sc_txidx];
	sc->sc_txidx = (sc->sc_txidx + 1) % URTW_TX_DATA_LIST_COUNT;

	bzero(data->buf, URTW_TX_MAXSIZE);
	data->buf[0] = m0->m_pkthdr.len & 0xff;
	data->buf[1] = (m0->m_pkthdr.len & 0x0f00) >> 8;
	data->buf[1] |= (1 << 7);

	/* XXX sc_preamble_mode is always 2. */
	if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
	    (ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_PREAMBLE) &&
	    (sc->sc_preamble_mode == 1) && (sc->sc_currate != 0))
		data->buf[2] |= 1;
	if ((m0->m_pkthdr.len > ic->ic_rtsthreshold) &&
	    prior == URTW_PRIORITY_LOW)
		return ENOTSUP; /* TODO */
	if (wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG)
		data->buf[2] |= (1 << 1);
	/* RTS rate - 10 means we use a basic rate. */
	data->buf[2] |= (urtw_rate2rtl(2) << 3);
	/*
	 * XXX currently TX rate control depends on the rate value of
	 * RX descriptor because I don't know how to we can control TX rate
	 * in more smart way.  Please fix me you find a thing.
	 */
	data->buf[3] = sc->sc_currate;
	if (prior == URTW_PRIORITY_NORMAL) {
		if (IEEE80211_IS_MULTICAST(wh->i_addr1))
			data->buf[3] = urtw_rate2rtl(ni->ni_rates.rs_rates[0]);
		else if (ic->ic_fixed_rate != -1)
			data->buf[3] = urtw_rate2rtl(ic->ic_fixed_rate);
	}

	if (sc->sc_hwrev & URTW_HWREV_8187) {
		data->buf[8] = 3;		/* CW minimum */
		data->buf[8] |= (7 << 4);	/* CW maximum */
		data->buf[9] |= 11;		/* retry limitation */
		m_copydata(m0, 0, m0->m_pkthdr.len, &data->buf[12]);
	} else {
		data->buf[21] |= 11;		/* retry limitation */
		m_copydata(m0, 0, m0->m_pkthdr.len, &data->buf[32]);
	}

	data->ni = ni;

	/* mbuf is no longer needed. */
	m_freem(m0);

	usbd_setup_xfer(data->xfer,
	    (prior == URTW_PRIORITY_LOW) ? sc->sc_txpipe_low :
	    sc->sc_txpipe_normal, data, data->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, URTW_DATA_TIMEOUT,
	    (prior == URTW_PRIORITY_LOW) ? urtw_txeof_low : urtw_txeof_normal);
	error = usbd_transfer(data->xfer);
	if (error != USBD_IN_PROGRESS && error != USBD_NORMAL_COMPLETION) {
		printf("%s: could not send frame: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		return (EIO);
	}

	error = urtw_led_ctl(sc, URTW_LED_CTL_TX);
	if (error != 0)
		printf("%s: could not control LED (%d)\n",
		    sc->sc_dev.dv_xname, error);

	if (prior == URTW_PRIORITY_LOW)
		sc->sc_tx_low_queued++;
	else
		sc->sc_tx_normal_queued++;

	return (0);
}

usbd_status
urtw_8225_usb_init(struct urtw_softc *sc)
{
	uint8_t data;
	usbd_status error;

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

	usbd_delay_ms(sc->sc_udev, 500);
fail:
	return (error);
}

usbd_status
urtw_8185_rf_pins_enable(struct urtw_softc *sc)
{
	usbd_status error = 0;

	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, 0x1ff7);
fail:
	return (error);
}

usbd_status
urtw_8187_write_phy(struct urtw_softc *sc, uint8_t addr, uint32_t data)
{
	uint32_t phyw;
	usbd_status error;

	phyw = ((data << 8) | (addr | 0x80));
	urtw_write8_m(sc, 0x7f, ((phyw & 0xff000000) >> 24));
	urtw_write8_m(sc, 0x7e, ((phyw & 0x00ff0000) >> 16));
	urtw_write8_m(sc, 0x7d, ((phyw & 0x0000ff00) >> 8));
	urtw_write8_m(sc, 0x7c, ((phyw & 0x000000ff)));
	/*
	 * Delay removed from 8185 to 8187.
	 * usbd_delay_ms(sc->sc_udev, 1);
	 */
fail:
	return (error);
}

usbd_status
urtw_8187_write_phy_ofdm_c(struct urtw_softc *sc, uint8_t addr, uint32_t data)
{
	data = data & 0xff;
	return (urtw_8187_write_phy(sc, addr, data));
}

usbd_status
urtw_8187_write_phy_cck_c(struct urtw_softc *sc, uint8_t addr, uint32_t data)
{
	data = data & 0xff;
	return (urtw_8187_write_phy(sc, addr, data | 0x10000));
}

usbd_status
urtw_8225_setgain(struct urtw_softc *sc, int16_t gain)
{
	usbd_status error;

	urtw_8187_write_phy_ofdm(sc, 0x0d, urtw_8225_gain[gain * 4]);
	urtw_8187_write_phy_ofdm(sc, 0x1b, urtw_8225_gain[gain * 4 + 2]);
	urtw_8187_write_phy_ofdm(sc, 0x1d, urtw_8225_gain[gain * 4 + 3]);
	urtw_8187_write_phy_ofdm(sc, 0x23, urtw_8225_gain[gain * 4 + 1]);
fail:
	return (error);
}

usbd_status
urtw_8225_set_txpwrlvl(struct urtw_softc *sc, int chan)
{
	int i, idx, set;
	uint8_t *cck_pwltable;
	uint8_t cck_pwrlvl_max, ofdm_pwrlvl_min, ofdm_pwrlvl_max;
	uint8_t cck_pwrlvl = sc->sc_txpwr_cck[chan] & 0xff;
	uint8_t ofdm_pwrlvl = sc->sc_txpwr_ofdm[chan] & 0xff;
	usbd_status error;

	cck_pwrlvl_max = 11;
	ofdm_pwrlvl_max = 25;	/* 12 -> 25 */
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
	usbd_delay_ms(sc->sc_udev, 1);

	/* OFDM power setting */
	ofdm_pwrlvl = (ofdm_pwrlvl > (ofdm_pwrlvl_max - ofdm_pwrlvl_min)) ?
	    ofdm_pwrlvl_max : ofdm_pwrlvl + ofdm_pwrlvl_min;
	ofdm_pwrlvl = (ofdm_pwrlvl > 35) ? 35 : ofdm_pwrlvl;

	idx = ofdm_pwrlvl % 6;
	set = ofdm_pwrlvl / 6;

	error = urtw_8185_set_anaparam2(sc, URTW_8187_8225_ANAPARAM2_ON);
	if (error)
		goto fail;
	urtw_8187_write_phy_ofdm(sc, 2, 0x42);
	urtw_8187_write_phy_ofdm(sc, 6, 0);
	urtw_8187_write_phy_ofdm(sc, 8, 0);

	urtw_write8_m(sc, URTW_TX_GAIN_OFDM,
	    urtw_8225_tx_gain_cck_ofdm[set] >> 1);
	urtw_8187_write_phy_ofdm(sc, 0x5, urtw_8225_txpwr_ofdm[idx]);
	urtw_8187_write_phy_ofdm(sc, 0x7, urtw_8225_txpwr_ofdm[idx]);
	usbd_delay_ms(sc->sc_udev, 1);
fail:
	return (error);
}

usbd_status
urtw_8185_tx_antenna(struct urtw_softc *sc, uint8_t ant)
{
	usbd_status error;

	urtw_write8_m(sc, URTW_TX_ANTENNA, ant);
	usbd_delay_ms(sc->sc_udev, 1);
fail:
	return (error);
}

usbd_status
urtw_8225_rf_init(struct urtw_rf *rf)
{
	struct urtw_softc *sc = rf->rf_sc;
	int i;
	uint16_t data;
	usbd_status error;

	error = urtw_8180_set_anaparam(sc, URTW_8187_8225_ANAPARAM_ON);
	if (error)
		goto fail;

	error = urtw_8225_usb_init(sc);
	if (error)
		goto fail;

	urtw_write32_m(sc, URTW_RF_TIMING, 0x000a8008);
	urtw_read16_m(sc, URTW_8187_BRSR, &data);	/* XXX ??? */
	urtw_write16_m(sc, URTW_8187_BRSR, 0xffff);
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

	usbd_delay_ms(sc->sc_udev, 500);

	for (i = 0; i < nitems(urtw_8225_rf_part1); i++) {
		urtw_8225_write(sc, urtw_8225_rf_part1[i].reg,
		    urtw_8225_rf_part1[i].val);
	}
	usbd_delay_ms(sc->sc_udev, 50);
	urtw_8225_write(sc, 0x2, 0xc4d);
	usbd_delay_ms(sc->sc_udev, 200);
	urtw_8225_write(sc, 0x2, 0x44d);
	usbd_delay_ms(sc->sc_udev, 200);
	urtw_8225_write(sc, 0x0, 0x127);

	for (i = 0; i < nitems(urtw_8225_rxgain); i++) {
		urtw_8225_write(sc, 0x1, (uint8_t)(i + 1));
		urtw_8225_write(sc, 0x2, urtw_8225_rxgain[i]);
	}

	urtw_8225_write(sc, 0x0, 0x27);
	urtw_8225_write(sc, 0x0, 0x22f);

	for (i = 0; i < nitems(urtw_8225_agc); i++) {
		urtw_8187_write_phy_ofdm(sc, 0xb, urtw_8225_agc[i]);
		urtw_8187_write_phy_ofdm(sc, 0xa, (uint8_t)i + 0x80);
	}

	for (i = 0; i < nitems(urtw_8225_rf_part2); i++) {
		urtw_8187_write_phy_ofdm(sc, urtw_8225_rf_part2[i].reg,
		    urtw_8225_rf_part2[i].val);
		usbd_delay_ms(sc->sc_udev, 1);
	}

	error = urtw_8225_setgain(sc, 4);
	if (error)
		goto fail;

	for (i = 0; i < nitems(urtw_8225_rf_part3); i++) {
		urtw_8187_write_phy_cck(sc, urtw_8225_rf_part3[i].reg,
		    urtw_8225_rf_part3[i].val);
		usbd_delay_ms(sc->sc_udev, 1);
	}

	urtw_write8_m(sc, 0x5b, 0x0d);

	error = urtw_8225_set_txpwrlvl(sc, 1);
	if (error)
		goto fail;

	urtw_8187_write_phy_cck(sc, 0x10, 0x9b);
	usbd_delay_ms(sc->sc_udev, 1);
	urtw_8187_write_phy_ofdm(sc, 0x26, 0x90);
	usbd_delay_ms(sc->sc_udev, 1);

	/* TX ant A, 0x0 for B */
	error = urtw_8185_tx_antenna(sc, 0x3);
	if (error)
		goto fail;
	urtw_write32_m(sc, 0x94, 0x3dc00002);

	error = urtw_8225_rf_set_chan(rf, 1);
fail:
	return (error);
}

usbd_status
urtw_8225_rf_set_chan(struct urtw_rf *rf, int chan)
{
	struct urtw_softc *sc = rf->rf_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c = ic->ic_ibss_chan;
	usbd_status error;

	error = urtw_8225_set_txpwrlvl(sc, chan);
	if (error)
		goto fail;
	urtw_8225_write(sc, 0x7, urtw_8225_channel[chan]);
	usbd_delay_ms(sc->sc_udev, 10);

	urtw_write8_m(sc, URTW_SIFS, 0x22);

	if (sc->sc_state == IEEE80211_S_ASSOC &&
	    ic->ic_flags & IEEE80211_F_SHSLOT)
		urtw_write8_m(sc, URTW_SLOT, IEEE80211_DUR_DS_SHSLOT);
	else
		urtw_write8_m(sc, URTW_SLOT, IEEE80211_DUR_DS_SLOT);

	if (IEEE80211_IS_CHAN_G(c)) {
		urtw_write8_m(sc, URTW_DIFS, 0x14);
		urtw_write8_m(sc, URTW_8187_EIFS, 0x5b - 0x14);
		urtw_write8_m(sc, URTW_CW_VAL, 0x73);
	} else {
		urtw_write8_m(sc, URTW_DIFS, 0x24);
		urtw_write8_m(sc, URTW_8187_EIFS, 0x5b - 0x24);
		urtw_write8_m(sc, URTW_CW_VAL, 0xa5);
	}

fail:
	return (error);
}

usbd_status
urtw_8225_rf_set_sens(struct urtw_rf *rf)
{
	struct urtw_softc *sc = rf->rf_sc;
	usbd_status error;

	if (rf->sens > 6)
		return (-1);

	if (rf->sens > 4)
		urtw_8225_write(sc, 0x0c, 0x850);
	else
		urtw_8225_write(sc, 0x0c, 0x50);

	rf->sens = 6 - rf->sens;
	error = urtw_8225_setgain(sc, rf->sens);
	if (error)
		goto fail;

	urtw_8187_write_phy_cck(sc, 0x41, urtw_8225_threshold[rf->sens]);

fail:
	return (error);
}

void
urtw_stop(struct ifnet *ifp, int disable)
{
	struct urtw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t data;
	usbd_status error;

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

	timeout_del(&sc->scan_to);
	timeout_del(&sc->sc_led_ch);

	urtw_intr_disable(sc);
	urtw_read8_m(sc, URTW_CMD, &data);
	data &= ~URTW_CMD_TX_ENABLE;
	data &= ~URTW_CMD_RX_ENABLE;
	urtw_write8_m(sc, URTW_CMD, data);

	if (sc->sc_rxpipe != NULL)
		usbd_abort_pipe(sc->sc_rxpipe);
	if (sc->sc_txpipe_low != NULL)
		usbd_abort_pipe(sc->sc_txpipe_low);
	if (sc->sc_txpipe_normal != NULL)
		usbd_abort_pipe(sc->sc_txpipe_normal);

fail:
	return;
}

int
urtw_isbmode(uint16_t rate)
{
	rate = urtw_rtl2rate(rate);

	return (((rate <= 22 && rate != 12 && rate != 18) ||
	    rate == 44) ? (1) : (0));
}

void
urtw_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct urtw_rx_data *data = priv;
	struct urtw_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct ieee80211_rxinfo rxi;
	struct mbuf *m, *mnew;
	uint8_t *desc, quality, rate;
	int actlen, flen, len, nf, rssi, s;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_rxpipe);
		ifp->if_ierrors++;
		goto skip;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &actlen, NULL);
	if (actlen < URTW_MIN_RXBUFSZ) {
		ifp->if_ierrors++;
		goto skip;
	}

	if (sc->sc_hwrev & URTW_HWREV_8187)
		/* 4 dword and 4 byte CRC */
		len = actlen - (4 * 4);
	else
		/* 5 dword and 4 byte CRC */
		len = actlen - (4 * 5);

	desc = data->buf + len;
	flen = ((desc[1] & 0x0f) << 8) + (desc[0] & 0xff);
	if (flen > actlen) {
		ifp->if_ierrors++;
		goto skip;
	}

	rate = (desc[2] & 0xf0) >> 4;
	if (sc->sc_hwrev & URTW_HWREV_8187) {
		quality = desc[4] & 0xff;
		rssi = (desc[6] & 0xfe) >> 1;

		/* XXX correct? */
		if (!urtw_isbmode(rate)) {
			rssi = (rssi > 90) ? 90 : ((rssi < 25) ? 25 : rssi);
			rssi = ((90 - rssi) * 100) / 65;
		} else {
			rssi = (rssi > 90) ? 95 : ((rssi < 30) ? 30 : rssi);
			rssi = ((95 - rssi) * 100) / 65;
		}
	} else {
		quality = desc[12];
		rssi = 14 - desc[14] / 2;
	}

	MGETHDR(mnew, M_DONTWAIT, MT_DATA);
	if (mnew == NULL) {
		printf("%s: could not allocate rx mbuf\n",
		    sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		goto skip;
	}
	MCLGET(mnew, M_DONTWAIT);
	if (!(mnew->m_flags & M_EXT)) {
		printf("%s: could not allocate rx mbuf cluster\n",
		    sc->sc_dev.dv_xname);
		m_freem(mnew);
		ifp->if_ierrors++;
		goto skip;
	}

	m = data->m;
	data->m = mnew;
	data->buf = mtod(mnew, uint8_t *);

	/* finalize mbuf */
	m->m_pkthdr.len = m->m_len = flen - 4;

	s = splnet();

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct urtw_rx_radiotap_header *tap = &sc->sc_rxtap;

		/* XXX Are variables correct? */
		tap->wr_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);
		tap->wr_dbm_antsignal = (int8_t)rssi;

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_rxtap_len;
		mb.m_next = m;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_IN);
	}
#endif
	wh = mtod(m, struct ieee80211_frame *);
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_DATA)
		sc->sc_currate = (rate > 0) ? rate : sc->sc_currate;
	ni = ieee80211_find_rxnode(ic, wh);

	/* XXX correct? */
	if (!urtw_isbmode(rate)) {
		if (quality > 127)
			quality = 0;
		else if (quality < 27)
			quality = 100;
		else
			quality = 127 - quality;
	} else
		quality = (quality > 64) ? 0 : ((64 - quality) * 100) / 64;

	nf = quality;

	/* send the frame to the 802.11 layer */
	memset(&rxi, 0, sizeof(rxi));
	rxi.rxi_rssi = rssi;
	ieee80211_input(ifp, m, ni, &rxi);

	/* node is no longer needed */
	ieee80211_release_node(ic, ni);

	splx(s);

skip:	/* setup a new transfer */
	usbd_setup_xfer(xfer, sc->sc_rxpipe, data, data->buf, MCLBYTES,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, urtw_rxeof);
	(void)usbd_transfer(xfer);
}

usbd_status
urtw_8225v2_setgain(struct urtw_softc *sc, int16_t gain)
{
	uint8_t *gainp;
	usbd_status error;

	/* XXX for A? */
	gainp = urtw_8225v2_gain_bg;
	urtw_8187_write_phy_ofdm(sc, 0x0d, gainp[gain * 3]);
	usbd_delay_ms(sc->sc_udev, 1);
	urtw_8187_write_phy_ofdm(sc, 0x1b, gainp[gain * 3 + 1]);
	usbd_delay_ms(sc->sc_udev, 1);
	urtw_8187_write_phy_ofdm(sc, 0x1d, gainp[gain * 3 + 2]);
	usbd_delay_ms(sc->sc_udev, 1);
	urtw_8187_write_phy_ofdm(sc, 0x21, 0x17);
	usbd_delay_ms(sc->sc_udev, 1);
fail:
	return (error);
}

usbd_status
urtw_8225v2_set_txpwrlvl(struct urtw_softc *sc, int chan)
{
	int i;
	uint8_t *cck_pwrtable;
	uint8_t cck_pwrlvl_max = 15, ofdm_pwrlvl_max = 25, ofdm_pwrlvl_min = 10;
	uint8_t cck_pwrlvl = sc->sc_txpwr_cck[chan] & 0xff;
	uint8_t ofdm_pwrlvl = sc->sc_txpwr_ofdm[chan] & 0xff;
	usbd_status error;

	/* CCK power setting */
	cck_pwrlvl = (cck_pwrlvl > cck_pwrlvl_max) ? cck_pwrlvl_max : cck_pwrlvl;
	cck_pwrlvl += sc->sc_txpwr_cck_base;
	cck_pwrlvl = (cck_pwrlvl > 35) ? 35 : cck_pwrlvl;
	cck_pwrtable = (chan == 14) ? urtw_8225v2_txpwr_cck_ch14 :
	    urtw_8225v2_txpwr_cck;

	for (i = 0; i < 8; i++) {
		urtw_8187_write_phy_cck(sc, 0x44 + i, cck_pwrtable[i]);
	}
	urtw_write8_m(sc, URTW_TX_GAIN_CCK,
	    urtw_8225v2_tx_gain_cck_ofdm[cck_pwrlvl]);
	usbd_delay_ms(sc->sc_udev, 1);

	/* OFDM power setting */
	ofdm_pwrlvl = (ofdm_pwrlvl > (ofdm_pwrlvl_max - ofdm_pwrlvl_min)) ?
		ofdm_pwrlvl_max : ofdm_pwrlvl + ofdm_pwrlvl_min;
	ofdm_pwrlvl += sc->sc_txpwr_ofdm_base;
	ofdm_pwrlvl = (ofdm_pwrlvl > 35) ? 35 : ofdm_pwrlvl;

	error = urtw_8185_set_anaparam2(sc, URTW_8187_8225_ANAPARAM2_ON);
	if (error)
		goto fail;

	urtw_8187_write_phy_ofdm(sc, 2, 0x42);
	urtw_8187_write_phy_ofdm(sc, 5, 0x0);
	urtw_8187_write_phy_ofdm(sc, 6, 0x40);
	urtw_8187_write_phy_ofdm(sc, 7, 0x0);
	urtw_8187_write_phy_ofdm(sc, 8, 0x40);

	urtw_write8_m(sc, URTW_TX_GAIN_OFDM,
	    urtw_8225v2_tx_gain_cck_ofdm[ofdm_pwrlvl]);
	usbd_delay_ms(sc->sc_udev, 1);
fail:
	return (error);
}

usbd_status
urtw_8225v2_rf_init(struct urtw_rf *rf)
{
	struct urtw_softc *sc = rf->rf_sc;
	int i;
	uint16_t data;
	uint32_t data32;
	usbd_status error;

	error = urtw_8180_set_anaparam(sc, URTW_8187_8225_ANAPARAM_ON);
	if (error)
		goto fail;

	error = urtw_8225_usb_init(sc);
	if (error)
		goto fail;

	urtw_write32_m(sc, URTW_RF_TIMING, 0x000a8008);
	urtw_read16_m(sc, URTW_8187_BRSR, &data);	/* XXX ??? */
	urtw_write16_m(sc, URTW_8187_BRSR, 0xffff);
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

	usbd_delay_ms(sc->sc_udev, 1000);

	for (i = 0; i < nitems(urtw_8225v2_rf_part1); i++) {
		urtw_8225_write(sc, urtw_8225v2_rf_part1[i].reg,
		    urtw_8225v2_rf_part1[i].val);
		usbd_delay_ms(sc->sc_udev, 1);
	}
	usbd_delay_ms(sc->sc_udev, 50);

	urtw_8225_write(sc, 0x0, 0x1b7);

	for (i = 0; i < nitems(urtw_8225v2_rxgain); i++) {
		urtw_8225_write(sc, 0x1, (uint8_t)(i + 1));
		urtw_8225_write(sc, 0x2, urtw_8225v2_rxgain[i]);
	}

	urtw_8225_write(sc, 0x3, 0x2);
	urtw_8225_write(sc, 0x5, 0x4);
	urtw_8225_write(sc, 0x0, 0xb7);
	urtw_8225_write(sc, 0x2, 0xc4d);
	usbd_delay_ms(sc->sc_udev, 100);
	urtw_8225_write(sc, 0x2, 0x44d);
	usbd_delay_ms(sc->sc_udev, 100);

	error = urtw_8225_read(sc, 0x6, &data32);
	if (error != 0)
		goto fail;
	if (data32 != 0xe6)
		printf("%s: expect 0xe6!! (0x%x)\n", sc->sc_dev.dv_xname,
		    data32);
	if (!(data32 & 0x80)) {
		urtw_8225_write(sc, 0x02, 0x0c4d);
		usbd_delay_ms(sc->sc_udev, 200);
		urtw_8225_write(sc, 0x02, 0x044d);
		usbd_delay_ms(sc->sc_udev, 100);
		error = urtw_8225_read(sc, 0x6, &data32);
		if (error != 0)
			goto fail;
		if (!(data32 & 0x80))
			printf("%s: RF calibration failed\n",
			    sc->sc_dev.dv_xname);
	}
	usbd_delay_ms(sc->sc_udev, 100);

	urtw_8225_write(sc, 0x0, 0x2bf);
	for (i = 0; i < nitems(urtw_8225_agc); i++) {
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

	urtw_write8_m(sc, 0x5b, 0x0d);

	error = urtw_8225v2_set_txpwrlvl(sc, 1);
	if (error)
		goto fail;

	urtw_8187_write_phy_cck(sc, 0x10, 0x9b);
	urtw_8187_write_phy_ofdm(sc, 0x26, 0x90);

	/* TX ant A, 0x0 for B */
	error = urtw_8185_tx_antenna(sc, 0x3);
	if (error)
		goto fail;
	urtw_write32_m(sc, 0x94, 0x3dc00002);

	error = urtw_8225_rf_set_chan(rf, 1);
fail:
	return (error);
}

usbd_status
urtw_8225v2_rf_set_chan(struct urtw_rf *rf, int chan)
{
	struct urtw_softc *sc = rf->rf_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c = ic->ic_ibss_chan;
	usbd_status error;

	error = urtw_8225v2_set_txpwrlvl(sc, chan);
	if (error)
		goto fail;

	urtw_8225_write(sc, 0x7, urtw_8225_channel[chan]);
	usbd_delay_ms(sc->sc_udev, 10);

	urtw_write8_m(sc, URTW_SIFS, 0x22);

	if(sc->sc_state == IEEE80211_S_ASSOC &&
	    ic->ic_flags & IEEE80211_F_SHSLOT)
		urtw_write8_m(sc, URTW_SLOT, IEEE80211_DUR_DS_SHSLOT);
	else
		urtw_write8_m(sc, URTW_SLOT, IEEE80211_DUR_DS_SLOT);

	if (IEEE80211_IS_CHAN_G(c)) {
		urtw_write8_m(sc, URTW_DIFS, 0x14);
		urtw_write8_m(sc, URTW_8187_EIFS, 0x5b - 0x14);
		urtw_write8_m(sc, URTW_CW_VAL, 0x73);
	} else {
		urtw_write8_m(sc, URTW_DIFS, 0x24);
		urtw_write8_m(sc, URTW_8187_EIFS, 0x5b - 0x24);
		urtw_write8_m(sc, URTW_CW_VAL, 0xa5);
	}

fail:
	return (error);
}

void
urtw_set_chan(struct urtw_softc *sc, struct ieee80211_channel *c)
{
	struct urtw_rf *rf = &sc->sc_rf;
	struct ieee80211com *ic = &sc->sc_ic;
	usbd_status error = 0;
	uint32_t data;
	u_int chan;

	chan = ieee80211_chan2ieee(ic, c);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return;
	/*
	 * During changing the channel we need to temporary disable
	 * TX.
	 */
	urtw_read32_m(sc, URTW_TX_CONF, &data);
	data &= ~URTW_TX_LOOPBACK_MASK;
	urtw_write32_m(sc, URTW_TX_CONF, data | URTW_TX_LOOPBACK_MAC);
	error = rf->set_chan(rf, chan);
	if (error != 0) {
		printf("%s could not change the channel\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	usbd_delay_ms(sc->sc_udev, 10);
	urtw_write32_m(sc, URTW_TX_CONF, data | URTW_TX_LOOPBACK_NONE);

fail:	return;

}

void
urtw_next_scan(void *arg)
{
	struct urtw_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);

	usbd_ref_decr(sc->sc_udev);
}

void
urtw_task(void *arg)
{
	struct urtw_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	enum ieee80211_state ostate;
	usbd_status error = 0;

	if (usbd_is_dying(sc->sc_udev))
		return;

	ostate = ic->ic_state;

	switch (sc->sc_state) {
	case IEEE80211_S_INIT:
		if (ostate == IEEE80211_S_RUN) {
			/* turn link LED off */
			(void)urtw_led_off(sc, URTW_LED_GPIO);
		}
		break;

	case IEEE80211_S_SCAN:
		urtw_set_chan(sc, ic->ic_bss->ni_chan);
		if (!usbd_is_dying(sc->sc_udev))
			timeout_add_msec(&sc->scan_to, 200);
		break;

	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		urtw_set_chan(sc, ic->ic_bss->ni_chan);
		break;

	case IEEE80211_S_RUN:
		ni = ic->ic_bss;

		/* setting bssid. */
		error = urtw_set_bssid(sc, ni->ni_bssid);
		if (error != 0)
			goto fail;
		urtw_update_msr(sc);
		/* XXX maybe the below would be incorrect. */
		urtw_write16_m(sc, URTW_ATIM_WND, 2);
		urtw_write16_m(sc, URTW_ATIM_TR_ITV, 100);
		urtw_write16_m(sc, URTW_BEACON_INTERVAL, 0x64);
		urtw_write16_m(sc, URTW_BEACON_INTERVAL_TIME, 0x3ff);
		error = urtw_led_ctl(sc, URTW_LED_CTL_LINK);
		if (error != 0)
			printf("%s: could not control LED (%d)\n",
			    sc->sc_dev.dv_xname, error);
		break;
	}

	sc->sc_newstate(ic, sc->sc_state, sc->sc_arg);

fail:
	if (error != 0)
		DPRINTF(("%s: error processing RUN state.",
		    sc->sc_dev.dv_xname));
}

usbd_status
urtw_8187b_update_wmm(struct urtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c = ic->ic_ibss_chan;
	uint32_t data;
	uint8_t aifs, sifs, slot, ecwmin, ecwmax;
	usbd_status error;

	sifs = 0xa;
	if (IEEE80211_IS_CHAN_G(c))
		slot = 0x9;
	else
		slot = 0x14;

	aifs = (2 * slot) + sifs;
	ecwmin = 3;
	ecwmax = 7;

	data = ((uint32_t)aifs << 0) |		/* AIFS, offset 0 */
	    ((uint32_t)ecwmin << 8) |		/* ECW minimum, offset 8 */
	    ((uint32_t)ecwmax << 12);		/* ECW maximum, offset 16 */

	urtw_write32_m(sc, URTW_AC_VO, data);
	urtw_write32_m(sc, URTW_AC_VI, data);
	urtw_write32_m(sc, URTW_AC_BE, data);
	urtw_write32_m(sc, URTW_AC_BK, data);

fail:
	return (error);
}

usbd_status
urtw_8187b_reset(struct urtw_softc *sc)
{
	uint8_t data;
	usbd_status error;

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;

	urtw_read8_m(sc, URTW_CONFIG3, &data);
	urtw_write8_m(sc, URTW_CONFIG3, data | URTW_CONFIG3_ANAPARAM_WRITE |
		URTW_CONFIG3_GNT_SELECT);

	urtw_write32_m(sc, URTW_ANAPARAM2, URTW_8187B_8225_ANAPARAM2_ON);
	urtw_write32_m(sc, URTW_ANAPARAM, URTW_8187B_8225_ANAPARAM_ON);
	urtw_write8_m(sc, URTW_ANAPARAM3, URTW_8187B_8225_ANAPARAM3_ON);

	urtw_write8_m(sc, 0x61, 0x10);
	urtw_read8_m(sc, 0x62, &data);
	urtw_write8_m(sc, 0x62, data & ~(1 << 5));
	urtw_write8_m(sc, 0x62, data | (1 << 5));

	urtw_read8_m(sc, URTW_CONFIG3, &data);
	urtw_write8_m(sc, URTW_CONFIG3, data & ~URTW_CONFIG3_ANAPARAM_WRITE);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	urtw_read8_m(sc, URTW_CMD, &data);
	data = (data & 2) | URTW_CMD_RST;
	urtw_write8_m(sc, URTW_CMD, data);
	usbd_delay_ms(sc->sc_udev, 100);

	urtw_read8_m(sc, URTW_CMD, &data);
	if (data & URTW_CMD_RST) {
		printf("%s: reset timeout\n", sc->sc_dev.dv_xname);
		goto fail;
	}

fail:
	return (error);
}

int
urtw_8187b_init(struct ifnet *ifp)
{
	struct urtw_softc *sc = ifp->if_softc;
	struct urtw_rf *rf = &sc->sc_rf;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t data;
	usbd_status error;

	urtw_stop(ifp, 0);

	error = urtw_8187b_update_wmm(sc);
	if (error != 0)
		goto fail;
	error = urtw_8187b_reset(sc);
	if (error)
		goto fail;

	/* Applying MAC address again. */
	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;
	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	error = urtw_set_macaddr(sc, ic->ic_myaddr);
	if (error)
		goto fail;
	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	error = urtw_update_msr(sc);
	if (error)
		goto fail;

	error = rf->init(rf);
	if (error != 0)
		goto fail;

	urtw_write8_m(sc, URTW_CMD, URTW_CMD_TX_ENABLE |
		URTW_CMD_RX_ENABLE);
	error = urtw_intr_enable(sc);
	if (error != 0)
		goto fail;

	error = urtw_write8e(sc, 0x41, 0xf4);
	if (error != 0)
		goto fail;
	error = urtw_write8e(sc, 0x40, 0x00);
	if (error != 0)
		goto fail;
	error = urtw_write8e(sc, 0x42, 0x00);
	if (error != 0)
		goto fail;
	error = urtw_write8e(sc, 0x42, 0x01);
	if (error != 0)
		goto fail;
	error = urtw_write8e(sc, 0x40, 0x0f);
	if (error != 0)
		goto fail;
	error = urtw_write8e(sc, 0x42, 0x00);
	if (error != 0)
		goto fail;
	error = urtw_write8e(sc, 0x42, 0x01);
	if (error != 0)
		goto fail;

	urtw_read8_m(sc, 0xdb, &data);
	urtw_write8_m(sc, 0xdb, data | (1 << 2));
	urtw_write16_idx_m(sc, 0x72, 0x59fa, 3);
	urtw_write16_idx_m(sc, 0x74, 0x59d2, 3);
	urtw_write16_idx_m(sc, 0x76, 0x59d2, 3);
	urtw_write16_idx_m(sc, 0x78, 0x19fa, 3);
	urtw_write16_idx_m(sc, 0x7a, 0x19fa, 3);
	urtw_write16_idx_m(sc, 0x7c, 0x00d0, 3);
	urtw_write8_m(sc, 0x61, 0);
	urtw_write8_idx_m(sc, 0x80, 0x0f, 1);
	urtw_write8_idx_m(sc, 0x83, 0x03, 1);
	urtw_write8_m(sc, 0xda, 0x10);
	urtw_write8_idx_m(sc, 0x4d, 0x08, 2);

	urtw_write32_m(sc, URTW_HSSI_PARA, 0x0600321b);

	urtw_write16_idx_m(sc, 0xec, 0x0800, 1);

	urtw_write8_m(sc, URTW_ACM_CONTROL, 0);

	/* Reset softc variables. */
	sc->sc_txidx = sc->sc_tx_low_queued = sc->sc_tx_normal_queued = 0;
	sc->sc_txtimer = 0;

	if (!(sc->sc_flags & URTW_INIT_ONCE)) {
		error = urtw_open_pipes(sc);
		if (error != 0)
			goto fail;
		error = urtw_alloc_rx_data_list(sc);
		if (error != 0)
			goto fail;
		error = urtw_alloc_tx_data_list(sc);
		if (error != 0)
			goto fail;
		sc->sc_flags |= URTW_INIT_ONCE;
	}

	error = urtw_rx_enable(sc);
	if (error != 0)
		goto fail;
	error = urtw_tx_enable(sc);
	if (error != 0)
		goto fail;

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ifp->if_timer = 1;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

fail:
	return (error);
}

usbd_status
urtw_8225v2_b_config_mac(struct urtw_softc *sc)
{
	int i;
	usbd_status error;

	for (i = 0; i < nitems(urtw_8187b_regtbl); i++) {
		urtw_write8_idx_m(sc, urtw_8187b_regtbl[i].reg,
		    urtw_8187b_regtbl[i].val, urtw_8187b_regtbl[i].idx);
	}

	urtw_write16_m(sc, URTW_TID_AC_MAP, 0xfa50);
	urtw_write16_m(sc, URTW_INT_MIG, 0);

	urtw_write32_idx_m(sc, 0xf0, 0, 1);
	urtw_write32_idx_m(sc, 0xf4, 0, 1);
	urtw_write8_idx_m(sc, 0xf8, 0, 1);

	urtw_write32_m(sc, URTW_RF_TIMING, 0x00004001);

fail:
	return (error);
}

usbd_status
urtw_8225v2_b_init_rfe(struct urtw_softc *sc)
{
	usbd_status error;

	urtw_write16_m(sc, URTW_RF_PINS_OUTPUT, 0x0480);
	urtw_write16_m(sc, URTW_RF_PINS_SELECT, 0x2488);
	urtw_write16_m(sc, URTW_RF_PINS_ENABLE, 0x1fff);
	usbd_delay_ms(sc->sc_udev, 100);

fail:
	return (error);
}

usbd_status
urtw_8225v2_b_update_chan(struct urtw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c = ic->ic_ibss_chan;
	uint8_t aifs, difs, eifs, sifs, slot;
	usbd_status error;

	urtw_write8_m(sc, URTW_SIFS, 0x22);

	sifs = 0xa;
	if (IEEE80211_IS_CHAN_G(c)) {
		slot = 0x9;
		difs = 0x1c;
		eifs = 0x5b;
	} else {
		slot = 0x14;
		difs = 0x32;
		eifs = 0x5b;
	}
	aifs = (2 * slot) + sifs;

	urtw_write8_m(sc, URTW_SLOT, slot);

	urtw_write8_m(sc, URTW_AC_VO, aifs);
	urtw_write8_m(sc, URTW_AC_VI, aifs);
	urtw_write8_m(sc, URTW_AC_BE, aifs);
	urtw_write8_m(sc, URTW_AC_BK, aifs);

	urtw_write8_m(sc, URTW_DIFS, difs);
	urtw_write8_m(sc, URTW_8187B_EIFS, eifs);

fail:
	return (error);
}

usbd_status
urtw_8225v2_b_rf_init(struct urtw_rf *rf)
{
	struct urtw_softc *sc = rf->rf_sc;
	int i;
	uint8_t data;
	usbd_status error;

	/* Set up ACK rate, retry limit, TX AGC, TX antenna. */
	urtw_write16_m(sc, URTW_8187B_BRSR, 0x0fff);
	urtw_read8_m(sc, URTW_CW_CONF, &data);
	urtw_write8_m(sc, URTW_CW_CONF, data |
		URTW_CW_CONF_PERPACKET_RETRY);
	urtw_read8_m(sc, URTW_TX_AGC_CTL, &data);
	urtw_write8_m(sc, URTW_TX_AGC_CTL, data |
		URTW_TX_AGC_CTL_PERPACKET_GAIN |
		URTW_TX_AGC_CTL_PERPACKET_ANTSEL);

	/* Auto rate fallback control. */
	urtw_write16_idx_m(sc, URTW_ARFR, 0x0fff, 1);	/* 1M ~ 54M */
	urtw_read8_m(sc, URTW_RATE_FALLBACK, &data);
	urtw_write8_m(sc, URTW_RATE_FALLBACK, data |
		URTW_RATE_FALLBACK_ENABLE);

	urtw_write16_m(sc, URTW_BEACON_INTERVAL, 100);
	urtw_write16_m(sc, URTW_ATIM_WND, 2);
	urtw_write16_idx_m(sc, URTW_FEMR, 0xffff, 1);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;
	urtw_read8_m(sc, URTW_CONFIG1, &data);
	urtw_write8_m(sc, URTW_CONFIG1, (data & 0x3f) | 0x80);
	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	urtw_write8_m(sc, URTW_WPA_CONFIG, 0);
	urtw_8225v2_b_config_mac(sc);
	urtw_write16_idx_m(sc, URTW_RFSW_CTRL, 0x569a, 2);

	error = urtw_set_mode(sc, URTW_EPROM_CMD_CONFIG);
	if (error)
		goto fail;
	urtw_read8_m(sc, URTW_CONFIG3, &data);
	urtw_write8_m(sc, URTW_CONFIG3, data | URTW_CONFIG3_ANAPARAM_WRITE);
	error = urtw_set_mode(sc, URTW_EPROM_CMD_NORMAL);
	if (error)
		goto fail;

	urtw_8225v2_b_init_rfe(sc);

	for (i = 0; i < nitems(urtw_8225v2_b_rf); i++) {
		urtw_8225_write(sc, urtw_8225v2_b_rf[i].reg,
		    urtw_8225v2_b_rf[i].val);
	}

	for (i = 0; i < nitems(urtw_8225v2_rxgain); i++) {
		urtw_8225_write(sc, 0x1, (uint8_t)(i + 1));
		urtw_8225_write(sc, 0x2, urtw_8225v2_rxgain[i]);
	}

	urtw_8225_write(sc, 0x03, 0x080);
	urtw_8225_write(sc, 0x05, 0x004);
	urtw_8225_write(sc, 0x00, 0x0b7);
	urtw_8225_write(sc, 0x02, 0xc4d);
	urtw_8225_write(sc, 0x02, 0x44d);
	urtw_8225_write(sc, 0x00, 0x2bf);

	urtw_write8_m(sc, URTW_TX_GAIN_CCK, 0x03);
	urtw_write8_m(sc, URTW_TX_GAIN_OFDM, 0x07);
	urtw_write8_m(sc, URTW_TX_ANTENNA, 0x03);

	urtw_8187_write_phy_ofdm(sc, 0x80, 0x12);
	for (i = 0; i < nitems(urtw_8225v2_agc); i++) {
		urtw_8187_write_phy_ofdm(sc, 0x0f, urtw_8225v2_agc[i]);
		urtw_8187_write_phy_ofdm(sc, 0x0e, (uint8_t)i + 0x80);
		urtw_8187_write_phy_ofdm(sc, 0x0e, 0);
	}
	urtw_8187_write_phy_ofdm(sc, 0x80, 0x10);

	for (i = 0; i < nitems(urtw_8225v2_ofdm); i++)
		urtw_8187_write_phy_ofdm(sc, i, urtw_8225v2_ofdm[i]);

	urtw_8225v2_b_update_chan(sc);

	urtw_8187_write_phy_ofdm(sc, 0x97, 0x46);
	urtw_8187_write_phy_ofdm(sc, 0xa4, 0xb6);
	urtw_8187_write_phy_ofdm(sc, 0x85, 0xfc);
	urtw_8187_write_phy_cck(sc, 0xc1, 0x88);

	error = urtw_8225v2_b_rf_set_chan(rf, 1);
fail:
	return (error);
}

usbd_status
urtw_8225v2_b_rf_set_chan(struct urtw_rf *rf, int chan)
{
	struct urtw_softc *sc = rf->rf_sc;
	usbd_status error;

	error = urtw_8225v2_b_set_txpwrlvl(sc, chan);
	if (error)
		goto fail;

	urtw_8225_write(sc, 0x7, urtw_8225_channel[chan]);
	/*
	 * Delay removed from 8185 to 8187.
	 * usbd_delay_ms(sc->sc_udev, 10);
	 */

	urtw_write16_m(sc, URTW_AC_VO, 0x5114);
	urtw_write16_m(sc, URTW_AC_VI, 0x5114);
	urtw_write16_m(sc, URTW_AC_BE, 0x5114);
	urtw_write16_m(sc, URTW_AC_BK, 0x5114);

fail:
	return (error);
}

usbd_status
urtw_8225v2_b_set_txpwrlvl(struct urtw_softc *sc, int chan)
{
	int i;
	uint8_t *cck_pwrtable;
	uint8_t cck_pwrlvl_min, cck_pwrlvl_max, ofdm_pwrlvl_min,
	    ofdm_pwrlvl_max;
	int8_t cck_pwrlvl = sc->sc_txpwr_cck[chan] & 0xff;
	int8_t ofdm_pwrlvl = sc->sc_txpwr_ofdm[chan] & 0xff;
	usbd_status error;

	if (sc->sc_hwrev & URTW_HWREV_8187B_B) {
		cck_pwrlvl_min = 0;
		cck_pwrlvl_max = 15;
		ofdm_pwrlvl_min = 2;
		ofdm_pwrlvl_max = 17;
	} else {
		cck_pwrlvl_min = 7;
		cck_pwrlvl_max = 22;
		ofdm_pwrlvl_min = 10;
		ofdm_pwrlvl_max = 25;
	}

	/* CCK power setting */
	cck_pwrlvl = (cck_pwrlvl > (cck_pwrlvl_max - cck_pwrlvl_min)) ?
	    cck_pwrlvl_max : (cck_pwrlvl + cck_pwrlvl_min);

	cck_pwrlvl += sc->sc_txpwr_cck_base;
	cck_pwrlvl = (cck_pwrlvl > 35) ? 35 : cck_pwrlvl;
	cck_pwrlvl = (cck_pwrlvl < 0) ? 0 : cck_pwrlvl;

	cck_pwrtable = (chan == 14) ? urtw_8225v2_txpwr_cck_ch14 :
	    urtw_8225v2_txpwr_cck;

	if (sc->sc_hwrev & URTW_HWREV_8187B_B) {
		if (cck_pwrlvl <= 6)
			; /* do nothing */
		else if (cck_pwrlvl <= 11)
			cck_pwrtable += 8;
		else
			cck_pwrtable += 16;
	} else {
		if (cck_pwrlvl <= 5)
			; /* do nothing */
		else if (cck_pwrlvl <= 11)
			cck_pwrtable += 8;
		else if (cck_pwrlvl <= 17)
			cck_pwrtable += 16;
		else
			cck_pwrtable += 24;
	}

	for (i = 0; i < 8; i++) {
		urtw_8187_write_phy_cck(sc, 0x44 + i, cck_pwrtable[i]);
	}

	urtw_write8_m(sc, URTW_TX_GAIN_CCK,
	    urtw_8225v2_tx_gain_cck_ofdm[cck_pwrlvl] << 1);
	/*
	 * Delay removed from 8185 to 8187.
	 * usbd_delay_ms(sc->sc_udev, 1);
	 */

	/* OFDM power setting */
	ofdm_pwrlvl = (ofdm_pwrlvl > (ofdm_pwrlvl_max - ofdm_pwrlvl_min)) ?
	    ofdm_pwrlvl_max : ofdm_pwrlvl + ofdm_pwrlvl_min;

	ofdm_pwrlvl += sc->sc_txpwr_ofdm_base;
	ofdm_pwrlvl = (ofdm_pwrlvl > 35) ? 35 : ofdm_pwrlvl;
	ofdm_pwrlvl = (ofdm_pwrlvl < 0) ? 0 : ofdm_pwrlvl;

	urtw_write8_m(sc, URTW_TX_GAIN_OFDM,
	    urtw_8225v2_tx_gain_cck_ofdm[ofdm_pwrlvl] << 1);

	if (sc->sc_hwrev & URTW_HWREV_8187B_B) {
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

	/*
	 * Delay removed from 8185 to 8187.
	 * usbd_delay_ms(sc->sc_udev, 1);
	 */
fail:
	return (error);
}

int
urtw_set_bssid(struct urtw_softc *sc, const uint8_t *bssid)
{
	int error;

	urtw_write32_m(sc, URTW_BSSID,
	    bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24);
	urtw_write16_m(sc, URTW_BSSID + 4,
	    bssid[4] | bssid[5] << 8);

	return 0;

fail:
	return error;
}

int
urtw_set_macaddr(struct urtw_softc *sc, const uint8_t *addr)
{
	int error;

	urtw_write32_m(sc, URTW_MAC0,
	    addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	urtw_write16_m(sc, URTW_MAC4,
	    addr[4] | addr[5] << 8);

	return 0;

fail:
	return error;
}

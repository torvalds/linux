/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 *
 * $OpenBSD: if_urtwnreg.h,v 1.3 2010/11/16 18:02:59 damien Exp $
 * $FreeBSD$
 */

#ifndef R92C_RX_DESC_H
#define R92C_RX_DESC_H

/* Rx MAC descriptor (common parts / USB). */
struct r92c_rx_stat {
	uint32_t	rxdw0;
#define R92C_RXDW0_PKTLEN_M	0x00003fff
#define R92C_RXDW0_PKTLEN_S	0
#define R92C_RXDW0_CRCERR	0x00004000
#define R92C_RXDW0_ICVERR	0x00008000
#define R92C_RXDW0_INFOSZ_M	0x000f0000
#define R92C_RXDW0_INFOSZ_S	16
#define R92C_RXDW0_CIPHER_M	0x00700000
#define R92C_RXDW0_CIPHER_S	20
#define R92C_RXDW0_QOS		0x00800000
#define R92C_RXDW0_SHIFT_M	0x03000000
#define R92C_RXDW0_SHIFT_S	24
#define R92C_RXDW0_PHYST	0x04000000
#define R92C_RXDW0_SWDEC	0x08000000
#define R92C_RXDW0_LS		0x10000000
#define R92C_RXDW0_FS		0x20000000
#define R92C_RXDW0_EOR		0x40000000
#define R92C_RXDW0_OWN		0x80000000

	uint32_t	rxdw1;
#define R92C_RXDW1_MACID_M	0x0000001f
#define R92C_RXDW1_MACID_S	0
#define R92C_RXDW1_AMSDU	0x00002000
#define R92C_RXDW1_AMPDU_MORE	0x00004000
#define R92C_RXDW1_AMPDU	0x00008000
#define R92C_RXDW1_MC		0x40000000
#define R92C_RXDW1_BC		0x80000000

	uint32_t	rxdw2;
	uint32_t	rxdw3;
#define R92C_RXDW3_RATE_M	0x0000003f
#define R92C_RXDW3_RATE_S	0
#define R92C_RXDW3_HT		0x00000040
#define R92C_RXDW3_SPLCP	0x00000100
#define R92C_RXDW3_HT40		0x00000200
#define R92C_RXDW3_HTC		0x00000400
#define R92C_RXDW3_BSSID_FIT_M	0x00003000
#define R92C_RXDW3_BSSID_FIT_S	12

	uint32_t	rxdw4;
	uint32_t	tsf_low;
} __packed __attribute__((aligned(4)));

/* Rx PHY CCK descriptor. */
struct r92c_rx_cck {
	uint8_t		adc_pwdb[4];
	uint8_t		sq_rpt;
	uint8_t		agc_rpt;
} __packed;

/* Rx PHY descriptor. */
struct r92c_rx_phystat {
	uint8_t		trsw_gain[4];
	uint8_t		pwdb_all;
	uint8_t		cfosho[4];
	uint8_t		cfotail[4];
	uint8_t		rxevm[2];
	uint8_t		rxsnr[4];
	uint8_t		pdsnr[2];
	uint8_t		csi_current[2];
	uint8_t		csi_target[2];
	uint8_t		sigevm;
	uint8_t		max_ex_pwr;
	uint8_t		phy_byte28;
#define R92C_PHY_BYTE28_ANTSEL		0x01
#define R92C_PHY_BYTE28_ANTSEL_B	0x02
#define R92C_PHY_BYTE28_ANT_TRAIN_EN	0x04
#define R92C_PHY_BYTE28_IDLE_LONG	0x08
#define R92C_PHY_BYTE28_RXSC_M		0x30
#define R92C_PHY_BYTE28_RXSC_S		4
#define R92C_PHY_BYTE28_SGI_EN		0x40
#define R92C_PHY_BYTE28_EX_INTF_FLG	0x80
} __packed;

#endif	/* R92C_RX_DESC_H */

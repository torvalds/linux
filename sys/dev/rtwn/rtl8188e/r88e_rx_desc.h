/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#ifndef R88E_RX_DESC_H
#define R88E_RX_DESC_H

#include <dev/rtwn/rtl8192c/r92c_rx_desc.h>

/* Rx MAC descriptor defines (chip-specific). */
/* Rx dword 3 */
#define R88E_RXDW3_RPT_M	0x0000c000
#define R88E_RXDW3_RPT_S	14
#define R88E_RXDW3_RPT_RX	0
#define R88E_RXDW3_RPT_TX1	1
#define R88E_RXDW3_RPT_TX2	2
#define R88E_RXDW3_RPT_HIS	3

/* Rx PHY descriptor. */
struct r88e_rx_phystat {
	uint8_t		path_agc[2];
	uint8_t		chan;
	uint8_t		reserved1;
	uint8_t		sig_qual;
	uint8_t		agc_rpt;
	uint8_t		rpt_b;
	uint8_t		reserved2;
	uint8_t		noise_power;
	uint8_t		path_cfotail[2];
	uint8_t		pcts_mask[2];
	uint8_t		stream_rxevm[2];
	uint8_t		path_rxsnr[2];
	uint8_t		noise_power_db_lsb;
	uint8_t		reserved3[3];
	uint8_t		stream_csi[2];
	uint8_t		stream_target_csi[2];
	uint8_t		sig_evm;
} __packed;

/* Tx report (type 1). */
struct r88e_tx_rpt_ccx {
	uint8_t		rptb0;
#define R88E_RPTB6_PKT_NUM_M	0x0e
#define R88E_RPTB6_PKT_NUM_S	1
#define R88E_RPTB0_INT_CCX	0x80

	uint8_t		rptb1;
#define R88E_RPTB1_MACID_M	0x3f
#define R88E_RPTB1_MACID_S	0
#define R88E_RPTB1_PKT_OK	0x40
#define R88E_RPTB1_BMC		0x80

	uint8_t		rptb2;
#define R88E_RPTB2_RETRY_CNT_M	0x3f
#define R88E_RPTB2_RETRY_CNT_S	0
#define R88E_RPTB2_LIFE_EXPIRE	0x40
#define R88E_RPTB2_RETRY_OVER	0x80

	uint8_t		queue_time_low;
	uint8_t		queue_time_high;
	uint8_t		final_rate;
	uint8_t		rptb6;
#define R88E_RPTB6_QSEL_M	0xf0
#define R88E_RPTB6_QSEL_S	4

	uint8_t		rptb7;
} __packed;

/* Interrupt message format. */
/* XXX recheck */
struct r88e_intr_msg {
	uint8_t		c2h_id;
	uint8_t		c2h_seq;
	uint8_t		c2h_evt;
	uint8_t		reserved1[13];
	uint8_t		cpwm1;
	uint8_t		reserved2[3];
	uint8_t		cpwm2;
	uint8_t		reserved3[27];
	uint32_t	hisr;
	uint32_t	hisr_ex;
};

#endif	/* R88E_RX_DESC_H */

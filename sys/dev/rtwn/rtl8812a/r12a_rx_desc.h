/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef R12A_RX_DESC_H
#define R12A_RX_DESC_H

#include <dev/rtwn/rtl8192c/r92c_rx_desc.h>

/* Rx MAC descriptor defines (chip-specific). */
/* Rx dword 1 */
#define R12A_RXDW1_AMSDU	0x00002000
#define R12A_RXDW1_AMPDU	0x00008000
#define R12A_RXDW1_CKSUM_ERR	0x00100000
#define R12A_RXDW1_IPV6		0x00200000
#define R12A_RXDW1_UDP		0x00400000
#define R12A_RXDW1_CKSUM	0x00800000
/* Rx dword 2 */
#define R12A_RXDW2_RPT_C2H	0x10000000
/* Rx dword 3 */
#define R12A_RXDW3_RATE_M	0x0000007f
#define R12A_RXDW3_RATE_S	0
/* Rx dword 4 */
#define R12A_RXDW4_SPLCP	0x00000001
#define R12A_RXDW4_LDPC		0x00000002
#define R12A_RXDW4_STBC		0x00000004
#define R12A_RXDW4_BW_M		0x00000030
#define R12A_RXDW4_BW_S		4
#define R12A_RXDW4_BW20		0
#define R12A_RXDW4_BW40		1
#define R12A_RXDW4_BW80		2
#define R12A_RXDW4_BW160	3

/* Rx PHY descriptor. */
struct r12a_rx_phystat {
	uint8_t		gain_trsw[2];
	uint16_t	phyw1;
#define R12A_PHYW1_CHAN_M	0x03ff
#define R12A_PHYW1_CHAN_S	0
#define R12A_PHYW1_CHAN_EXT_M	0x3c00
#define R12A_PHYW1_CHAN_EXT_S	10
#define R12A_PHYW1_RFMOD_M	0xc000
#define R12A_PHYW1_RFMOD_S	14

	uint8_t		pwdb_all;
	uint8_t		cfosho[4];
	uint8_t		cfotail[4];
	uint8_t		rxevm[2];
	uint8_t		rxsnr[2];
	uint8_t		pcts_msk_rpt[2];
	uint8_t		pdsnr[2];
	uint8_t		csi_current[2];
	uint8_t		rx_gain_c;
	uint8_t		rx_gain_d;
	uint8_t		sigevm;
	uint16_t	phyw13;
#define R12A_PHYW13_ANTIDX_A_M	0x0700
#define R12A_PHYW13_ANTIDX_A_S	8
#define R12A_PHYW13_ANTIDX_B_M	0x3800
#define R12A_PHYW13_ANTIDX_B_S	11
} __packed;

#endif	/* R12A_RX_DESC_H */

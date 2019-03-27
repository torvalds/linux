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

#ifndef R12A_FW_CMD_H
#define R12A_FW_CMD_H

#include <dev/rtwn/rtl8188e/r88e_fw_cmd.h>

/*
 * Host to firmware commands.
 */
/* Note: some parts are shared with RTL8188EU. */
#define R12A_CMD_MSR_RPT		0x01
#define R12A_CMD_SET_PWRMODE		0x20
#define R12A_CMD_IQ_CALIBRATE		0x45

/* Structure for R12A_CMD_MSR_RPT. */
struct r12a_fw_cmd_msrrpt {
	uint8_t		msrb0;
#define R12A_MSRRPT_B0_DISASSOC		0x00
#define R12A_MSRRPT_B0_ASSOC		0x01
#define R12A_MSRRPT_B0_MACID_IND	0x02

	uint8_t		macid;
	uint8_t		macid_end;
} __packed;

/* Structure for R12A_CMD_SET_PWRMODE. */
struct r12a_fw_cmd_pwrmode {
	uint8_t		mode;
	uint8_t		pwrb1;
	uint8_t		bcn_pass;
	uint8_t		queue_uapsd;
	uint8_t		pwr_state;
	uint8_t		pwrb5;
#define R12A_PWRMODE_B5_NO_BTCOEX	0x40
} __packed;

/* Structure for R12A_CMD_IQ_CALIBRATE. */
struct r12a_fw_cmd_iq_calib {
	uint8_t		chan;
	uint8_t		band_bw;
#define RTWN_CMD_IQ_CHAN_WIDTH_20	0x01
#define RTWN_CMD_IQ_CHAN_WIDTH_40	0x02
#define RTWN_CMD_IQ_CHAN_WIDTH_80	0x04
#define RTWN_CMD_IQ_CHAN_WIDTH_160	0x08
#define RTWN_CMD_IQ_BAND_2GHZ		0x10
#define RTWN_CMD_IQ_BAND_5GHZ		0x20

	uint8_t		ext_5g_pa_lna;
#define RTWN_CMD_IQ_EXT_PA_5G(pa)	(pa)
#define RTWN_CMD_IQ_EXT_LNA_5G(lna)	((lna) << 1)
} __packed;


/*
 * C2H event types.
 */
#define R12A_C2H_DEBUG		0x00
#define R12A_C2H_TX_REPORT	0x03
#define R12A_C2H_BT_INFO	0x09
#define R12A_C2H_RA_REPORT	0x0c
#define R12A_C2H_IQK_FINISHED	0x11

/* Structure for R12A_C2H_TX_REPORT event. */
struct r12a_c2h_tx_rpt {
	uint8_t		txrptb0;
#define R12A_TXRPTB0_QSEL_M		0x1f
#define R12A_TXRPTB0_QSEL_S		0
#define R12A_TXRPTB0_BC			0x20
#define R12A_TXRPTB0_LIFE_EXPIRE	0x40
#define R12A_TXRPTB0_RETRY_OVER		0x80

	uint8_t		macid;
	uint8_t		txrptb2;
#define R12A_TXRPTB2_RETRY_CNT_M	0x3f
#define R12A_TXRPTB2_RETRY_CNT_S	0

	uint8_t		queue_time_low;	/* 256 msec unit */
	uint8_t		queue_time_high;
	uint8_t		final_rate;
	uint16_t	reserved;
} __packed;

/* Structure for R12A_C2H_RA_REPORT event. */
struct r12a_c2h_ra_report {
	uint8_t		rarptb0;
#define R12A_RARPTB0_RATE_M	0x3f
#define R12A_RARPTB0_RATE_S	0

	uint8_t		macid;
	uint8_t		rarptb2;
#define R12A_RARPTB0_LDPC	0x01
#define R12A_RARPTB0_TXBF	0x02
#define R12A_RARPTB0_NOISE	0x04
} __packed;

#endif	/* R12A_FW_CMD_H */

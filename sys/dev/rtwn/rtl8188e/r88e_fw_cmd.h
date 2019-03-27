/*-
 * Copyright (c) 2015 Kevin Lo <kevlo@FreeBSD.org>
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

#ifndef R88E_FW_CMD_H
#define R88E_FW_CMD_H

/*
 * Host to firmware commands.
 */
struct r88e_fw_cmd {
	uint8_t id;
#define R88E_CMD_RSVD_PAGE		0x00
#define R88E_CMD_MSR_RPT		0x01
#define R88E_CMD_SET_PWRMODE		0x20

	uint8_t msg[7];
} __packed __attribute__((aligned(4)));

/* Structure for R88E_CMD_RSVD_PAGE. */
struct r88e_fw_cmd_rsvdpage {
	uint8_t		probe_resp;
	uint8_t		ps_poll;
	uint8_t		null_data;
	uint8_t		null_data_qos;
	uint8_t		null_data_qos_bt;
} __packed;

/* Structure for R88E_CMD_MSR_RPT. */
struct r88e_fw_cmd_msrrpt {
	uint8_t		msrb0;
#define R88E_MSRRPT_B0_DISASSOC		0x00
#define R88E_MSRRPT_B0_ASSOC		0x01

	uint8_t		macid;
} __packed;

/* Structure for R88E_CMD_SET_PWRMODE. */
struct r88e_fw_cmd_pwrmode {
	uint8_t		mode;
#define R88E_PWRMODE_CAM		0
#define R88E_PWRMODE_LEG		1
#define R88E_PWRMODE_UAPSD		2

	uint8_t		pwrb1;
#define R88E_PWRMODE_B1_RLBM_M		0x0f
#define R88E_PWRMODE_B1_RLBM_S		0
#define R88E_PWRMODE_B1_MODE_MIN	0
#define R88E_PWRMODE_B1_MODE_MAX	1
#define R88E_PWRMODE_B1_MODE_DTIM	2

#define R88E_PWRMODE_B1_SMART_PS_M	0xf0
#define R88E_PWRMODE_B1_SMART_PS_S	4
#define R88E_PWRMODE_B1_LEG_PSPOLL0	0
#define R88E_PWRMODE_B1_LEG_PSPOLL1	1
#define R88E_PWRMODE_B1_LEG_NULLDATA	2
#define R88E_PWRMODE_B1_WMM_PSPOLL	0
#define R88E_PWRMODE_B1_WMM_NULLDATA	1

	uint8_t		bcn_pass;
	uint8_t		queue_uapsd;
	uint8_t		pwr_state;
#define R88E_PWRMODE_STATE_RFOFF	0x00
#define R88E_PWRMODE_STATE_RFON		0x04
#define R88E_PWRMODE_STATE_ALLON	0x0c
} __packed;

#endif	/* R88E_FW_CMD_H */

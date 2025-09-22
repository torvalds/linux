/*	$OpenBSD: acxreg.h,v 1.13 2022/01/09 05:42:38 jsg Exp $ */

/*
 * Copyright (c) 2006 Jonathan Gray <jsg@openbsd.org>
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

/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _ACXREG_H
#define _ACXREG_H

/*
 * IO register index
 */
#define ACXREG_SOFT_RESET		0
#define ACXREG_FWMEM_ADDR		1
#define ACXREG_FWMEM_DATA		2
#define ACXREG_FWMEM_CTRL		3
#define ACXREG_FWMEM_START		4
#define ACXREG_EVENT_MASK		5
#define ACXREG_INTR_TRIG		6
#define ACXREG_INTR_MASK		7
#define ACXREG_INTR_STATUS		8
#define ACXREG_INTR_STATUS_CLR		9	/* cleared after being read */
#define ACXREG_INTR_ACK			10
#define ACXREG_HINTR_TRIG		11	/* XXX what's this? */
#define ACXREG_RADIO_ENABLE		12
#define ACXREG_EEPROM_INIT		13
#define ACXREG_EEPROM_CTRL		14
#define ACXREG_EEPROM_ADDR		15
#define ACXREG_EEPROM_DATA		16
#define ACXREG_EEPROM_CONF		17
#define ACXREG_EEPROM_INFO		18
#define ACXREG_PHY_ADDR			19
#define ACXREG_PHY_DATA			20
#define ACXREG_PHY_CTRL			21
#define ACXREG_GPIO_OUT_ENABLE		22
#define ACXREG_GPIO_OUT			23
#define ACXREG_CMD_REG_OFFSET		24
#define ACXREG_INFO_REG_OFFSET		25
#define ACXREG_RESET_SENSE		26
#define ACXREG_ECPU_CTRL		27
#define ACXREG_MAX			28
#define ACXREG(reg, val)		[ACXREG_##reg] = val

/*
 * Value read from ACXREG_EEPROM_INFO
 * upper 8bits are radio type
 * lower 8bits are form factor
 */
#define ACX_EEINFO_RADIO_TYPE_SHIFT	8
#define ACX_EEINFO_RADIO_TYPE_MASK	(0xff << ACX_EEINFO_RADIO_TYPE_SHIFT)
#define ACX_EEINFO_FORM_FACTOR_MASK	0xff

#define ACX_EEINFO_HAS_RADIO_TYPE(info)	((info) & ACX_EEINFO_RADIO_TYPE_MASK)
#define ACX_EEINFO_RADIO_TYPE(info)	((info) >> ACX_EEINFO_RADIO_TYPE_SHIFT)
#define ACX_EEINFO_FORM_FACTOR(info)	((info) & ACX_EEINFO_FORM_FACTOR_MASK)

/*
 * Size of command register whose location is obtained
 * from ACXREG_CMD_REG_OFFSET IO register
 */
#define ACX_CMD_REG_SIZE		4	/* 4 bytes */

/*
 * Size of information register whose location is obtained
 * from ACXREG_INFO_REG_OFFSET IO register
 */
#define ACX_INFO_REG_SIZE		4	/* 4 bytes */

/*
 * Offset of EEPROM variables
 */
#define ACX_EE_VERSION_OFS		0x05

/*
 * Possible values for various IO registers
 */

/* ACXREG_SOFT_RESET */
#define ACXRV_SOFT_RESET		0x1

/* ACXREG_FWMEM_START */
#define ACXRV_FWMEM_START_OP		0x0

/* ACXREG_FWMEM_CTRL */
#define ACXRV_FWMEM_ADDR_AUTOINC	0x10000

/* ACXREG_EVENT_MASK */
#define ACXRV_EVENT_DISABLE		0x8000	/* XXX What's this?? */

/* ACXREG_INTR_TRIG */
#define ACXRV_TRIG_CMD_FINI		0x0001
#define ACXRV_TRIG_TX_FINI		0x0004

/* ACXREG_INTR_MASK */
#define ACXRV_INTR_RX_DATA		0x0001
#define ACXRV_INTR_TX_FINI		0x0002
#define ACXRV_INTR_TX_XFER		0x0004
#define ACXRV_INTR_RX_FINI		0x0008
#define ACXRV_INTR_DTIM			0x0010
#define ACXRV_INTR_BEACON		0x0020
#define ACXRV_INTR_TIMER		0x0040
#define ACXRV_INTR_KEY_MISS		0x0080
#define ACXRV_INTR_WEP_FAIL		0x0100
#define ACXRV_INTR_CMD_FINI		0x0200
#define ACXRV_INTR_INFO			0x0400
#define ACXRV_INTR_OVERFLOW		0x0800	/* XXX */
#define ACXRV_INTR_PROC_ERR		0x1000	/* XXX */
#define ACXRV_INTR_SCAN_FINI		0x2000
#define ACXRV_INTR_FCS_THRESH		0x4000	/* XXX */
#define ACXRV_INTR_UNKN			0x8000
#define ACXRV_INTR_ALL			0xffff

/* ACXREG_EEPROM_INIT */
#define ACXRV_EEPROM_INIT		0x1

/* ACXREG_EEPROM_CTRL */
#define ACXRV_EEPROM_READ		0x2

/* ACXREG_PHY_CTRL */
#define ACXRV_PHY_WRITE			0x1
#define ACXRV_PHY_READ			0x2

/* ACXREG_PHY_ADDR */
#define ACXRV_PHYREG_TXPOWER		0x11	/* axc100 */
#define ACXRV_PHYREG_SENSITIVITY	0x30

/* ACXREG_ECPU_CTRL */
#define ACXRV_ECPU_HALT			0x1
#define ACXRV_ECPU_START		0x0

/* Commands */
#define ACXCMD_GET_CONF		0x01
#define ACXCMD_SET_CONF		0x02
#define ACXCMD_ENABLE_RXCHAN	0x03
#define ACXCMD_ENABLE_TXCHAN	0x04
#define ACXCMD_TMPLT_TIM	0x0a
#define ACXCMD_JOIN_BSS		0x0b
#define ACXCMD_WEP_MGMT		0x0c	/* acx111 */
#define ACXCMD_SLEEP		0x0f
#define ACXCMD_WAKEUP		0x10
#define ACXCMD_INIT_MEM		0x12	/* acx100 */
#define ACXCMD_TMPLT_BEACON	0x13
#define ACXCMD_TMPLT_PROBE_RESP	0x14
#define ACXCMD_TMPLT_NULL_DATA	0x15
#define ACXCMD_TMPLT_PROBE_REQ	0x16
#define ACXCMD_INIT_RADIO	0x18

#if 0
/*
 * acx111 does not agree with acx100 about
 * the meaning of following values.  So they
 * are put into chip specific files.
 */
#define ACX_CONF_FW_RING	0x0003
#define ACX_CONF_MEMOPT		0x0005
#endif
#define ACX_CONF_MEMBLK_SIZE	0x0004	/* acx100 */
#define ACX_CONF_RATE_FALLBACK	0x0006
#define ACX_CONF_WEPOPT		0x0007	/* acx100 */
#define ACX_CONF_MMAP		0x0008
#define ACX_CONF_FWREV		0x000d
#define ACX_CONF_RXOPT		0x0010
#define ACX_CONF_OPTION		0x0015	/* acx111 */
#define ACX_CONF_EADDR		0x1001
#define ACX_CONF_NRETRY_SHORT	0x1005
#define ACX_CONF_NRETRY_LONG	0x1006
#define ACX_CONF_WEPKEY		0x1007	/* acx100 */
#define ACX_CONF_MSDU_LIFETIME	0x1008
#define ACX_CONF_REGDOM		0x100a
#define ACX_CONF_ANTENNA	0x100b
#define ACX_CONF_TXPOWER	0x100d	/* acx111 */
#define ACX_CONF_CCA_MODE	0x100e
#define ACX_CONF_ED_THRESH	0x100f
#define ACX_CONF_WEP_TXKEY	0x1010

/*
 * NOTE:
 * Following structs' fields are little endian
 */

struct acx_conf {
	uint16_t	conf_id;	/* see ACXCONF_ (_acxcmd.h) */
	uint16_t	conf_data_len;
} __packed;

struct acx_conf_mmap {
	struct acx_conf	confcom;
	uint32_t	code_start;
	uint32_t	code_end;
	uint32_t	wep_cache_start;
	uint32_t	wep_cache_end;
	uint32_t	pkt_tmplt_start;
	uint32_t	pkt_tmplt_end;
	uint32_t	fw_desc_start;
	uint32_t	fw_desc_end;
	uint32_t	memblk_start;
	uint32_t	memblk_end;
} __packed;

struct acx_conf_wepopt {
	struct acx_conf	confcom;
	uint16_t	nkey;
	uint8_t		opt;	/* see WEPOPT_ */
} __packed;

#define WEPOPT_HDWEP	0	/* hardware WEP */

struct acx_conf_eaddr {
	struct acx_conf	confcom;
	uint8_t		eaddr[IEEE80211_ADDR_LEN];
} __packed;

struct acx_conf_regdom {
	struct acx_conf	confcom;
	uint8_t		regdom;
	uint8_t		unknown;
} __packed;

struct acx_conf_antenna {
	struct acx_conf	confcom;
	uint8_t		antenna;
} __packed;

struct acx_conf_fwrev {
	struct acx_conf	confcom;
#define ACX_FWREV_LEN	20
	/*
	 * "Rev xx.xx.xx.xx"
	 * '\0' terminated
	 */
	char		fw_rev[ACX_FWREV_LEN];
	uint32_t	hw_id;
} __packed;

struct acx_conf_nretry_long {
	struct acx_conf	confcom;
	uint8_t		nretry;
} __packed;

struct acx_conf_nretry_short {
	struct acx_conf	confcom;
	uint8_t		nretry;
} __packed;

struct acx_conf_msdu_lifetime {
	struct acx_conf	confcom;
	uint32_t	lifetime;
} __packed;

struct acx_conf_rate_fallback {
	struct acx_conf	confcom;
	uint8_t		ratefb_enable;	/* 0/1 */
} __packed;

struct acx_conf_rxopt {
	struct acx_conf	confcom;
	uint16_t	opt1;	/* see RXOPT1_ */
	uint16_t	opt2;	/* see RXOPT2_ */
} __packed;

#define RXOPT1_INCL_RXBUF_HDR	0x2000	/* rxbuf with acx_rxbuf_hdr */
#define RXOPT1_RECV_SSID	0x0400	/* recv frame for joined SSID */
#define RXOPT1_FILT_BCAST	0x0200	/* filt broadcast pkt */
#define RXOPT1_RECV_MCAST1	0x0100	/* recv pkt for multicast addr1 */
#define RXOPT1_RECV_MCAST0	0x0080	/* recv pkt for multicast addr0 */
#define RXOPT1_FILT_ALLMULTI	0x0040	/* filt allmulti pkt */
#define RXOPT1_FILT_FSSID	0x0020	/* filt frame for foreign SSID */
#define RXOPT1_FILT_FDEST	0x0010	/* filt frame for foreign dest addr */
#define RXOPT1_PROMISC		0x0008	/* promisc mode */
#define RXOPT1_INCL_FCS		0x0004
#define RXOPT1_INCL_PHYHDR	0x0000	/* XXX 0x0002 */

#define RXOPT2_RECV_ASSOC_REQ	0x0800
#define RXOPT2_RECV_AUTH	0x0400
#define RXOPT2_RECV_BEACON	0x0200
#define RXOPT2_RECV_CF		0x0100
#define RXOPT2_RECV_CTRL	0x0080
#define RXOPT2_RECV_DATA	0x0040
#define RXOPT2_RECV_BROKEN	0x0020	/* broken frame */
#define RXOPT2_RECV_MGMT	0x0010
#define RXOPT2_RECV_PROBE_REQ	0x0008
#define RXOPT2_RECV_PROBE_RESP	0x0004
#define RXOPT2_RECV_ACK		0x0002	/* RTS/CTS/ACK */
#define RXOPT2_RECV_OTHER	0x0001

struct acx_conf_wep_txkey {
	struct acx_conf	confcom;
	uint8_t		wep_txkey;
} __packed;


struct acx_tmplt_null_data {
	uint16_t	size;
	struct ieee80211_frame data;
} __packed;

struct acx_tmplt_probe_req {
	uint16_t	size;
	union {
		struct {
			struct ieee80211_frame f;
			uint8_t		var[1];
		} __packed	u_data;
		uint8_t		u_mem[0x44];
	}		data;
} __packed;

#define ACX_TMPLT_PROBE_REQ_SIZ(var_len)	\
	(sizeof(uint16_t) + sizeof(struct ieee80211_frame) + (var_len))

struct acx_tmplt_probe_resp {
	uint16_t	size;
	union {
		struct {
			struct ieee80211_frame f;
			uint8_t		time_stamp[8];
			uint16_t	beacon_intvl;
			uint16_t	cap;
			uint8_t		var[1];
		} __packed	u_data;
		uint8_t		u_mem[0x54];
	}		data;
} __packed;

#define ACX_TMPLT_PROBE_RESP_SIZ(var_len)				\
	(sizeof(uint16_t) + sizeof(struct ieee80211_frame) +		\
	 8 * sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + (var_len))

/* XXX same as acx_tmplt_probe_resp */
struct acx_tmplt_beacon {
	uint16_t	size;
	union {
		struct {
			struct ieee80211_frame f;
			uint8_t		time_stamp[8];
			uint16_t	beacon_intvl;
			uint16_t	cap;
			uint8_t		var[1];
		} __packed	u_data;
		uint8_t		u_mem[0x54];
	}		data;
} __packed;

/* XXX C&P of ACX_TMPLT_PROVE_RESP_SIZ() */
#define ACX_TMPLT_BEACON_SIZ(var_len)					\
	(sizeof(uint16_t) + sizeof(struct ieee80211_frame) +		\
	 8 * sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + (var_len))

/* XXX do NOT belong here */
struct tim_head {
	uint8_t	eid;
	uint8_t	len;
	uint8_t	dtim_count;
	uint8_t	dtim_period;
	uint8_t	bitmap_ctrl;
} __packed;

/* For tim_head.len (tim_head - eid - len + bitmap) */
#define ACX_TIM_LEN(bitmap_len)	\
	(sizeof(struct tim_head) - (2 * sizeof(uint8_t)) + (bitmap_len))
#define ACX_TIM_BITMAP_LEN	1

struct acx_tmplt_tim {
	uint16_t	size;
	union {
		struct {
			struct tim_head	th;
			uint8_t		bitmap[1];
		} __packed	u_data;
		uint8_t		u_mem[0x100];
	}		data;
#define tim_eid		data.u_data.th.eid
#define tim_len		data.u_data.th.len
#define tim_dtim_count	data.u_data.th.dtim_count
#define tim_dtim_period	data.u_data.th.dtim_period
#define tim_bitmap_ctrl	data.u_data.th.bitmap_ctrl
#define tim_bitmap	data.u_data.bitmap
} __packed;

#define ACX_TMPLT_TIM_SIZ(bitmap_len)	\
	(sizeof(uint16_t) + sizeof(struct tim_head) + (bitmap_len))

#define CMDPRM_WRITE_REGION_1(sc, r, rlen)		\
	bus_space_write_region_1((sc)->sc_mem2_bt,	\
				 (sc)->sc_mem2_bh,	\
				 (sc)->sc_cmd_param,	\
				 (const uint8_t *)(r), (rlen))

#define CMDPRM_READ_REGION_1(sc, r, rlen)				\
	bus_space_read_region_1((sc)->sc_mem2_bt, (sc)->sc_mem2_bh,	\
				(sc)->sc_cmd_param, (uint8_t *)(r), (rlen))

/*
 * This will clear previous command's
 * execution status too
 */
#define CMD_WRITE_4(sc, val)					\
	bus_space_write_4((sc)->sc_mem2_bt, (sc)->sc_mem2_bh,	\
			  (sc)->sc_cmd, (val))
#define CMD_READ_4(sc)		\
	bus_space_read_4((sc)->sc_mem2_bt, (sc)->sc_mem2_bh, (sc)->sc_cmd)

/*
 * acx command register layerout:
 * upper 16bits are command execution status
 * lower 16bits are command to be executed
 */
#define ACX_CMD_STATUS_SHIFT	16
#define ACX_CMD_STATUS_OK	1

struct radio_init {
	uint32_t	radio_ofs;	/* radio firmware offset */
	uint32_t	radio_len;	/* radio firmware length */
} __packed;

struct bss_join_hdr {
	uint8_t		bssid[IEEE80211_ADDR_LEN];
	uint16_t	beacon_intvl;
	uint8_t		chip_spec[3];
	uint8_t		ndata_txrate;	/* see ACX_NDATA_TXRATE_ */
	uint8_t		ndata_txopt;	/* see ACX_NDATA_TXOPT_ */
	uint8_t		mode;		/* see ACX_MODE_ */
	uint8_t		channel;
	uint8_t		esslen;
	char		essid[1];
} __packed;

/*
 * non-data frame tx rate
 */
#define ACX_NDATA_TXRATE_1		10	/* 1Mbits/s */
#define ACX_NDATA_TXRATE_2		20	/* 2Mbits/s */

/*
 * non-data frame tx options
 */
#define ACX_NDATA_TXOPT_PBCC		0x40
#define ACX_NDATA_TXOPT_OFDM		0x20
#define ACX_NDATA_TXOPT_SHORT_PREAMBLE	0x10

#define BSS_JOIN_BUFLEN		\
	(sizeof(struct bss_join_hdr) + IEEE80211_NWID_LEN - 1)
#define BSS_JOIN_PARAM_SIZE(bj)	\
	(sizeof(struct bss_join_hdr) + (bj)->esslen - 1)


#define PCIR_BAR(x)     (PCI_MAPS + (x) * 4)

#endif	/* !_ACXREG_H */

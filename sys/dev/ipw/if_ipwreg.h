/*      $FreeBSD$	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2006
 *      Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 */

#define IPW_NTBD	128
#define IPW_TBD_SZ	(IPW_NTBD * sizeof (struct ipw_bd))
#define IPW_NDATA	(IPW_NTBD / 2)
#define IPW_NRBD	128
#define IPW_RBD_SZ	(IPW_NRBD * sizeof (struct ipw_bd))
#define IPW_STATUS_SZ	(IPW_NRBD * sizeof (struct ipw_status))

#define IPW_CSR_INTR		0x0008
#define IPW_CSR_INTR_MASK	0x000c
#define IPW_CSR_INDIRECT_ADDR	0x0010
#define IPW_CSR_INDIRECT_DATA	0x0014
#define IPW_CSR_AUTOINC_ADDR	0x0018
#define IPW_CSR_AUTOINC_DATA	0x001c
#define IPW_CSR_RST		0x0020
#define IPW_CSR_CTL		0x0024
#define IPW_CSR_IO		0x0030
#define IPW_CSR_TX_BASE		0x0200
#define IPW_CSR_TX_SIZE		0x0204
#define IPW_CSR_RX_BASE		0x0240
#define IPW_CSR_STATUS_BASE	0x0244
#define IPW_CSR_RX_SIZE		0x0248
#define IPW_CSR_TX_READ		0x0280
#define IPW_CSR_RX_READ		0x02a0
#define IPW_CSR_TABLE1_BASE	0x0380
#define IPW_CSR_TABLE2_BASE	0x0384
#define IPW_CSR_TX_WRITE	0x0f80
#define IPW_CSR_RX_WRITE	0x0fa0

/* possible flags for register IPW_CSR_INTR */
#define IPW_INTR_TX_TRANSFER	0x00000001
#define IPW_INTR_RX_TRANSFER	0x00000002
#define IPW_INTR_STATUS_CHANGE	0x00000010
#define IPW_INTR_COMMAND_DONE	0x00010000
#define IPW_INTR_FW_INIT_DONE	0x01000000
#define IPW_INTR_FATAL_ERROR	0x40000000
#define IPW_INTR_PARITY_ERROR	0x80000000

#define IPW_INTR_MASK							\
	(IPW_INTR_TX_TRANSFER | IPW_INTR_RX_TRANSFER |			\
	 IPW_INTR_STATUS_CHANGE | IPW_INTR_COMMAND_DONE |		\
	 IPW_INTR_FW_INIT_DONE | IPW_INTR_FATAL_ERROR |			\
	 IPW_INTR_PARITY_ERROR)

/* possible flags for register IPW_CSR_RST */
#define IPW_RST_PRINCETON_RESET	0x00000001
#define IPW_RST_SW_RESET	0x00000080
#define IPW_RST_MASTER_DISABLED	0x00000100
#define IPW_RST_STOP_MASTER	0x00000200

/* possible flags for register IPW_CSR_CTL */
#define IPW_CTL_CLOCK_READY	0x00000001
#define IPW_CTL_ALLOW_STANDBY	0x00000002
#define IPW_CTL_INIT		0x00000004

/* possible flags for register IPW_CSR_IO */
#define IPW_IO_GPIO1_ENABLE	0x00000008
#define IPW_IO_GPIO1_MASK	0x0000000c
#define IPW_IO_GPIO3_MASK	0x000000c0
#define IPW_IO_LED_OFF		0x00002000
#define IPW_IO_RADIO_DISABLED	0x00010000

/* state codes sent by fw on IPW_STATUS_CODE_NEWSTATE interrupt */
#define IPW_STATE_INITIALIZED		0x0001
#define IPW_STATE_CC_FOUND		0x0002	/* 802.11d cc received */
#define IPW_STATE_ASSOCIATED		0x0004
#define IPW_STATE_ASSOCIATION_LOST	0x0008
#define IPW_STATE_ASSOCIATION_CHANGED	0x0010	/* assoc params changed? */
#define IPW_STATE_SCAN_COMPLETE		0x0020
#define IPW_STATE_PS_ENTER		0x0040	/* entered power-save mode */
#define IPW_STATE_PS_EXIT		0x0080	/* exited power-save mode */
#define IPW_STATE_RADIO_DISABLED	0x0100
#define IPW_STATE_DISABLED		0x0200
#define IPW_STATE_POWER_DOWN		0x0400	/* ??? */
#define IPW_STATE_SCANNING		0x0800

/* table1 offsets */
#define IPW_INFO_LOCK			480
#define IPW_INFO_APS_CNT		604
#define IPW_INFO_APS_BASE		608
#define IPW_INFO_CARD_DISABLED		628
#define IPW_INFO_CURRENT_CHANNEL	756
#define IPW_INFO_CURRENT_TX_RATE	768

/* table2 offsets */
#define IPW_INFO_CURRENT_SSID	48
#define IPW_INFO_CURRENT_BSSID	112

/* supported rates */
#define IPW_RATE_DS1	1
#define IPW_RATE_DS2	2
#define IPW_RATE_DS5	4
#define IPW_RATE_DS11	8

/* firmware binary image header */
struct ipw_firmware_hdr {
	uint32_t	version;
	uint32_t	mainsz;
	uint32_t	ucodesz;
} __packed;

/* buffer descriptor */
struct ipw_bd {
	uint32_t	physaddr;
	uint32_t	len;
	uint8_t		flags;
#define IPW_BD_FLAG_TX_FRAME_802_3		0x00
#define IPW_BD_FLAG_TX_NOT_LAST_FRAGMENT	0x01
#define IPW_BD_FLAG_TX_FRAME_COMMAND		0x02
#define IPW_BD_FLAG_TX_FRAME_802_11		0x04
#define IPW_BD_FLAG_TX_LAST_FRAGMENT		0x08
	uint8_t		nfrag;	/* number of fragments */
	uint8_t		reserved[6];
} __packed;

/* status */
struct ipw_status {
	uint32_t	len;
	uint16_t	code;
#define IPW_STATUS_CODE_COMMAND		0
#define IPW_STATUS_CODE_NEWSTATE	1
#define IPW_STATUS_CODE_DATA_802_11	2
#define IPW_STATUS_CODE_DATA_802_3	3
#define IPW_STATUS_CODE_NOTIFICATION	4
	uint8_t		flags;
#define IPW_STATUS_FLAG_DECRYPTED	0x01
#define IPW_STATUS_FLAG_WEP_ENCRYPTED	0x02
#define IPW_STATUS_FLAG_CRC_ERROR	0x04
	uint8_t		rssi;	/* received signal strength indicator */
#define	IPW_RSSI_TO_DBM	(-98)		/* XXX fixed nf to convert dBm */
} __packed;

/* data header */
struct ipw_hdr {
	uint32_t	type;
#define IPW_HDR_TYPE_SEND	33
	uint32_t	subtype;
	uint8_t		encrypted;
	uint8_t		encrypt;
	uint8_t		keyidx;
	uint8_t		keysz;
	uint8_t		key[IEEE80211_KEYBUF_SIZE];
	uint8_t		reserved[10];
	uint8_t		src_addr[IEEE80211_ADDR_LEN];
	uint8_t		dst_addr[IEEE80211_ADDR_LEN];
	uint16_t	fragmentsz;
} __packed;

/* command */
struct ipw_cmd {
	uint32_t	type;
#define IPW_CMD_ENABLE				2
#define IPW_CMD_SET_CONFIGURATION		6
#define IPW_CMD_SET_ESSID			8
#define IPW_CMD_SET_MANDATORY_BSSID		9
#define IPW_CMD_SET_MAC_ADDRESS			11
#define IPW_CMD_SET_MODE			12
#define IPW_CMD_SET_CHANNEL			14
#define IPW_CMD_SET_RTS_THRESHOLD		15
#define IPW_CMD_SET_FRAG_THRESHOLD		16
#define IPW_CMD_SET_POWER_MODE			17
#define IPW_CMD_SET_TX_RATES			18
#define IPW_CMD_SET_BASIC_TX_RATES		19
#define IPW_CMD_SET_WEP_KEY			20
#define IPW_CMD_SET_WEP_KEY_INDEX		25
#define IPW_CMD_SET_WEP_FLAGS			26
#define IPW_CMD_ADD_MULTICAST			27
#define IPW_CMD_SET_BEACON_INTERVAL		29
#define IPW_CMD_SET_TX_POWER_INDEX		36
#define IPW_CMD_BROADCAST_SCAN			43
#define IPW_CMD_DISABLE				44
#define IPW_CMD_SET_DESIRED_BSSID		45
#define IPW_CMD_SET_SCAN_OPTIONS		46
#define IPW_CMD_SET_SCAN_DWELL_TIME		47
#define IPW_CMD_SET_SHORT_RETRY			51
#define IPW_CMD_SET_LONG_RETRY			52
#define IPW_CMD_PREPARE_POWER_DOWN		58
#define IPW_CMD_DISABLE_PHY			61
#define IPW_CMD_SET_MSDU_TX_RATES		62
#define IPW_CMD_SET_SECURITY_INFO		67
#define IPW_CMD_DISASSOCIATE			68
#define IPW_CMD_SET_WPA_IE			69
	uint32_t	subtype;
	uint32_t	seq;
	uint32_t	len;
	uint8_t		data[400];
	uint32_t	status;
	uint8_t		reserved[68];
} __packed;

/* possible values for command IPW_CMD_SET_POWER_MODE */
#define IPW_POWER_MODE_CAM	0
#define IPW_POWER_MODE_AUTO	6

/* possible values for command IPW_CMD_SET_MODE */
#define IPW_MODE_BSS		0
#define IPW_MODE_IBSS		1
#define IPW_MODE_MONITOR	2

/* possible flags for command IPW_CMD_SET_WEP_FLAGS */
#define IPW_WEPON	0x8

/* structure for command IPW_CMD_SET_WEP_KEY */
struct ipw_wep_key {
	uint8_t	idx;
	uint8_t	len;
	uint8_t	key[13];
} __packed;

/* structure for command IPW_CMD_SET_SECURITY_INFORMATION */
struct ipw_security {
	uint32_t	ciphers;
#define IPW_CIPHER_NONE		0x00000001
#define IPW_CIPHER_WEP40	0x00000002
#define IPW_CIPHER_TKIP		0x00000004
#define IPW_CIPHER_CCMP		0x00000010
#define IPW_CIPHER_WEP104	0x00000020
#define IPW_CIPHER_CKIP		0x00000040
	uint16_t	reserved1;
	uint8_t		authmode;
#define IPW_AUTH_OPEN	0
#define IPW_AUTH_SHARED	1
	uint16_t	reserved2;
} __packed;

/* structure for command IPW_CMD_SET_SCAN_OPTIONS */
struct ipw_scan_options {
	uint32_t	flags;
#define IPW_SCAN_DO_NOT_ASSOCIATE	0x00000001
#define IPW_SCAN_MIXED_CELL		0x00000002
#define IPW_SCAN_PASSIVE		0x00000008
	uint32_t	channels;
} __packed;

/* structure for command IPW_CMD_SET_CONFIGURATION */
struct ipw_configuration {
	uint32_t	flags;
#define IPW_CFG_PROMISCUOUS	0x00000004
#define IPW_CFG_PREAMBLE_AUTO	0x00000010
#define IPW_CFG_IBSS_AUTO_START	0x00000020
#define IPW_CFG_802_1x_ENABLE	0x00004000
#define IPW_CFG_BSS_MASK	0x00008000
#define IPW_CFG_IBSS_MASK	0x00010000
	uint32_t	bss_chan;
	uint32_t	ibss_chan;
} __packed;

/* structure for command IPW_CMD_SET_WPA_IE */
struct ipw_wpa_ie {
	uint16_t	mask;
	uint16_t	capinfo;
	uint16_t	lintval;
	uint8_t		bssid[IEEE80211_ADDR_LEN];
	uint32_t	len;
	struct ieee80211_ie_wpa	ie;
} __packed;

/* element in AP table */
struct ipw_node {
	uint32_t	reserved1[2];
	uint8_t		bssid[IEEE80211_ADDR_LEN];
	uint8_t		chan;
	uint8_t		rates;
	uint16_t	reserved2;
	uint16_t	capinfo;
	uint16_t	reserved3;
	uint16_t	intval;
	uint8_t		reserved4[28];
	uint8_t		essid[IEEE80211_NWID_LEN];
	uint16_t	reserved5;
	uint8_t		esslen;
	uint8_t		reserved6[7];
	uint8_t		rssi;
} __packed;

/* EEPROM = Electrically Erasable Programmable Read-Only Memory */

#define IPW_MEM_EEPROM_CTL	0x00300040

#define IPW_EEPROM_RADIO	0x11
#define IPW_EEPROM_MAC		0x21
#define IPW_EEPROM_CHANNEL_LIST	0x37

#define IPW_EEPROM_DELAY	1	/* minimum hold time (microsecond) */

#define IPW_EEPROM_C	(1 << 0)	/* Serial Clock */
#define IPW_EEPROM_S	(1 << 1)	/* Chip Select */
#define IPW_EEPROM_D	(1 << 2)	/* Serial data input */
#define IPW_EEPROM_Q	(1 << 4)	/* Serial data output */

#define IPW_EEPROM_SHIFT_D	2
#define IPW_EEPROM_SHIFT_Q	4

/*
 * control and status registers access macros
 */
#define CSR_READ_1(sc, reg)						\
	bus_space_read_1((sc)->sc_st, (sc)->sc_sh, (reg))

#define CSR_READ_2(sc, reg)						\
	bus_space_read_2((sc)->sc_st, (sc)->sc_sh, (reg))

#define CSR_READ_4(sc, reg)						\
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (reg))

#define CSR_WRITE_1(sc, reg, val)					\
	bus_space_write_1((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define CSR_WRITE_2(sc, reg, val)					\
	bus_space_write_2((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define CSR_WRITE_4(sc, reg, val)					\
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define CSR_WRITE_MULTI_1(sc, reg, buf, len)				\
	bus_space_write_multi_1((sc)->sc_st, (sc)->sc_sh, (reg), 	\
	    (buf), (len))

/*
 * indirect memory space access macros
 */
#define MEM_READ_1(sc, addr)						\
	(CSR_WRITE_4((sc), IPW_CSR_INDIRECT_ADDR, (addr)),		\
	 CSR_READ_1((sc), IPW_CSR_INDIRECT_DATA))

#define MEM_READ_4(sc, addr)						\
	(CSR_WRITE_4((sc), IPW_CSR_INDIRECT_ADDR, (addr)),		\
	 CSR_READ_4((sc), IPW_CSR_INDIRECT_DATA))

#define MEM_WRITE_1(sc, addr, val) do {					\
	CSR_WRITE_4((sc), IPW_CSR_INDIRECT_ADDR, (addr));		\
	CSR_WRITE_1((sc), IPW_CSR_INDIRECT_DATA, (val));		\
} while (/* CONSTCOND */0)

#define MEM_WRITE_2(sc, addr, val) do {					\
	CSR_WRITE_4((sc), IPW_CSR_INDIRECT_ADDR, (addr));		\
	CSR_WRITE_2((sc), IPW_CSR_INDIRECT_DATA, (val));		\
} while (/* CONSTCOND */0)

#define MEM_WRITE_4(sc, addr, val) do {					\
	CSR_WRITE_4((sc), IPW_CSR_INDIRECT_ADDR, (addr));		\
	CSR_WRITE_4((sc), IPW_CSR_INDIRECT_DATA, (val));		\
} while (/* CONSTCOND */0)

#define MEM_WRITE_MULTI_1(sc, addr, buf, len) do {			\
	CSR_WRITE_4((sc), IPW_CSR_INDIRECT_ADDR, (addr));		\
	CSR_WRITE_MULTI_1((sc), IPW_CSR_INDIRECT_DATA, (buf), (len));	\
} while (/* CONSTCOND */0)

/*
 * EEPROM access macro
 */
#define IPW_EEPROM_CTL(sc, val) do {					\
	MEM_WRITE_4((sc), IPW_MEM_EEPROM_CTL, (val));			\
	DELAY(IPW_EEPROM_DELAY);					\
} while (0)

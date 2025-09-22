/*	$OpenBSD: if_iwireg.h,v 1.28 2008/09/04 15:59:52 damien Exp $	*/

/*-
 * Copyright (c) 2004-2008
 *      Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
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

#define IWI_CMD_RING_COUNT	16
#define IWI_TX_RING_COUNT	64
#define IWI_RX_RING_COUNT	32

#define IWI_CSR_INTR		0x0008
#define IWI_CSR_INTR_MASK	0x000c
#define IWI_CSR_INDIRECT_ADDR	0x0010
#define IWI_CSR_INDIRECT_DATA	0x0014
#define IWI_CSR_AUTOINC_ADDR	0x0018
#define IWI_CSR_AUTOINC_DATA	0x001c
#define IWI_CSR_RST		0x0020
#define IWI_CSR_CTL		0x0024
#define IWI_CSR_IO		0x0030
#define IWI_CSR_CMD_BASE	0x0200
#define IWI_CSR_CMD_SIZE	0x0204
#define IWI_CSR_TX_BASE(ac)	(0x0208 + (ac) * 8)
#define IWI_CSR_TX_SIZE(ac)	(0x020c + (ac) * 8)
#define IWI_CSR_CMD_RIDX	0x0280
#define IWI_CSR_TX_RIDX(ac)	(0x0284 + (ac) * 4)
#define IWI_CSR_RX_RIDX		0x02a0
#define IWI_CSR_RX_BASE		0x0500
#define IWI_CSR_TABLE0_SIZE	0x0700
#define IWI_CSR_TABLE0_BASE	0x0704
#define IWI_CSR_NODE_BASE	0x0c0c
#define IWI_CSR_CMD_WIDX	0x0f80
#define IWI_CSR_TX_WIDX(ac)	(0x0f84 + (ac) * 4)
#define IWI_CSR_RX_WIDX		0x0fa0
#define IWI_CSR_READ_INT	0x0ff4

/* aliases */
#define IWI_CSR_CURRENT_TX_RATE	IWI_CSR_TABLE0_BASE

/* possible flags for IWI_CSR_INTR */
#define IWI_INTR_RX_DONE	0x00000002
#define IWI_INTR_CMD_DONE	0x00000800
#define IWI_INTR_TX1_DONE	0x00001000
#define IWI_INTR_TX2_DONE	0x00002000
#define IWI_INTR_TX3_DONE	0x00004000
#define IWI_INTR_TX4_DONE	0x00008000
#define IWI_INTR_FW_INITED	0x01000000
#define IWI_INTR_RADIO_OFF	0x04000000
#define IWI_INTR_FATAL_ERROR	0x40000000
#define IWI_INTR_PARITY_ERROR	0x80000000

#define IWI_INTR_MASK							\
	(IWI_INTR_RX_DONE | IWI_INTR_CMD_DONE | IWI_INTR_TX1_DONE |	\
	 IWI_INTR_TX2_DONE | IWI_INTR_TX3_DONE | IWI_INTR_TX4_DONE |	\
	 IWI_INTR_FW_INITED | IWI_INTR_RADIO_OFF |			\
	 IWI_INTR_FATAL_ERROR | IWI_INTR_PARITY_ERROR)

/* possible flags for register IWI_CSR_RST */
#define IWI_RST_PRINCETON_RESET	0x00000001
#define IWI_RST_SW_RESET	0x00000080
#define IWI_RST_MASTER_DISABLED	0x00000100
#define IWI_RST_STOP_MASTER	0x00000200

/* possible flags for register IWI_CSR_CTL */
#define IWI_CTL_CLOCK_READY	0x00000001
#define IWI_CTL_ALLOW_STANDBY	0x00000002
#define IWI_CTL_INIT		0x00000004

/* possible flags for register IWI_CSR_IO */
#define IWI_IO_RADIO_ENABLED	0x00010000

/* possible flags for IWI_CSR_READ_INT */
#define IWI_READ_INT_INIT_HOST	0x20000000

/* constants for command blocks */
#define IWI_CB_DEFAULT_CTL	0x8cea0000
#define IWI_CB_MAXDATALEN	8191

/* firmware binary image header */
struct iwi_firmware_hdr {
	uint8_t		oldvermaj;
	uint8_t		oldvermin;
	uint8_t		vermaj;
	uint8_t		vermin;
	uint32_t	bootsz;
	uint32_t	ucodesz;
	uint32_t	mainsz;
} __packed;

struct iwi_hdr {
	uint8_t	type;
#define IWI_HDR_TYPE_DATA	0
#define IWI_HDR_TYPE_COMMAND	1
#define IWI_HDR_TYPE_NOTIF	3
#define IWI_HDR_TYPE_FRAME	9

	uint8_t	seq;
	uint8_t	flags;
#define IWI_HDR_FLAG_IRQ	0x04

	uint8_t	reserved;
} __packed;

struct iwi_notif {
	uint32_t	reserved[2];
	uint8_t		type;
#define IWI_NOTIF_TYPE_ASSOCIATION	10
#define IWI_NOTIF_TYPE_AUTHENTICATION	11
#define IWI_NOTIF_TYPE_SCAN_CHANNEL	12
#define IWI_NOTIF_TYPE_SCAN_COMPLETE	13
#define IWI_NOTIF_TYPE_BAD_LINK		15
#define IWI_NOTIF_TYPE_BEACON		17
#define IWI_NOTIF_TYPE_PAIRWISE_KEY	18
#define IWI_NOTIF_TYPE_CALIBRATION	20
#define IWI_NOTIF_TYPE_NOISE		25

	uint8_t		flags;
	uint16_t	len;
} __packed;

/* structure for notification IWI_NOTIF_TYPE_AUTHENTICATION */
struct iwi_notif_authentication {
	uint8_t	state;
#define IWI_DEAUTHENTICATED	0
#define IWI_AUTHENTICATED	9
} __packed;

/* structure for notification IWI_NOTIF_TYPE_ASSOCIATION */
struct iwi_notif_association {
	uint8_t			state;
#define IWI_DEASSOCIATED	0
#define IWI_ASSOCIATED		12

	struct ieee80211_frame	frame;
	uint16_t		capinfo;
	uint16_t		status;
	uint16_t		associd;
} __packed;

/* structure for notification IWI_NOTIF_TYPE_SCAN_CHANNEL */
struct iwi_notif_scan_channel {
	uint8_t	nchan;
	uint8_t	reserved[47];
} __packed;

/* structure for notification IWI_NOTIF_TYPE_SCAN_COMPLETE */
struct iwi_notif_scan_complete {
	uint8_t	type;
	uint8_t	nchan;
	uint8_t	status;
	uint8_t	reserved;
} __packed;

/* structure for notification IWI_NOTIF_TYPE_BEACON */
struct iwi_notif_beacon {
	uint32_t	status;
#define IWI_BEACON_MISSED	1

	uint32_t	count;
};

/* received frame header */
struct iwi_frame {
	uint32_t	reserved1[2];
	uint8_t		chan;
	uint8_t		status;
	uint8_t		rate;
	uint8_t		rssi;	/* receiver signal strength indicator */
	uint8_t		agc;	/* automatic gain control */
	uint8_t		rssi_dbm;
	uint16_t	signal;
	uint16_t	noise;
	uint8_t		antenna;
	uint8_t		control;
	uint8_t		reserved2[2];
	uint16_t	len;
} __packed;

/* header for transmission */
struct iwi_tx_desc {
	struct iwi_hdr	hdr;
	uint32_t	reserved1;
	uint8_t		station;
	uint8_t		reserved2[3];
	uint8_t		cmd;
#define IWI_DATA_CMD_TX	0x0b

	uint8_t		seq;
	uint16_t	len;
	uint8_t		priority;
	uint8_t		flags;
#define IWI_DATA_FLAG_SHPREAMBLE	(1 << 2)
#define IWI_DATA_FLAG_NO_WEP		(1 << 5)
#define IWI_DATA_FLAG_NEED_ACK		(1 << 7)

	uint8_t		xflags;
#define IWI_DATA_XFLAG_CCK		(1 << 0)
#define IWI_DATA_XFLAG_QOS		(1 << 4)

	uint8_t		txkey;
#define IWI_DATA_KEY_USE_PAIRWISE	(1 << 5)
#define IWI_DATA_KEY_WEP40		(1 << 6)
#define IWI_DATA_KEY_WEP104		(1 << 7)

	uint8_t		wepkey[IEEE80211_KEYBUF_SIZE];
	uint8_t		rate;
	uint8_t		antenna;
	uint8_t		reserved3[10];

	struct ieee80211_qosframe_addr4	wh;
	uint32_t	iv[2];

	uint32_t	nseg;
#define IWI_MAX_NSEG	6
#define IWI_MAX_SCATTER	(IWI_MAX_NSEG - 2)

	uint32_t	seg_addr[IWI_MAX_NSEG];
	uint16_t	seg_len[IWI_MAX_NSEG];
} __packed;

/* command */
struct iwi_cmd_desc {
	struct iwi_hdr	hdr;
	uint8_t		type;
#define IWI_CMD_ENABLE			2
#define IWI_CMD_SET_CONFIG		6
#define IWI_CMD_SET_ESSID		8
#define IWI_CMD_SET_MAC_ADDRESS		11
#define IWI_CMD_SET_RTS_THRESHOLD	15
#define IWI_CMD_SET_FRAG_THRESHOLD	16
#define IWI_CMD_SET_POWER_MODE		17
#define IWI_CMD_SET_GROUP_KEY		18
#define IWI_CMD_SET_PAIRWISE_KEY	19
#define IWI_CMD_ASSOCIATE		21
#define IWI_CMD_SET_RATES		22
#define IWI_CMD_SET_QOS_PARAMS		25
#define IWI_CMD_SCAN			26
#define IWI_CMD_SET_OPTIE		31
#define IWI_CMD_DISABLE			33
#define IWI_CMD_SET_RANDOM_SEED		34
#define IWI_CMD_SET_TX_POWER		35
#define IWI_CMD_SET_SENSITIVITY		42
#define IWI_CMD_SET_QOS_CAP		84

	uint8_t		len;
	uint16_t	reserved;
	uint8_t		data[120];
} __packed;

/* node information (IBSS) */
struct iwi_node {
	uint8_t	bssid[IEEE80211_ADDR_LEN];
	uint8_t	reserved[2];
} __packed;

/* constants for 'mode' fields */
#define IWI_MODE_11A	0
#define IWI_MODE_11B	1
#define IWI_MODE_11G	2

/* possible values for command IWI_CMD_SET_POWER_MODE */
#define IWI_POWER_MODE_CAM	0

/* structure for command IWI_CMD_SET_RATES */
struct iwi_rateset {
	uint8_t	mode;
	uint8_t	nrates;
	uint8_t	type;
#define IWI_RATESET_TYPE_NEGOTIATED	0
#define IWI_RATESET_TYPE_SUPPORTED	1

	uint8_t	reserved;
	uint8_t	rates[12];
} __packed;

/* structures for command IWI_CMD_SET_QOS_PARAMS */
struct iwi_qos_params {
	uint16_t	cwmin[EDCA_NUM_AC];
	uint16_t	cwmax[EDCA_NUM_AC];
	uint8_t		aifsn[EDCA_NUM_AC];
	uint8_t		acm  [EDCA_NUM_AC];
	uint8_t		txop [EDCA_NUM_AC];
} __packed;

struct iwi_qos_cmd {
	struct iwi_qos_params	cck;
	struct iwi_qos_params	ofdm;
	struct iwi_qos_params	current;
} __packed;

/* copied verbatim from sys/net80211/ieee80211_output.c */
static const struct ieee80211_edca_ac_params iwi_cck[EDCA_NUM_AC] = {
	[EDCA_AC_BK] = { 5, 10, 7,   0 },
	[EDCA_AC_BE] = { 5, 10, 3,   0 },
	[EDCA_AC_VI] = { 4,  5, 2, 188 },
	[EDCA_AC_VO] = { 3,  4, 2, 102 }
};

/* copied verbatim from sys/net80211/ieee80211_output.c */
static const struct ieee80211_edca_ac_params iwi_ofdm[EDCA_NUM_AC] = {
	[EDCA_AC_BK] = { 4, 10, 7,   0 },
	[EDCA_AC_BE] = { 4, 10, 3,   0 },
	[EDCA_AC_VI] = { 3,  4, 2,  94 },
	[EDCA_AC_VO] = { 2,  3, 2,  47 }
};

/* structure for command IWI_CMD_SET_TX_POWER */
struct iwi_txpower {
	uint8_t	nchan;
	uint8_t	mode;
	struct {
		uint8_t	chan;
		uint8_t	power;
#define IWI_TXPOWER_MAX		20
#define IWI_TXPOWER_RATIO	(IEEE80211_TXPOWER_MAX / IWI_TXPOWER_MAX)
	} __packed chan[37];
} __packed;

/* structure for command IWI_CMD_ASSOCIATE */
struct iwi_associate {
	uint8_t		chan;
	uint8_t		auth;
#define IWI_AUTH_OPEN	0
#define IWI_AUTH_SHARED	1
#define IWI_AUTH_NONE	3

	uint8_t		type;
#define IWI_ASSOC_ASSOCIATE	0
#define IWI_ASSOC_REASSOCIATE	1
#define IWI_ASSOC_DISASSOCIATE	2
#define IWI_ASSOC_SIBSS		3

	uint8_t		reserved1;
	uint16_t	policy;
#define IWI_ASSOC_POLICY_QOS	(1 << 0)
#define IWI_ASSOC_POLICY_RSN	(1 << 1)

	uint8_t		plen;
#define IWI_ASSOC_SHPREAMBLE	(1 << 2)

	uint8_t		mode;
	uint8_t		bssid[IEEE80211_ADDR_LEN];
	uint8_t		tstamp[8];
	uint16_t	capinfo;
	uint16_t	lintval;
	uint16_t	intval;
	uint8_t		dst[IEEE80211_ADDR_LEN];
	uint32_t	reserved3;
	uint16_t	reserved4;
} __packed;

/* structure for command IWI_CMD_SCAN */
struct iwi_scan {
	uint32_t	index;
	uint8_t		channels[54];
#define IWI_CHAN_5GHZ	(0 << 6)
#define IWI_CHAN_2GHZ	(1 << 6)

	uint8_t		type[27];
#define IWI_SCAN_TYPE_PASSIVE	0x11
#define IWI_SCAN_TYPE_DIRECTED	0x22
#define IWI_SCAN_TYPE_BROADCAST	0x33
#define IWI_SCAN_TYPE_BDIRECTED	0x44

	uint8_t		reserved1;
	uint16_t	reserved2;
	uint16_t	passive;	/* dwell time */
	uint16_t	directed;	/* dwell time */
	uint16_t	broadcast;	/* dwell time */
	uint16_t	bdirected;	/* dwell time */
} __packed;

/* structure for command IWI_CMD_SET_CONFIGURATION */
struct iwi_configuration {
	uint8_t	bluetooth_coexistence;
	uint8_t	reserved1;
	uint8_t	answer_pbreq;
	uint8_t	allow_invalid_frames;
	uint8_t	multicast_enabled;
	uint8_t	exclude_unicast_unencrypted;
	uint8_t	disable_unicast_decryption;
	uint8_t	exclude_multicast_unencrypted;
	uint8_t	disable_multicast_decryption;
	uint8_t	antenna;
	uint8_t	reserved2;
	uint8_t	bg_autodetection;
	uint8_t	reserved3;
	uint8_t	enable_multicast_filtering;
	uint8_t	bluetooth_threshold;
	uint8_t	silence_threshold;
	uint8_t	allow_beacon_and_probe_resp;
	uint8_t	allow_mgt;
	uint8_t	report_noise;
	uint8_t	reserved5;
} __packed;

/* structure for command IWI_CMD_SET_GROUP_KEY */
struct iwi_group_key {
	uint8_t	cmd;
#define IWI_GROUP_KEY_CMD_SETKEY	0x08

	uint8_t	seq;
	uint8_t	idx;
	uint8_t	len;
	uint8_t	key[16];
} __packed;

/* structure for command IWI_CMD_SET_PAIRWISE_KEY */
struct iwi_pairwise_key {
	uint8_t		idx;
	uint8_t		cipher;
#define IWI_CIPHER_WEP	0
#define IWI_CIPHER_CCMP	2
#define IWI_CIPHER_TKIP	3
	uint8_t		sta;
	uint8_t		flags;
	uint8_t		key[16];
	uint64_t	tsc;
} __packed;

#define IWI_MEM_EEPROM_CTL	0x00300040
#define IWI_MEM_EVENT_CTL	0x00300004

/* possible flags for register IWI_MEM_EVENT */
#define IWI_LED_ASSOC	(1 << 5)
#define IWI_LED_MASK	0xd9fffffb

/* EEPROM = Electrically Erasable Programmable Read-Only Memory */

#define IWI_EEPROM_MAC	0x21

#define IWI_EEPROM_DELAY	1	/* minimum hold time (microsecond) */

#define IWI_EEPROM_C	(1 << 0)	/* Serial Clock */
#define IWI_EEPROM_S	(1 << 1)	/* Chip Select */
#define IWI_EEPROM_D	(1 << 2)	/* Serial data input */
#define IWI_EEPROM_Q	(1 << 4)	/* Serial data output */

#define IWI_EEPROM_SHIFT_D	2
#define IWI_EEPROM_SHIFT_Q	4

/*
 * control and status registers access macros
 */
#define CSR_READ_1(sc, reg)						\
	bus_space_read_1((sc)->sc_st, (sc)->sc_sh, (reg))

#define CSR_READ_2(sc, reg)						\
	bus_space_read_2((sc)->sc_st, (sc)->sc_sh, (reg))

#define CSR_READ_4(sc, reg)						\
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (reg))

#define CSR_READ_REGION_4(sc, offset, datap, count)			\
	bus_space_read_region_4((sc)->sc_st, (sc)->sc_sh, (offset),	\
	    (datap), (count))

#define CSR_WRITE_1(sc, reg, val)					\
	bus_space_write_1((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define CSR_WRITE_2(sc, reg, val)					\
	bus_space_write_2((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define CSR_WRITE_4(sc, reg, val)					\
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define CSR_WRITE_REGION_1(sc, offset, datap, count)			\
	bus_space_write_region_1((sc)->sc_st, (sc)->sc_sh, (offset),	\
	    (datap), (count))
/*
 * indirect memory space access macros
 */
#define MEM_WRITE_1(sc, addr, val) do {					\
	CSR_WRITE_4((sc), IWI_CSR_INDIRECT_ADDR, (addr));		\
	CSR_WRITE_1((sc), IWI_CSR_INDIRECT_DATA, (val));		\
} while (/* CONSTCOND */0)

#define MEM_WRITE_2(sc, addr, val) do {					\
	CSR_WRITE_4((sc), IWI_CSR_INDIRECT_ADDR, (addr));		\
	CSR_WRITE_2((sc), IWI_CSR_INDIRECT_DATA, (val));		\
} while (/* CONSTCOND */0)

#define MEM_WRITE_4(sc, addr, val) do {					\
	CSR_WRITE_4((sc), IWI_CSR_INDIRECT_ADDR, (addr));		\
	CSR_WRITE_4((sc), IWI_CSR_INDIRECT_DATA, (val));		\
} while (/* CONSTCOND */0)

#define MEM_WRITE_MULTI_1(sc, addr, buf, len) do {			\
	CSR_WRITE_4((sc), IWI_CSR_INDIRECT_ADDR, (addr));		\
	CSR_WRITE_MULTI_1((sc), IWI_CSR_INDIRECT_DATA, (buf), (len));	\
} while (/* CONSTCOND */0)

/*
 * EEPROM access macro
 */
#define IWI_EEPROM_CTL(sc, val) do {					\
	MEM_WRITE_4((sc), IWI_MEM_EEPROM_CTL, (val));			\
	DELAY(IWI_EEPROM_DELAY);					\
} while (/* CONSTCOND */0)

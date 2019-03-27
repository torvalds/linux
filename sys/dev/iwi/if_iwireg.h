/*	$FreeBSD$	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, 2005
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

#define IWI_CMD_RING_COUNT	16
#define IWI_TX_RING_COUNT	64
#define IWI_RX_RING_COUNT	32

#define IWI_TX_DESC_SIZE	(sizeof (struct iwi_tx_desc))
#define IWI_CMD_DESC_SIZE	(sizeof (struct iwi_cmd_desc))

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
#define IWI_CSR_TX1_BASE	0x0208
#define IWI_CSR_TX1_SIZE	0x020c
#define IWI_CSR_TX2_BASE	0x0210
#define IWI_CSR_TX2_SIZE	0x0214
#define IWI_CSR_TX3_BASE	0x0218
#define IWI_CSR_TX3_SIZE	0x021c
#define IWI_CSR_TX4_BASE	0x0220
#define IWI_CSR_TX4_SIZE	0x0224
#define IWI_CSR_CMD_RIDX	0x0280
#define IWI_CSR_TX1_RIDX	0x0284
#define IWI_CSR_TX2_RIDX	0x0288
#define IWI_CSR_TX3_RIDX	0x028c
#define IWI_CSR_TX4_RIDX	0x0290
#define IWI_CSR_RX_RIDX		0x02a0
#define IWI_CSR_RX_BASE		0x0500
#define IWI_CSR_TABLE0_SIZE	0x0700
#define IWI_CSR_TABLE0_BASE	0x0704
#define IWI_CSR_NODE_BASE	0x0c0c 
#define IWI_CSR_CMD_WIDX	0x0f80
#define IWI_CSR_TX1_WIDX	0x0f84
#define IWI_CSR_TX2_WIDX	0x0f88
#define IWI_CSR_TX3_WIDX	0x0f8c
#define IWI_CSR_TX4_WIDX	0x0f90
#define IWI_CSR_RX_WIDX		0x0fa0
#define IWI_CSR_READ_INT	0x0ff4

/* aliases */
#define IWI_CSR_CURRENT_TX_RATE	IWI_CSR_TABLE0_BASE

/* flags for IWI_CSR_INTR */
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
	(IWI_INTR_RX_DONE | IWI_INTR_CMD_DONE |	IWI_INTR_TX1_DONE | 	\
	 IWI_INTR_TX2_DONE | IWI_INTR_TX3_DONE | IWI_INTR_TX4_DONE |	\
	 IWI_INTR_FW_INITED | IWI_INTR_RADIO_OFF |			\
	 IWI_INTR_FATAL_ERROR | IWI_INTR_PARITY_ERROR)

/* flags for IWI_CSR_RST */
#define IWI_RST_PRINCETON_RESET	0x00000001
#define	IWI_RST_STANDBY		0x00000004
#define	IWI_RST_LED_ACTIVITY	0x00000010	/* tx/rx traffic led */
#define	IWI_RST_LED_ASSOCIATED	0x00000020	/* station associated led */
#define	IWI_RST_LED_OFDM	0x00000040	/* ofdm/cck led */
#define IWI_RST_SOFT_RESET	0x00000080
#define IWI_RST_MASTER_DISABLED	0x00000100
#define IWI_RST_STOP_MASTER	0x00000200
#define IWI_RST_GATE_ODMA	0x02000000
#define IWI_RST_GATE_IDMA	0x04000000
#define IWI_RST_GATE_ADMA	0x20000000

/* flags for IWI_CSR_CTL */
#define IWI_CTL_CLOCK_READY	0x00000001
#define IWI_CTL_ALLOW_STANDBY	0x00000002
#define IWI_CTL_INIT		0x00000004

/* flags for IWI_CSR_IO */
#define IWI_IO_RADIO_ENABLED	0x00010000

/* flags for IWI_CSR_READ_INT */
#define IWI_READ_INT_INIT_HOST	0x20000000

/* constants for command blocks */
#define IWI_CB_DEFAULT_CTL	0x8cea0000
#define IWI_CB_MAXDATALEN	8191

/* supported rates */
#define IWI_RATE_DS1	10
#define IWI_RATE_DS2	20
#define IWI_RATE_DS5	55
#define IWI_RATE_DS11	110
#define IWI_RATE_OFDM6	13
#define IWI_RATE_OFDM9	15
#define IWI_RATE_OFDM12	5
#define IWI_RATE_OFDM18	7
#define IWI_RATE_OFDM24	9
#define IWI_RATE_OFDM36	11
#define IWI_RATE_OFDM48	1
#define IWI_RATE_OFDM54	3

/*
 * Old version firmware images start with this header,
 * fields are in little endian (le32) format.
 */
struct iwi_firmware_ohdr {
	uint32_t	version;
	uint32_t	mode;
};
#define	IWI_FW_REQ_MAJOR	2
#define	IWI_FW_REQ_MINOR	4
#define	IWI_FW_GET_MAJOR(ver)	((ver) & 0xff)
#define	IWI_FW_GET_MINOR(ver)	(((ver) & 0xff00) >> 8)

#define	IWI_FW_MODE_UCODE	0
#define	IWI_FW_MODE_BOOT	0
#define	IWI_FW_MODE_BSS		0
#define	IWI_FW_MODE_IBSS	1
#define	IWI_FW_MODE_MONITOR	2

/*
 * New version firmware images contain boot, ucode and firmware
 * all in one chunk. The header at the beginning gives the version
 * and the size of each (sub)image, in le32 format.
 */
struct iwi_firmware_hdr {
	uint32_t	version;	/* version stamp */
	uint32_t	bsize;		/* size of boot image */
	uint32_t	usize;		/* size of ucode image */
	uint32_t	fsize;		/* size of firmware image */
};

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
#define IWI_NOTIF_TYPE_SUCCESS		0
#define IWI_NOTIF_TYPE_UNSPECIFIED	1	/* unspecified failure */
#define IWI_NOTIF_TYPE_ASSOCIATION	10
#define IWI_NOTIF_TYPE_AUTHENTICATION	11
#define IWI_NOTIF_TYPE_SCAN_CHANNEL	12
#define IWI_NOTIF_TYPE_SCAN_COMPLETE	13
#define IWI_NOTIF_TYPE_FRAG_LENGTH	14
#define IWI_NOTIF_TYPE_LINK_QUALITY	15	/* "link deterioration" */
#define IWI_NOTIF_TYPE_BEACON		17	/* beacon state, e.g. miss */
#define	IWI_NOTIF_TYPE_TGI_TX_KEY	18	/* WPA transmit key */
#define IWI_NOTIF_TYPE_CALIBRATION	20
#define IWI_NOTIF_TYPE_NOISE		25

	uint8_t		flags;
	uint16_t	len;
} __packed;

/* structure for notification IWI_NOTIF_TYPE_AUTHENTICATION */
struct iwi_notif_authentication {
	uint8_t	state;
#define IWI_AUTH_FAIL		0
#define	IWI_AUTH_SENT_1		1		/* tx first frame */
#define	IWI_AUTH_RECV_2		2		/* rx second frame */
#define	IWI_AUTH_SEQ1_PASS	3		/* 1st exchange passed */
#define	IWI_AUTH_SEQ1_FAIL	4		/* 1st exchange failed */
#define IWI_AUTH_SUCCESS	9
} __packed;

/* structure for notification IWI_NOTIF_TYPE_ASSOCIATION */
struct iwi_notif_association {
	uint8_t			state;
#define IWI_ASSOC_INIT		0
#define IWI_ASSOC_SUCCESS	12
	uint8_t			pad[11];
} __packed;

/* structure for notification IWI_NOTIF_TYPE_SCAN_CHANNEL */
struct iwi_notif_scan_channel {
	uint8_t	nchan;
	/* XXX this is iwi_cmd_stats, and a u8 reserved field */
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
struct iwi_notif_beacon_state {
	uint32_t state;
#define IWI_BEACON_MISS		1
	uint32_t number;
} __packed;

/* structure(s) for notification IWI_NOTIF_TYPE_LINK_QUALITY */

#define RX_FREE_BUFFERS 32
#define RX_LOW_WATERMARK 8

#define SUP_RATE_11A_MAX_NUM_CHANNELS  8
#define SUP_RATE_11B_MAX_NUM_CHANNELS  4
#define SUP_RATE_11G_MAX_NUM_CHANNELS  12

// Used for passing to driver number of successes and failures per rate
struct iwi_rate_histogram {
        union {
                uint32_t a[SUP_RATE_11A_MAX_NUM_CHANNELS];
                uint32_t b[SUP_RATE_11B_MAX_NUM_CHANNELS];
                uint32_t g[SUP_RATE_11G_MAX_NUM_CHANNELS];
        } success;
        union {
                uint32_t a[SUP_RATE_11A_MAX_NUM_CHANNELS];
                uint32_t b[SUP_RATE_11B_MAX_NUM_CHANNELS];
                uint32_t g[SUP_RATE_11G_MAX_NUM_CHANNELS];
        } failed;
} __packed;

/* statistics command response */
struct iwi_cmd_stats {
	uint8_t cmd_id;
	uint8_t seq_num;
	uint16_t good_sfd;
	uint16_t bad_plcp;
	uint16_t wrong_bssid;
	uint16_t valid_mpdu;
	uint16_t bad_mac_header;
	uint16_t reserved_frame_types;
	uint16_t rx_ina;
	uint16_t bad_crc32;
	uint16_t invalid_cts;
	uint16_t invalid_acks;
	uint16_t long_distance_ina_fina;
	uint16_t dsp_silence_unreachable;
	uint16_t accumulated_rssi;
	uint16_t rx_ovfl_frame_tossed;
	uint16_t rssi_silence_threshold;
	uint16_t rx_ovfl_frame_supplied;
	uint16_t last_rx_frame_signal;
	uint16_t last_rx_frame_noise;
	uint16_t rx_autodetec_no_ofdm;
	uint16_t rx_autodetec_no_barker;
	uint16_t reserved;
} __packed;

#define	SILENCE_OVER_THRESH		(1)
#define	SILENCE_UNDER_THRESH		(2)

struct iwi_notif_link_quality {
	struct iwi_cmd_stats stats;
	uint8_t rate;
	uint8_t modulation;
	struct iwi_rate_histogram histogram;
	uint8_t silence_notification_type;   /* SILENCE_OVER/UNDER_THRESH */
	uint16_t silence_count;
} __packed;

/* received frame header */
struct iwi_frame {
	uint32_t	reserved1[2];
	uint8_t		chan;
	uint8_t		status;
	uint8_t		rate;
	uint8_t		rssi;
	uint8_t		agc;
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
	uint8_t		station;	/* adhoc sta #, 0 for bss */
	uint8_t		reserved2[3];
	uint8_t		cmd;
#define IWI_DATA_CMD_TX	0x0b

	uint8_t		seq;
	uint16_t	len;
	uint8_t		priority;
	uint8_t		flags;
#define IWI_DATA_FLAG_SHPREAMBLE	0x04
#define IWI_DATA_FLAG_NO_WEP		0x20
#define IWI_DATA_FLAG_NEED_ACK		0x80

	uint8_t		xflags;
#define IWI_DATA_XFLAG_QOS	0x10

	uint8_t		wep_txkey;
	uint8_t		wepkey[IEEE80211_KEYBUF_SIZE];
	uint8_t		rate;
	uint8_t		antenna;
	uint8_t		reserved3[10];
	struct ieee80211_qosframe_addr4	wh;
	uint32_t	iv;
	uint32_t	eiv;

	uint32_t	nseg;
#define IWI_MAX_NSEG	6
	uint32_t	seg_addr[IWI_MAX_NSEG];
	uint16_t	seg_len[IWI_MAX_NSEG];
} __packed;

/* command */
struct iwi_cmd_desc {
	struct iwi_hdr	hdr;
	uint8_t		type;
#define IWI_CMD_ENABLE				2
#define IWI_CMD_SET_CONFIG			6
#define IWI_CMD_SET_ESSID			8
#define IWI_CMD_SET_MAC_ADDRESS			11
#define IWI_CMD_SET_RTS_THRESHOLD		15
#define IWI_CMD_SET_FRAG_THRESHOLD		16
#define IWI_CMD_SET_POWER_MODE			17
#define IWI_CMD_SET_WEP_KEY			18
#define IWI_CMD_SCAN				20
#define IWI_CMD_ASSOCIATE			21
#define IWI_CMD_SET_RATES			22
#define IWI_CMD_ABORT_SCAN			23
#define IWI_CMD_SET_WME_PARAMS			25
#define IWI_CMD_SCAN_EXT			26
#define IWI_CMD_SET_OPTIE			31
#define IWI_CMD_DISABLE				33
#define IWI_CMD_SET_IV				34
#define IWI_CMD_SET_TX_POWER			35
#define IWI_CMD_SET_SENSITIVITY			42
#define IWI_CMD_SET_WMEIE			84

	uint8_t		len;
	uint16_t	reserved;
	uint8_t		data[120];
} __packed;

/* node information (IBSS) */
struct iwi_ibssnode {
	uint8_t	bssid[IEEE80211_ADDR_LEN];
	uint8_t	reserved[2];
} __packed;

/* constants for 'mode' fields */
#define IWI_MODE_11A	0
#define IWI_MODE_11B	1
#define IWI_MODE_11G	2

/* possible values for command IWI_CMD_SET_POWER_MODE */
#define IWI_POWER_MODE_CAM	0	/* no power save */
#define IWI_POWER_MODE_PSP	3
#define IWI_POWER_MODE_MAX	5	/* max power save operation */

/* structure for command IWI_CMD_SET_RATES */
struct iwi_rateset {
	uint8_t	mode;
	uint8_t	nrates;
	uint8_t	type;
#define IWI_RATESET_TYPE_NEGOTIATED	0
#define IWI_RATESET_TYPE_SUPPORTED	1

	uint8_t	reserved;
#define	IWI_RATESET_SIZE	12
	uint8_t	rates[IWI_RATESET_SIZE];
} __packed;

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
	uint8_t		chan;		/* channel # */
	uint8_t		auth;		/* type and key */
#define IWI_AUTH_OPEN	0
#define IWI_AUTH_SHARED	1
#define IWI_AUTH_NONE	3

	uint8_t		type;		/* request */
#define	IWI_HC_ASSOC		0
#define	IWI_HC_REASSOC		1
#define	IWI_HC_DISASSOC		2
#define	IWI_HC_IBSS_START	3
#define	IWI_HC_IBSS_RECONF	4
#define	IWI_HC_DISASSOC_QUIET	5
	uint8_t		reserved;
	uint16_t	policy;
#define IWI_POLICY_WME	1
#define IWI_POLICY_WPA	2

	uint8_t		plen;		/* preamble length */
	uint8_t		mode;		/* 11a, 11b, or 11g */
	uint8_t		bssid[IEEE80211_ADDR_LEN];
	uint8_t		tstamp[8];	/* tsf for beacon sync */
	uint16_t	capinfo;
	uint16_t	lintval;	/* listen interval */
	uint16_t	intval;		/* beacon interval */
	uint8_t		dst[IEEE80211_ADDR_LEN];
	uint16_t	atim_window;
	uint8_t		smr;
	uint8_t		reserved1;
	uint16_t	reserved2;
} __packed;

#define	IWI_SCAN_CHANNELS	54

/* structure for command IWI_CMD_SCAN */
struct iwi_scan {
	uint8_t		type;
	uint16_t	dwelltime;	/* channel dwell time (ms) */
	uint8_t		channels[IWI_SCAN_CHANNELS];
#define IWI_CHAN_5GHZ	(0 << 6)
#define IWI_CHAN_2GHZ	(1 << 6)

	uint8_t		reserved[3];
} __packed;

/* scan type codes */
#define IWI_SCAN_TYPE_PASSIVE_STOP	0 /* passive, stop on first beacon */
#define IWI_SCAN_TYPE_PASSIVE		1 /* passive, full dwell on channel */
#define IWI_SCAN_TYPE_DIRECTED		2 /* active, directed probe req */
#define IWI_SCAN_TYPE_BROADCAST		3 /* active, bcast probe req */
#define IWI_SCAN_TYPE_BDIRECTED		4 /* active, directed+bcast probe */
#define IWI_SCAN_TYPES			5

/* scan result codes */
#define	IWI_SCAN_COMPLETED		1 /* scan compeleted successfully */
#define IWI_SCAN_ABORTED		2 /* scan was aborted by the driver */

/* structure for command IWI_CMD_SCAN_EXT */
struct iwi_scan_ext {
	uint32_t	full_scan_index;
	uint8_t		channels[IWI_SCAN_CHANNELS];
	uint8_t		scan_type[IWI_SCAN_CHANNELS / 2];
	uint8_t		reserved;
	uint16_t	dwell_time[IWI_SCAN_TYPES];
} __packed;

/* structure for command IWI_CMD_SET_CONFIG */
struct iwi_configuration {
	uint8_t	bluetooth_coexistence;
	uint8_t	reserved1;
	uint8_t	answer_pbreq;		/* answer bcast ssid probe req frames */
	uint8_t	allow_invalid_frames;	/* accept data frames w/ errors */
	uint8_t	multicast_enabled;	/* accept frames w/ any bssid */
	uint8_t	drop_unicast_unencrypted;
	uint8_t	disable_unicast_decryption;
	uint8_t	drop_multicast_unencrypted;
	uint8_t	disable_multicast_decryption;
	uint8_t	antenna;		/* antenna diversity */
#define	IWI_ANTENNA_AUTO	0	/* firmware selects best antenna */
#define	IWI_ANTENNA_A		1	/* use antenna A only */
#define	IWI_ANTENNA_B		3	/* use antenna B only */
#define	IWI_ANTENNA_SLOWDIV	2	/* slow diversity algorithm */
	uint8_t	include_crc;		/* include crc in rx'd frames */
	uint8_t	use_protection;		/* auto-detect 11g operation */
	uint8_t	protection_ctsonly;	/* use CTS-to-self protection */
	uint8_t	enable_multicast_filtering;
	uint8_t	bluetooth_threshold;	/* collision threshold */
	uint8_t	silence_threshold;	/* silence over/under threshold */
	uint8_t	allow_beacon_and_probe_resp;/* accept frames w/ any bssid */
	uint8_t	allow_mgt;		/* accept frames w/ any bssid */
	uint8_t	noise_reported;		/* report noise stats to host */
	uint8_t	reserved5;
} __packed;

/* structure for command IWI_CMD_SET_WEP_KEY */
struct iwi_wep_key {
	uint8_t	cmd;
#define IWI_WEP_KEY_CMD_SETKEY	0x08

	uint8_t	seq;
	uint8_t	idx;
	uint8_t	len;
	uint8_t	key[IEEE80211_KEYBUF_SIZE];
} __packed;

/* structure for command IWI_CMD_SET_WME_PARAMS */
struct iwi_wme_params {
	uint16_t	cwmin[WME_NUM_AC];
	uint16_t	cwmax[WME_NUM_AC];
	uint8_t		aifsn[WME_NUM_AC];
	uint8_t		acm[WME_NUM_AC];
	uint16_t	burst[WME_NUM_AC];
} __packed;

/* structure for command IWI_CMD_SET_SENSITIVTY */
struct iwi_sensitivity {
	uint16_t rssi;			/* beacon rssi in dBm */
#define	IWI_RSSI_TO_DBM		112
	uint16_t reserved;
} __packed;

#define IWI_MEM_EEPROM_EVENT	0x00300004
#define IWI_MEM_EEPROM_CTL	0x00300040

#define IWI_EEPROM_MAC	0x21
#define IWI_EEPROM_NIC	0x25		/* nic type (lsb) */
#define IWI_EEPROM_SKU	0x25		/* nic type (msb) */

#define IWI_EEPROM_DELAY	1	/* minimum hold time (microsecond) */

#define IWI_EEPROM_C	(1 << 0)	/* Serial Clock */
#define IWI_EEPROM_S	(1 << 1)	/* Chip Select */
#define IWI_EEPROM_D	(1 << 2)	/* Serial data input */
#define IWI_EEPROM_Q	(1 << 4)	/* Serial data output */

#define IWI_EEPROM_SHIFT_D    2
#define IWI_EEPROM_SHIFT_Q    4

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

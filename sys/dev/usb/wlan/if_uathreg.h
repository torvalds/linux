/*	$OpenBSD: if_uathreg.h,v 1.2 2006/09/18 16:34:23 damien Exp $	*/
/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2006 Sam Leffler, Errno Consulting
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

#define UATH_CONFIG_INDEX	0
#define UATH_IFACE_INDEX	0

/* all fields are big endian */
struct uath_fwblock {
	uint32_t	flags;
#define UATH_WRITE_BLOCK	(1 << 4)

	uint32_t	len;
#define UATH_MAX_FWBLOCK_SIZE	2048

	uint32_t	total;
	uint32_t	remain;
	uint32_t	rxtotal;
	uint32_t	pad[123];
} __packed;

#define UATH_MAX_CMDSZ		512

/*
 * Messages are passed in Target Endianness.  All fixed-size
 * fields of a WDS Control Message are treated as 32-bit
 * values and Control Msgs are guaranteed to be 32-bit aligned.
 *
 * The format of a WDS Control Message is as follows:
 *    Message Length	32 bits
 *    Message Opcode	32 bits
 *    Message ID	32 bits
 *    parameter 1
 *    parameter 2
 *       ...
 *
 * A variable-length parameter, or a parmeter that is larger than
 * 32 bits is passed as <length, data> pair, where length is a
 * 32-bit quantity and data is padded to 32 bits.
 */
struct uath_cmd_hdr {
	uint32_t	len;		/* msg length including header */
	uint32_t	code;		/* operation code */
/* NB: these are defined for rev 1.5 firmware; rev 1.6 is different */
/* messages from Host -> Target */
#define	WDCMSG_HOST_AVAILABLE		0x01
#define WDCMSG_BIND			0x02
#define WDCMSG_TARGET_RESET		0x03
#define WDCMSG_TARGET_GET_CAPABILITY	0x04
#define WDCMSG_TARGET_SET_CONFIG	0x05
#define WDCMSG_TARGET_GET_STATUS	0x06
#define WDCMSG_TARGET_GET_STATS		0x07
#define WDCMSG_TARGET_START		0x08
#define WDCMSG_TARGET_STOP		0x09
#define WDCMSG_TARGET_ENABLE		0x0a
#define WDCMSG_TARGET_DISABLE		0x0b
#define	WDCMSG_CREATE_CONNECTION	0x0c
#define WDCMSG_UPDATE_CONNECT_ATTR	0x0d
#define	WDCMSG_DELETE_CONNECT		0x0e
#define	WDCMSG_SEND			0x0f
#define WDCMSG_FLUSH			0x10
/* messages from Target -> Host */
#define	WDCMSG_STATS_UPDATE		0x11
#define	WDCMSG_BMISS			0x12
#define	WDCMSG_DEVICE_AVAIL		0x13
#define	WDCMSG_SEND_COMPLETE		0x14
#define	WDCMSG_DATA_AVAIL		0x15
#define	WDCMSG_SET_PWR_MODE		0x16
#define	WDCMSG_BMISS_ACK		0x17
#define	WDCMSG_SET_LED_STEADY		0x18
#define	WDCMSG_SET_LED_BLINK		0x19
/* more messages */
#define	WDCMSG_SETUP_BEACON_DESC	0x1a
#define	WDCMSG_BEACON_INIT		0x1b
#define	WDCMSG_RESET_KEY_CACHE		0x1c
#define	WDCMSG_RESET_KEY_CACHE_ENTRY	0x1d
#define	WDCMSG_SET_KEY_CACHE_ENTRY	0x1e
#define	WDCMSG_SET_DECOMP_MASK		0x1f
#define	WDCMSG_SET_REGULATORY_DOMAIN	0x20
#define	WDCMSG_SET_LED_STATE		0x21
#define	WDCMSG_WRITE_ASSOCID		0x22
#define	WDCMSG_SET_STA_BEACON_TIMERS	0x23
#define	WDCMSG_GET_TSF			0x24
#define	WDCMSG_RESET_TSF		0x25
#define	WDCMSG_SET_ADHOC_MODE		0x26
#define	WDCMSG_SET_BASIC_RATE		0x27
#define	WDCMSG_MIB_CONTROL		0x28
#define	WDCMSG_GET_CHANNEL_DATA		0x29
#define	WDCMSG_GET_CUR_RSSI		0x2a
#define	WDCMSG_SET_ANTENNA_SWITCH	0x2b
#define	WDCMSG_USE_SHORT_SLOT_TIME	0x2f
#define	WDCMSG_SET_POWER_MODE		0x30
#define	WDCMSG_SETUP_PSPOLL_DESC	0x31
#define	WDCMSG_SET_RX_MULTICAST_FILTER	0x32
#define	WDCMSG_RX_FILTER		0x33
#define	WDCMSG_PER_CALIBRATION		0x34
#define	WDCMSG_RESET			0x35
#define	WDCMSG_DISABLE			0x36
#define	WDCMSG_PHY_DISABLE		0x37
#define	WDCMSG_SET_TX_POWER_LIMIT	0x38
#define	WDCMSG_SET_TX_QUEUE_PARAMS	0x39
#define	WDCMSG_SETUP_TX_QUEUE		0x3a
#define	WDCMSG_RELEASE_TX_QUEUE		0x3b
#define	WDCMSG_SET_DEFAULT_KEY		0x43
	uint32_t	msgid;		/* msg id (supplied by host) */
	uint32_t	magic;		/* response desired/target status */
	uint32_t	debug[4];	/* debug data area */
	/* msg data follows */
} __packed;

struct uath_chunk {
	uint8_t		seqnum;		/* sequence number for ordering */
	uint8_t		flags;
#define	UATH_CFLAGS_FINAL	0x01	/* final chunk of a msg */
#define	UATH_CFLAGS_RXMSG	0x02	/* chunk contains rx completion */
#define	UATH_CFLAGS_DEBUG	0x04	/* for debugging */
	uint16_t	length;		/* chunk size in bytes */
	/* chunk data follows */
} __packed;

#define	UATH_RX_DUMMYSIZE		4

/*
 * Message format for a WDCMSG_DATA_AVAIL message from Target to Host.
 */
struct uath_rx_desc {
	uint32_t	len;		/* msg length including header */
	uint32_t	code;		/* WDCMSG_DATA_AVAIL */
	uint32_t	gennum;		/* generation number */
	uint32_t	status;		/* start of RECEIVE_INFO */
#define	UATH_STATUS_OK			0
#define	UATH_STATUS_STOP_IN_PROGRESS	1
#define	UATH_STATUS_CRC_ERR		2
#define	UATH_STATUS_PHY_ERR		3
#define	UATH_STATUS_DECRYPT_CRC_ERR	4
#define	UATH_STATUS_DECRYPT_MIC_ERR	5
#define	UATH_STATUS_DECOMP_ERR		6
#define	UATH_STATUS_KEY_ERR		7
#define	UATH_STATUS_ERR			8
	uint32_t	tstamp_low;	/* low-order 32-bits of rx timestamp */
	uint32_t	tstamp_high;	/* high-order 32-bits of rx timestamp */
	uint32_t	framelen;	/* frame length */
	uint32_t	rate;		/* rx rate code */
	uint32_t	antenna;
	int32_t		rssi;
	uint32_t	channel;
	uint32_t	phyerror;
	uint32_t	connix;		/* key table ix for bss traffic */
	uint32_t	decrypterror;
	uint32_t	keycachemiss;
	uint32_t	pad;		/* XXX? */
} __packed;

struct uath_tx_desc {
	uint32_t	msglen;
	uint32_t	msgid;		/* msg id (supplied by host) */
	uint32_t	type;		/* opcode: WDMSG_SEND or WDCMSG_FLUSH */
	uint32_t	txqid;		/* tx queue id and flags */
#define	UATH_TXQID_MASK		0x0f
#define	UATH_TXQID_MINRATE	0x10	/* use min tx rate */
#define	UATH_TXQID_FF		0x20	/* content is fast frame */
	uint32_t	connid;		/* tx connection id */
#define UATH_ID_INVALID	0xffffffff	/* for sending prior to connection */
	uint32_t	flags;		/* non-zero if response desired */
#define UATH_TX_NOTIFY	(1 << 24)	/* f/w will send a UATH_NOTIF_TX */
	uint32_t	buflen;		/* payload length */
} __packed;

struct uath_cmd_host_available {
	uint32_t	sw_ver_major;
	uint32_t	sw_ver_minor;
	uint32_t	sw_ver_patch;
	uint32_t	sw_ver_build;
} __packed;
#define	ATH_SW_VER_MAJOR	1
#define	ATH_SW_VER_MINOR	5
#define	ATH_SW_VER_PATCH	0
#define	ATH_SW_VER_BUILD	9999

struct uath_cmd_bind {
	uint32_t	targethandle;
	uint32_t	hostapiversion;
} __packed;

/* structure for command WDCMSG_RESET */
struct uath_cmd_reset {
	uint32_t	flags;		/* channel flags */
#define	UATH_CHAN_TURBO	0x0100
#define	UATH_CHAN_CCK	0x0200
#define	UATH_CHAN_OFDM	0x0400
#define	UATH_CHAN_2GHZ	0x1000
#define	UATH_CHAN_5GHZ	0x2000
	uint32_t	freq;		/* channel frequency */
	uint32_t	maxrdpower;
	uint32_t	cfgctl;
	uint32_t	twiceantennareduction;
	uint32_t	channelchange;
	uint32_t	keeprccontent;
} __packed;

/* structure for commands UATH_CMD_READ_MAC and UATH_CMD_READ_EEPROM */
struct uath_read_mac {
	uint32_t	len;
	uint8_t		data[32];
} __packed;

/* structure for command UATH_CMD_WRITE_MAC */
struct uath_write_mac {
	uint32_t	reg;
	uint32_t	len;
	uint8_t		data[32];
} __packed;

/* structure for command UATH_CMD_STA_JOIN */
struct uath_cmd_join_bss {
	uint32_t	bssid;		/* NB: use zero */
	uint32_t	bssmac[2];	/* bssid mac address */
	uint32_t	bsstype;
	uint32_t	wlanmode;
	uint32_t	beaconinterval;
	uint32_t	dtiminterval;
	uint32_t	cfpinterval;
	uint32_t	atimwindow;
	uint32_t	defaultrateix;
	uint32_t	shortslottime11g;
	uint32_t	sleepduration;
	uint32_t	bmissthreshold;
	uint32_t	tcppowerlimit;
	uint32_t	quietduration;
	uint32_t	quietoffset;
	uint32_t	quietackctsallow;
	uint32_t	bssdefaultkey;	/* XXX? */
} __packed;

struct uath_cmd_assoc_bss {
	uint32_t	bssid;
	uint32_t	associd;
} __packed;

struct uath_cmd_start_bss {
	uint32_t	bssid;
} __packed;

/* structure for command UATH_CMD_0C */
struct uath_cmd_0c {
	uint32_t	magic1;
	uint32_t	magic2;
	uint32_t	magic3;
} __packed;

struct uath_cmd_ledsteady {		/* WDCMSG_SET_LED_STEADY */
	uint32_t	lednum;
#define UATH_LED_LINK		0
#define UATH_LED_ACTIVITY	1
	uint32_t	ledmode;
#define UATH_LED_OFF	0
#define UATH_LED_ON	1
} __packed;

struct uath_cmd_ledblink {		/* WDCMSG_SET_LED_BLINK */
	uint32_t	lednum;
	uint32_t	ledmode;
	uint32_t	blinkrate;
	uint32_t	slowmode;
} __packed;

struct uath_cmd_ledstate {		/* WDCMSG_SET_LED_STATE */
	uint32_t	connected;
} __packed;

struct uath_connkey_rec {
	uint8_t		bssid[IEEE80211_ADDR_LEN];
	uint32_t	keyiv;
	uint32_t	extkeyiv;
	uint16_t	keyflags;
	uint16_t	keylen;
	uint16_t	keytype;	/* WEP, TKIP or AES */
	/* As far as I know, MIPS 4Kp is 32-bit processor  */
	uint32_t	priv;
	uint8_t		keyval[32];
	uint16_t	aes_keylen;
	uint8_t		aes_keyval[16];
	uint8_t		mic_txkeyval[8];
	uint8_t		mic_rxkeyval[8];
	int64_t		keyrsc[17];
	int32_t		keytsc[17];
	int32_t		keyexttsc[17];
} __packed;

/* structure for command UATH_CMD_CRYPTO */
struct uath_cmd_crypto {
	uint32_t		keyidx;
#define UATH_DEFAULT_KEY	6
	uint32_t		xorkey;
	uint32_t		size;
	struct uath_connkey_rec	rec;
} __packed;

struct uath_cmd_rateset {
	uint8_t		length;
#define UATH_MAX_NRATES	32
	uint8_t		set[UATH_MAX_NRATES];
};

/* structure for command WDCMSG_SET_BASIC_RATE */
struct uath_cmd_rates {
	uint32_t	connid;
	uint32_t	keeprccontent;
	uint32_t	size;
	struct uath_cmd_rateset rateset;
} __packed;

enum {
	WLAN_MODE_NONE = 0,
	WLAN_MODE_11b,
	WLAN_MODE_11a,
	WLAN_MODE_11g,
	WLAN_MODE_11a_TURBO,
	WLAN_MODE_11g_TURBO,
	WLAN_MODE_11a_TURBO_PRIME,
	WLAN_MODE_11g_TURBO_PRIME,
	WLAN_MODE_11a_XR,
	WLAN_MODE_11g_XR,
};

struct uath_cmd_connection_attr {
	uint32_t	longpreambleonly;
	struct uath_cmd_rateset	rateset;
	uint32_t	wlanmode;
} __packed;

/* structure for command WDCMSG_CREATE_CONNECTION */
struct uath_cmd_create_connection {
	uint32_t	connid;
	uint32_t	bssid;
	uint32_t	size;
	struct uath_cmd_connection_attr	connattr;
} __packed;

struct uath_cmd_txq_setparams {		/* WDCMSG_SET_TX_QUEUE_PARAMS */
	uint32_t	qnum;
	uint32_t	aifs;
	uint32_t	logcwmin;
	uint32_t	logcwmax;
	uint32_t	bursttime;
	uint32_t	qflags;
} __packed;

struct uath_cmd_txq_attr {
	uint32_t	priority;
	uint32_t	aifs;
	uint32_t	logcwmin;
	uint32_t	logcwmax;
	uint32_t	bursttime;
	uint32_t	mode;
	uint32_t	qflags;
} __packed;

struct uath_cmd_txq_setup {		/* WDCMSG_SETUP_TX_QUEUE */
	uint32_t	qid;
	uint32_t	len;
	struct uath_cmd_txq_attr attr;
} __packed;

struct uath_cmd_stoptxdma {		/* WDCMSG_STOP_TX_DMA */
	uint32_t	qnum;
	uint32_t	msec;
} __packed;

/* structure for command UATH_CMD_31 */
struct uath_cmd_31 {
	uint32_t	magic1;
	uint32_t	magic2;
} __packed;

struct uath_cmd_rx_filter {		/* WDCMSG_RX_FILTER */
	uint32_t	bits;
#define UATH_FILTER_RX_UCAST		0x00000001
#define UATH_FILTER_RX_MCAST		0x00000002
#define UATH_FILTER_RX_BCAST		0x00000004
#define UATH_FILTER_RX_CONTROL		0x00000008
#define UATH_FILTER_RX_BEACON		0x00000010	/* beacon frames */
#define UATH_FILTER_RX_PROM		0x00000020	/* promiscuous mode */
#define UATH_FILTER_RX_PHY_ERR		0x00000040	/* phy errors */
#define UATH_FILTER_RX_PHY_RADAR	0x00000080	/* radar phy errors */
#define UATH_FILTER_RX_XR_POOL		0x00000400	/* XR group polls */
#define UATH_FILTER_RX_PROBE_REQ	0x00000800
	uint32_t	op;
#define UATH_FILTER_OP_INIT		0x0
#define UATH_FILTER_OP_SET		0x1
#define UATH_FILTER_OP_CLEAR		0x2
#define UATH_FILTER_OP_TEMP		0x3
#define UATH_FILTER_OP_RESTORE		0x4
} __packed;

struct uath_cmd_rx_mcast_filter {	/* WDCMSG_SET_RX_MCAST_FILTER */
	uint32_t	filter0;
	uint32_t	filter1;
} __packed;

struct uath_cmd_set_associd {		/* WDCMSG_WRITE_ASSOCID */
	uint32_t	defaultrateix;
	uint32_t	associd;
	uint32_t	timoffset;
	uint32_t	turboprime;
	uint32_t	bssid[2];
} __packed;

struct uath_cmd_set_stabeacon_timers {	/* WDCMSG_SET_STA_BEACON_TIMERS */
	uint32_t	nexttbtt;
	uint32_t	nextdtim;
	uint32_t	nextcfp;
	uint32_t	beaconperiod;
	uint32_t	dtimperiod;
	uint32_t	cfpperiod;
	uint32_t	cfpduration;
	uint32_t	sleepduration;
	uint32_t	bsmissthreshold;
} __packed;

enum {
	CFG_NONE,			/* Sentinal to indicate "no config" */
	CFG_REG_DOMAIN,			/* Regulatory Domain */
	CFG_RATE_CONTROL_ENABLE,
	CFG_DEF_XMIT_DATA_RATE,		/* NB: if rate control is not enabled */
	CFG_HW_TX_RETRIES,
	CFG_SW_TX_RETRIES,
	CFG_SLOW_CLOCK_ENABLE,
	CFG_COMP_PROC,
	CFG_USER_RTS_THRESHOLD,
	CFG_XR2NORM_RATE_THRESHOLD,
	CFG_XRMODE_SWITCH_COUNT,
	CFG_PROTECTION_TYPE,
	CFG_BURST_SEQ_THRESHOLD,
	CFG_ABOLT,
	CFG_IQ_LOG_COUNT_MAX,
	CFG_MODE_CTS,
	CFG_WME_ENABLED,
	CFG_GPRS_CBR_PERIOD,
	CFG_SERVICE_TYPE,
	/* MAC Address to use.  Overrides EEPROM */
	CFG_MAC_ADDR,
	CFG_DEBUG_EAR,
	CFG_INIT_REGS,
	/* An ID for use in error & debug messages */
	CFG_DEBUG_ID,
	CFG_COMP_WIN_SZ,
	CFG_DIVERSITY_CTL,
	CFG_TP_SCALE,
	CFG_TPC_HALF_DBM5,
	CFG_TPC_HALF_DBM2,
	CFG_OVERRD_TX_POWER,
	CFG_USE_32KHZ_CLOCK,
	CFG_GMODE_PROTECTION,
	CFG_GMODE_PROTECT_RATE_INDEX,
	CFG_GMODE_NON_ERP_PREAMBLE,
	CFG_WDC_TRANSPORT_CHUNK_SIZE,
};

enum {
	/* Sentinal to indicate "no capability" */
	CAP_NONE,
	CAP_ALL,			/* ALL capabilities */
	CAP_TARGET_VERSION,
	CAP_TARGET_REVISION,
	CAP_MAC_VERSION,
	CAP_MAC_REVISION,
	CAP_PHY_REVISION,
	CAP_ANALOG_5GHz_REVISION,
	CAP_ANALOG_2GHz_REVISION,
	/* Target supports WDC message debug features */
	CAP_DEBUG_WDCMSG_SUPPORT,

	CAP_REG_DOMAIN,
	CAP_COUNTRY_CODE,
	CAP_REG_CAP_BITS,

	CAP_WIRELESS_MODES,
	CAP_CHAN_SPREAD_SUPPORT,
	CAP_SLEEP_AFTER_BEACON_BROKEN,
	CAP_COMPRESS_SUPPORT,
	CAP_BURST_SUPPORT,
	CAP_FAST_FRAMES_SUPPORT,
	CAP_CHAP_TUNING_SUPPORT,
	CAP_TURBOG_SUPPORT,
	CAP_TURBO_PRIME_SUPPORT,
	CAP_DEVICE_TYPE,
	CAP_XR_SUPPORT,
	CAP_WME_SUPPORT,
	CAP_TOTAL_QUEUES,
	CAP_CONNECTION_ID_MAX,		/* Should absorb CAP_KEY_CACHE_SIZE */

	CAP_LOW_5GHZ_CHAN,
	CAP_HIGH_5GHZ_CHAN,
	CAP_LOW_2GHZ_CHAN,
	CAP_HIGH_2GHZ_CHAN,

	CAP_MIC_AES_CCM,
	CAP_MIC_CKIP,
	CAP_MIC_TKIP,
	CAP_MIC_TKIP_WME,
	CAP_CIPHER_AES_CCM,
	CAP_CIPHER_CKIP,
	CAP_CIPHER_TKIP,

	CAP_TWICE_ANTENNAGAIN_5G,
	CAP_TWICE_ANTENNAGAIN_2G,
};

enum {
	ST_NONE,                    /* Sentinal to indicate "no status" */
	ST_ALL,
	ST_SERVICE_TYPE,
	ST_WLAN_MODE,
	ST_FREQ,
	ST_BAND,
	ST_LAST_RSSI,
	ST_PS_FRAMES_DROPPED,
	ST_CACHED_DEF_ANT,
	ST_COUNT_OTHER_RX_ANT,
	ST_USE_FAST_DIVERSITY,
	ST_MAC_ADDR,
	ST_RX_GENERATION_NUM,
	ST_TX_QUEUE_DEPTH,
	ST_SERIAL_NUMBER,
	ST_WDC_TRANSPORT_CHUNK_SIZE,
};

enum {
	BSS_ATTR_BEACON_INTERVAL,
	BSS_ATTR_DTIM_INTERVAL,
	BSS_ATTR_CFP_INTERVAL,
	BSS_ATTR_CFP_MAX_DURATION,
	BSS_ATTR_ATIM_WINDOW,
	BSS_ATTR_DEFAULT_RATE_INDEX,
	BSS_ATTR_SHORT_SLOT_TIME_11g,
	BSS_ATTR_SLEEP_DURATION,
	BSS_ATTR_BMISS_THRESHOLD,
	BSS_ATTR_TPC_POWER_LIMIT,
	BSS_ATTR_BSS_KEY_UPDATE,
};

struct uath_cmd_update_bss_attribute {
	uint32_t	bssid;
	uint32_t	attribute;	/* BSS_ATTR_BEACON_INTERVAL, et al. */
	uint32_t	cfgsize;	/* should be zero 0 */
	uint32_t	cfgdata;
};

struct uath_cmd_update_bss_attribute_key {
	uint32_t	bssid;
	uint32_t	attribute;	/* BSS_ATTR_BSS_KEY_UPDATE */
	uint32_t	cfgsize;	/* size of remaining data */
	uint32_t	bsskeyix;
	uint32_t	isdefaultkey;
	uint32_t	keyiv;		/* IV generation control */
	uint32_t	extkeyiv;	/* extended IV for TKIP & CCM */
	uint32_t	keyflags;
	uint32_t	keytype;
	uint32_t	initvalue;	/* XXX */
	uint32_t	keyval[4];
	uint32_t	mictxkeyval[2];
	uint32_t	micrxkeyval[2];
	uint32_t	keyrsc[2];
};

enum {
	TARGET_DEVICE_AWAKE,
	TARGET_DEVICE_SLEEP,
	TARGET_DEVICE_PWRDN,
	TARGET_DEVICE_PWRSAVE,
	TARGET_DEVICE_SUSPEND,
	TARGET_DEVICE_RESUME,
};

#define UATH_MAX_TXBUFSZ						\
	(sizeof(struct uath_chunk) + sizeof(struct uath_tx_desc) +	\
	IEEE80211_MAX_LEN)

/*
 * it's not easy to measure how the chunk is passed into the host if the target
 * passed the multi-chunks so just we check a minimal size we can imagine.
 */
#define UATH_MIN_RXBUFSZ	(sizeof(struct uath_chunk))

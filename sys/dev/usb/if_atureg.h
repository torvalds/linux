/*	$OpenBSD: if_atureg.h,v 1.34 2022/01/09 05:43:00 jsg Exp $ */
/*
 * Copyright (c) 2003
 *	Daan Vreeken <Danovitsch@Vitsch.net>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Daan Vreeken.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY DAAN VREEKEN AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Daan Vreeken OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#define ATU_CONFIG_NO		1
#define ATU_IFACE_IDX		0

/* the number of simultaneously requested RX transfers */
#define ATU_RX_LIST_CNT	1

/*
 * the number of simultaneously started TX transfers
 * my measurements :
 * 1		430.82 KB/sec
 * 2		534.66 KB/sec
 * 3		536.23 KB/sec
 * 4		537.80 KB/sec
 * 6		537.30 KB/sec
 * 8		535.31 KB/sec
 * 16		535.68 KB/sec
 * 128		535.67 KB/sec (before you ask : yes, 128 is silly :)
 * (+/- 24% increase)
 */
#define ATU_TX_LIST_CNT	8

/*
 * According to the 802.11 spec (7.1.2) the frame body can be up to 2312 bytes
 */
#define ATU_RX_BUFSZ		(ATU_RX_HDRLEN + \
				 sizeof(struct ieee80211_frame_addr4) + 2312 + 4)
/* BE CAREFUL! should add ATU_TX_PADDING */
#define ATU_TX_BUFSZ		(ATU_TX_HDRLEN + \
				 sizeof(struct ieee80211_frame_addr4) + 2312)

#define ATU_MIN_FRAMELEN	60

/*
 * Sending packets of more than 1500 bytes confuses some access points, so the
 * default MTU is set to 1500 but can be increased up to 2310 bytes using
 * ifconfig
 */
#define ATU_DEFAULT_MTU	1500
#define ATU_MAX_MTU		(2312 - 2)

#define ATU_ENDPT_RX		0x0
#define ATU_ENDPT_TX		0x1
#define ATU_ENDPT_MAX		0x2

#define ATU_TX_TIMEOUT		10000
#define ATU_JOIN_TIMEOUT	2000

#define ATU_NO_QUIRK		0x0000
#define ATU_QUIRK_NO_REMAP	0x0001
#define ATU_QUIRK_FW_DELAY	0x0002

#define ATU_DEFAULT_SSID	""
#define ATU_DEFAULT_CHANNEL	10

enum atu_radio_type {
	RadioRFMD = 0,
	RadioRFMD2958,
	RadioRFMD2958_SMC,
	RadioIntersil,
	AT76C503_i3863,
	AT76C503_rfmd_acc,
	AT76C505_rfmd
};

struct atu_type {
	u_int16_t		atu_vid;
	u_int16_t		atu_pid;
	enum atu_radio_type	atu_radio;
	u_int16_t		atu_quirk;
};

struct atu_softc;

struct atu_chain {
	struct atu_softc	*atu_sc;
	struct usbd_xfer	*atu_xfer;
	char			*atu_buf;
	struct mbuf		*atu_mbuf;
	u_int8_t		atu_idx;
	u_int16_t		atu_length;
	int			atu_in_xfer;
	SLIST_ENTRY(atu_chain)	atu_list;
};

/* Radio capture format */

#define ATU_RX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_TSFT)			|	\
	 (1 << IEEE80211_RADIOTAP_FLAGS)		|	\
	 (1 << IEEE80211_RADIOTAP_RATE)			|	\
	 (1 << IEEE80211_RADIOTAP_CHANNEL)		|	\
	 (1 << IEEE80211_RADIOTAP_LOCK_QUALITY)		|	\
	 (1 << IEEE80211_RADIOTAP_RSSI)			|	\
	 0)

struct atu_rx_radiotap_header {
	struct ieee80211_radiotap_header	rr_ihdr;
	u_int64_t				rr_tsft;
	u_int8_t				rr_flags;
	u_int8_t				rr_rate;
	u_int16_t				rr_chan_freq;
	u_int16_t				rr_chan_flags;
	u_int16_t				rr_barker_lock;
	u_int8_t				rr_rssi;
	u_int8_t				rr_max_rssi;
} __packed;

#define ATU_TX_RADIOTAP_PRESENT				\
	((1 << IEEE80211_RADIOTAP_FLAGS)	|	\
	 (1 << IEEE80211_RADIOTAP_RATE)		|	\
	 (1 << IEEE80211_RADIOTAP_CHANNEL)	|	\
	 0)

struct atu_tx_radiotap_header {
	struct ieee80211_radiotap_header	rt_ihdr;
	u_int8_t				rt_flags;
	u_int8_t				rt_rate;
	u_int16_t				rt_chan_freq;
	u_int16_t				rt_chan_flags;
} __packed;

struct atu_cdata {
	struct atu_chain	atu_tx_chain[ATU_TX_LIST_CNT];
	struct atu_chain	atu_rx_chain[ATU_RX_LIST_CNT];

	SLIST_HEAD(atu_list_head, atu_chain)	atu_rx_free;
	struct atu_list_head	atu_tx_free;

	u_int8_t		atu_tx_inuse;
	u_int8_t		atu_tx_last_idx;	
};

#define MAX_SSID_LEN		32
#define ATU_AVG_TIME		20

struct atu_softc {
	struct device           atu_dev;
	struct ieee80211com	sc_ic;
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);

	char			sc_cmd;
#define ATU_C_NONE		0
#define ATU_C_SCAN		1
#define ATU_C_JOIN		2
	struct usb_task		sc_task;

	struct usbd_device	*atu_udev;
	struct usbd_interface	*atu_iface;
	struct ifmedia		atu_media;
	int			atu_ed[ATU_ENDPT_MAX];
	struct usbd_pipe	*atu_ep[ATU_ENDPT_MAX];
	int			atu_unit;
	int			atu_if_flags;

	struct atu_cdata	atu_cdata;

	struct timeval		atu_rx_notice;
	
	u_int8_t		atu_bssid[ETHER_ADDR_LEN];
	enum atu_radio_type	atu_radio;
	u_int16_t		atu_quirk;
	
	u_int8_t		atu_channel;
	u_int16_t		atu_desired_channel;
	u_int8_t		atu_mode;
#define NO_MODE_YET		0
#define AD_HOC_MODE		1
#define INFRASTRUCTURE_MODE	2

	u_int8_t		atu_radio_on;
	caddr_t			sc_radiobpf;

	union {
		struct atu_rx_radiotap_header	tap;
		u_int8_t			pad[64];
	} sc_rxtapu;
	union {
		struct atu_tx_radiotap_header	tap;
		u_int8_t			pad[64];
	} sc_txtapu;

};

#define sc_rxtap	sc_rxtapu.tap
#define sc_txtap	sc_txtapu.tap

/* Commands for uploading the firmware (standard DFU interface) */
#define DFU_DNLOAD		UT_WRITE_CLASS_INTERFACE, 0x01
#define DFU_GETSTATUS		UT_READ_CLASS_INTERFACE, 0x03
#define DFU_GETSTATE		UT_READ_CLASS_INTERFACE, 0x05
#define DFU_REMAP		UT_WRITE_VENDOR_INTERFACE, 0x0a

/* DFU states */
#define DFUState_AppIdle	0
#define DFUState_AppDetach	1
#define DFUState_DFUIdle	2
#define DFUState_DnLoadSync	3
#define DFUState_DnLoadBusy	4
#define DFUState_DnLoadIdle	5
#define DFUState_ManifestSync	6
#define DFUState_Manifest	7
#define DFUState_ManifestWait	8
#define DFUState_UploadIdle	9
#define DFUState_DFUError	10

#define DFU_MaxBlockSize	1024

/* AT76c503 operating modes */
#define MODE_NONE			0x00
#define MODE_NETCARD			0x01
#define MODE_CONFIG			0x02
#define MODE_DFU			0x03
#define MODE_NOFLASHNETCARD		0x04

/* AT76c503 commands */
#define CMD_SET_MIB			0x01
#define CMD_START_SCAN			0x03
#define CMD_JOIN			0x04
#define CMD_START_IBSS			0x05
#define CMD_RADIO			0x06
#define CMD_RADIO_ON			0x06
#define CMD_RADIO_OFF			0x07
#define CMD_STARTUP			0x0b

/* AT76c503 status messages -  used in atu_wait_completion */
#define STATUS_IDLE			0x00
#define STATUS_COMPLETE			0x01
#define STATUS_UNKNOWN			0x02
#define STATUS_INVALID_PARAMETER	0x03
#define STATUS_FUNCTION_NOT_SUPPORTED	0x04
#define STATUS_TIME_OUT			0x07
#define STATUS_IN_PROGRESS		0x08
#define STATUS_HOST_FAILURE		0xff
#define STATUS_SCAN_FAILED		0xf0

/* AT76c503 command header */
struct atu_cmd {
	uByte			Cmd;
	uByte			Reserved;
	uWord			Size;
} __packed;

/* CMD_SET_MIB command (0x01) */
struct atu_cmd_set_mib {
	/* AT76c503 command header */
	uByte		AtCmd;
	uByte		AtReserved;
	uWord		AtSize;

	/* MIB header */
	uByte		MIBType;
	uByte		MIBSize;
	uByte		MIBIndex;
	uByte		MIBReserved;

	/* MIB data */
	uByte		data[72];
} __packed;

/* CMD_STARTUP command (0x0b) */
struct atu_cmd_card_config {
	uByte			Cmd;
	uByte			Reserved;
	uWord			Size;
		
	uByte			ExcludeUnencrypted;
	uByte			PromiscuousMode;
	uByte			ShortRetryLimit;
	uByte			EncryptionType;
	uWord			RTS_Threshold;
	uWord			FragThreshold;		/* 256 .. 2346 */
	uByte			BasicRateSet[4];
	uByte			AutoRateFallback;
	uByte			Channel;
	uByte			PrivacyInvoked;		/* wep */
	uByte			WEP_DefaultKeyID;	/* 0 .. 3 */
	uByte			SSID[MAX_SSID_LEN];
	uByte			WEP_DefaultKey[4][13];
	uByte			SSID_Len;
	uByte			ShortPreamble;
	uWord			BeaconPeriod;
} __packed;

/* CMD_SCAN command (0x03) */
struct atu_cmd_do_scan {
	uByte			Cmd;
	uByte			Reserved;
	uWord			Size;
	
	uByte			BSSID[ETHER_ADDR_LEN];
	uByte			SSID[MAX_SSID_LEN];
	uByte			ScanType;
	uByte			Channel;
	uWord			ProbeDelay;
	uWord			MinChannelTime;
	uWord			MaxChannelTime;
	uByte			SSID_Len;
	uByte			InternationalScan;  
} __packed;

#define ATU_SCAN_ACTIVE		0x00
#define ATU_SCAN_PASSIVE	0x01

/* CMD_JOIN command (0x04) */
struct atu_cmd_join {
	uByte			Cmd;
	uByte			Reserved;
	uWord			Size;
	
	uByte			bssid[ETHER_ADDR_LEN];
	uByte			essid[32];
	uByte			bss_type;
	uByte			channel;
	uWord			timeout;
	uByte			essid_size;
	uByte			reserved;
} __packed;

/* CMD_START_IBSS (0x05) */
struct atu_cmd_start_ibss {
	uByte		Cmd;
	uByte		Reserved;
	uWord		Size;
	
	uByte		BSSID[ETHER_ADDR_LEN];
	uByte		SSID[32];
	uByte		BSSType; 
	uByte		Channel; 
	uByte		SSIDSize;
	uByte		Res[3];  
} __packed;

/*
 * The At76c503 adapters come with different types of radios on them.
 * At this moment the driver supports adapters with RFMD and Intersil radios.
 */

/* The config structure of an RFMD radio */
struct atu_rfmd_conf {
	u_int8_t		CR20[14];
	u_int8_t		CR21[14];
	u_int8_t		BB_CR[14];
	u_int8_t		PidVid[4];
	u_int8_t		MACAddr[ETHER_ADDR_LEN];
	u_int8_t		RegulatoryDomain;
	u_int8_t		LowPowerValues[14];
	u_int8_t		NormalPowerValues[14];
	u_int8_t		Reserved[3];
	/* then we have 84 bytes, somehow Windows reads 95?? */
	u_int8_t		Rest[11];
} __packed;

/* The config structure of an Intersil radio */
struct atu_intersil_conf {
	u_int8_t		MACAddr[ETHER_ADDR_LEN];
	/* From the HFA3861B manual : */
	/* Manual TX power control (7bit : -64 to 63) */
	u_int8_t		CR31[14];
	/* TX power measurement */
	u_int8_t		CR58[14];
	u_int8_t		PidVid[4];
	u_int8_t		RegulatoryDomain;
	u_int8_t		Reserved[1];
} __packed;


/* Firmware information request */
struct atu_fw {
	u_int8_t		major;
	u_int8_t		minor;
	u_int8_t		patch;
	u_int8_t		build;
} __packed;
        
/*
 * The header the AT76c503 puts in front of RX packets (for both management &
 * data)
 */
struct atu_rx_hdr {
	uWord			length;
	uByte			rx_rate;
	uByte			newbss;
	uByte			fragmentation;
	uByte			rssi;
	uByte			link_quality;
	uByte			noise_level;
	uDWord			rx_time;
} __packed;
#define ATU_RX_HDRLEN sizeof(struct atu_rx_hdr)

/*
 * The header we have to put in front of a TX packet before sending it to the
 * AT76c503
 */
struct atu_tx_hdr {
	uWord				length;
	uByte				tx_rate;
	uByte				padding;
	uByte				reserved[4];
} __packed;
#define ATU_TX_HDRLEN sizeof(struct atu_tx_hdr)

#define NR(x)		(void *)((long)x)

/*
 * The linux driver uses separate routines for every mib request they do
 * (eg. set_radio / set_preamble / set_frag / etc etc )
 * We just define a list of types, sizes and offsets and use those
 */

/*	Name				Type		Size	Index	*/
#define MIB_LOCAL			0x01
#define  MIB_LOCAL__BEACON_ENABLE	MIB_LOCAL,	1,	2
#define  MIB_LOCAL__AUTO_RATE_FALLBACK	MIB_LOCAL,	1,	3
#define  MIB_LOCAL__SSID_SIZE		MIB_LOCAL,	1,	5
#define  MIB_LOCAL__PREAMBLE		MIB_LOCAL,	1,	9
#define MIB_MAC_ADDR			0x02
#define  MIB_MAC_ADDR__ADDR		MIB_MAC_ADDR,	6,	0
#define MIB_MAC				0x03
#define  MIB_MAC__FRAG			MIB_MAC,	2,	8
#define  MIB_MAC__RTS			MIB_MAC,	2,	10
#define  MIB_MAC__DESIRED_SSID		MIB_MAC,	32,	28
#define MIB_MAC_MGMT			0x05
#define  MIB_MAC_MGMT__BEACON_PERIOD	MIB_MAC_MGMT,	2,	0
#define  MIB_MAC_MGMT__CURRENT_BSSID	MIB_MAC_MGMT,	6,	14
#define  MIB_MAC_MGMT__CURRENT_ESSID	MIB_MAC_MGMT,	32,	20
#define  MIB_MAC_MGMT__POWER_MODE	MIB_MAC_MGMT,	1,	53
#define  MIB_MAC_MGMT__IBSS_CHANGE	MIB_MAC_MGMT,	1,	54
#define MIB_MAC_WEP			0x06
#define  MIB_MAC_WEP__PRIVACY_INVOKED	MIB_MAC_WEP,	1,	0
#define  MIB_MAC_WEP__KEY_ID		MIB_MAC_WEP,	1,	1
#define  MIB_MAC_WEP__ICV_ERROR_COUNT	MIB_MAC_WEP,	4,	4
#define  MIB_MAC_WEP__EXCLUDED_COUNT	MIB_MAC_WEP,	4,	8
#define  MIB_MAC_WEP__KEYS(nr)		MIB_MAC_WEP,	13,	12+(nr)*13
#define  MIB_MAC_WEP__ENCR_LEVEL	MIB_MAC_WEP,	1,	64
#define MIB_PHY				0x07
#define  MIB_PHY__CHANNEL		MIB_PHY,	1,	20
#define  MIB_PHY__REG_DOMAIN		MIB_PHY,	1,	23
#define MIB_FW_VERSION			0x08
#define MIB_DOMAIN			0x09
#define  MIB_DOMAIN__POWER_LEVELS	MIB_DOMAIN,	14,	0
#define  MIB_DOMAIN__CHANNELS		MIB_DOMAIN,	14,	14

#define ATU_WEP_OFF			0
#define ATU_WEP_40BITS			1
#define ATU_WEP_104BITS			2

#define POWER_MODE_ACTIVE		1
#define POWER_MODE_SAVE			2
#define POWER_MODE_SMART		3

#define PREAMBLE_SHORT			1
#define PREAMBLE_LONG			0
